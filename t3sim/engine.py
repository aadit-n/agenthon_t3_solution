"""Run the organizer's market logic with compiled, allocation-conscious helpers."""
import hashlib
import json
import logging
import os
import pathlib
import resource
import time

import numpy as np
import pyarrow as pa
import pyarrow.parquet as pq

from t3sim.schemas import TRACE_SCHEMA, MESSAGE_SCHEMA
_installed = False


def install_optimizations():
    global _installed
    if _installed:
        return
    from abides_core.agent import Agent
    from abides_core.kernel import Kernel
    from abides_markets.agents.exchange_agent import ExchangeAgent
    from abides_markets.orders import LimitOrder, MarketOrder, Order
    from t3sim.hot import (clone_order, log_event, copy_market_value,
                          fast_runner, no_op, order_for_log)
    Agent.logEvent = log_event
    Kernel.runner = fast_runner
    # The upstream summary writer creates a second-resolution directory even
    # with skip_log=True. Independent workers can race on that directory.
    # Summary logs are not submission outputs or inputs to agent decisions.
    Kernel.write_summary_log = no_op
    import abides_markets.order_book as book_module
    import abides_markets.agents.trading_agent as trading_module
    from t3sim.level import CachedPriceLevel
    if os.environ.get("T3SIM_DEPTH_CACHE") == "1":
        book_module.PriceLevel = CachedPriceLevel
    book_module.deepcopy = copy_market_value
    trading_module.deepcopy = copy_market_value
    # Raw debugging logs/book snapshots are never inputs to any scenario agent.
    # BEST_BID/ASK event collection remains enabled and supplies canonical depth.
    ExchangeAgent.kernel_terminating = no_op
    LimitOrder.__deepcopy__ = clone_order
    MarketOrder.__deepcopy__ = clone_order
    Order.to_dict = order_for_log
    logging.disable(logging.INFO)
    pa.set_cpu_count(1)
    pa.set_io_thread_count(1)
    _installed = True


def table_from_rows(rows, schema):
    return pa.Table.from_arrays(
        [pa.array([r[i] for r in rows], type=f.type) for i, f in enumerate(schema)],
        schema=schema)


def extract_fast_trace(agents):
    # parse_logs_df flattens by agent construction order. Preserve that stable
    # tie order, including the reference's "last execution per order" labeling.
    orders = [r for agent in agents for r in agent._trace_orders]
    orders.sort(key=lambda r: r[0])
    final = {}
    for i, row in enumerate(orders):
        if row[2] == "ORDER_EXECUTED":
            final[row[6]] = i
    for i, row in enumerate(orders):
        if row[2] == "ORDER_EXECUTED":
            kind = "ORDER_FILLED" if final[row[6]] == i else "PARTIAL_FILL"
            orders[i] = row[:2] + (kind,) + row[3:]
    orders.extend(r for agent in agents for r in agent._trace_quotes.values())
    orders.sort(key=lambda r: (r[0], r[6]))
    return table_from_rows(orders, TRACE_SCHEMA)


def extract_fast_messages(end_state):
    seqmap = end_state["deliver_seq_by_key"]
    delivered = [(seqmap[(r["message_id"], r["dst_id"])], r)
                 for r in end_state["message_ledger"]
                 if (r["message_id"], r["dst_id"]) in seqmap]
    delivered.sort(key=lambda sr: sr[0])
    arrays = [pa.array([s for s, _ in delivered], type=pa.int64())]
    arrays.extend(pa.array([r[f.name] for _, r in delivered], type=f.type)
                  for f in list(MESSAGE_SCHEMA)[1:])
    return pa.Table.from_arrays(arrays, schema=MESSAGE_SCHEMA)


def sha256(path):
    with open(path, "rb") as fh:
        return hashlib.file_digest(fh, "sha256").hexdigest()


def simulate(config_path, out_path, seed=None):
    started = time.perf_counter()
    scenario = json.loads(pathlib.Path(config_path).read_text(encoding="utf8"))
    if seed is not None:
        scenario["seed"] = int(seed)
    # abides.run actually seeds the kernel with zero, ignoring config's kernel
    # RandomState. Reproduce that behavior exactly rather than "fixing" it.
    if os.environ.get("T3SIM_ENGINE", "native") == "native":
        from t3sim.config import build_native_config
        from t3sim.native import run_native
        config = build_native_config(scenario)
        pa.set_cpu_count(1)
        pa.set_io_thread_count(1)
        sim_started = time.perf_counter()
        trace, messages = run_native(config)
        sim_seconds = time.perf_counter() - sim_started
    else:
        trace, messages, sim_seconds = run_object_engine(scenario)
    return write_outputs(scenario, out_path, trace, messages, started, sim_seconds)


def run_object_engine(scenario):
    from abides_core.kernel import Kernel
    from abides_core.message import Message
    from abides_markets.orders import Order
    from abides_fork.config import ScenarioLatencyModel, build_config
    from t3sim.hot import BufferedLatency, HeapQueue
    install_optimizations()
    Order._order_id_counter = 0
    setattr(Message, "_Message__message_id_counter", 1)
    config = build_config(scenario)
    agents = config['agents']
    for agent in agents:
        agent._trace_orders = []
        agent._trace_quotes = {}
        agent.log_to_file = False
    agents[0].book_logging = False
    latency = config['agent_latency_model']
    if isinstance(latency, ScenarioLatencyModel):
        latency = BufferedLatency(latency)
    kernel = Kernel(agents=agents, start_time=config["start_time"],
                    stop_time=config["stop_time"],
                    agent_latency_model=latency,
                    default_computation_delay=config["default_computation_delay"],
                    custom_properties=config["custom_properties"],
                    random_state=np.random.RandomState(seed=0), skip_log=True)
    kernel.messages = HeapQueue()
    sim_started = time.perf_counter()
    end_state = kernel.run()
    sim_seconds = time.perf_counter() - sim_started
    trace = extract_fast_trace(agents)
    messages = extract_fast_messages(end_state)
    return trace, messages, sim_seconds


def write_outputs(scenario, out_path, trace, messages, started, sim_seconds):
    out_path = pathlib.Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    msg_out = out_path.parent / "message_trace.parquet"
    # Fixed row-group size/encoding is independent of elapsed time, process ID,
    # batch position and scheduling, ensuring byte-stable repeat outputs.
    if os.environ.get('T3SIM_IO_THREADS','2')=='1':
        trace_hash=write_parquet(trace,out_path)
        msg_hash=write_parquet(messages,msg_out)
    else:
        # Arrow's writers release the GIL. The two immutable tables and their
        # files are independent; encoding and row-group boundaries stay fixed.
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(max_workers=2) as pool:
            trace_future=pool.submit(write_parquet,trace,out_path)
            msg_future=pool.submit(write_parquet,messages,msg_out)
            trace_hash=trace_future.result()
            msg_hash=msg_future.result()
    elapsed = time.perf_counter() - started
    events = {
        "scenario_id": str(scenario["scenario_id"]), "seed": int(scenario["seed"]),
        "n_events": trace.num_rows, "n_messages": messages.num_rows,
        "wall_clock_sec": elapsed, "events_per_sec": trace.num_rows / elapsed,
        "trace_sha256": trace_hash, "message_trace_sha256": msg_hash,
        "peak_memory_bytes": int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss) * 1024,
        "gpu_seconds": 0.0, "simulation_loop_sec": sim_seconds,
    }
    (out_path.parent / "events.json").write_text(json.dumps(events, indent=2) + "\n")
    return events


def write_parquet(table,path):
    encoding=os.environ.get('T3SIM_PARQUET_ENCODING','delta')
    options={}
    if encoding!='legacy':
        options['use_dictionary']=[field.name for field in table.schema if pa.types.is_string(field.type)]
        if encoding=='delta':
            options['column_encoding']={field.name:'DELTA_BINARY_PACKED' for field in table.schema
                                        if pa.types.is_integer(field.type)}
    pq.write_table(table,path,compression='snappy',row_group_size=262144,**options)
    return sha256(path)
