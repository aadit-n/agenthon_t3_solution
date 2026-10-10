"""Repeatable local capacity tests for events, messages, orders and batch scaling.

Run from the solution folder:
    .venv-validation/Scripts/python.exe scripts/performance.py --runs 3

The service is a scenario CLI. An order command or exchange event is not an HTTP
request. Full-container and self-reported timings are kept explicitly separate.
"""
import argparse
import os
os.environ.setdefault('OPENBLAS_NUM_THREADS','1')
os.environ.setdefault('OMP_NUM_THREADS','1')
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import statistics
import subprocess
import uuid

import pyarrow.parquet as pq

ORDER_COMMANDS = {"LimitOrderMsg", "MarketOrderMsg", "CancelOrderMsg", "ModifyOrderMsg",
                  "ReplaceOrderMsg", "PartialCancelOrderMsg"}
DEFAULT_CASES = [
    ("mixed", "t3-as01-base-mix", None),
    ("medium", "t3-gb-base-30agent-30s", None),
    ("large", "t3-gb-mega-throughput", None),
    ("batch-1", "t3-gbatch-homog-8", 1),
    ("batch-2", "t3-gbatch-homog-8", 2),
    ("batch-4", "t3-gbatch-homog-8", 4),
]


def sha256(path):
    with path.open("rb") as fh:
        return hashlib.file_digest(fh, "sha256").hexdigest()


def json_command(command):
    return json.loads(subprocess.check_output(command, text=True))


def cgroup_cpu(path):
    if not path.exists():
        return {}
    return {key: int(value) for key, value in (line.split() for line in path.read_text().splitlines())}


def run_container(image, scenario_input, output, workers, timeout):
    output.mkdir(parents=True, exist_ok=True)
    tools = Path(__file__).resolve().parent
    name = "t3-performance-" + uuid.uuid4().hex
    batch = scenario_input.is_dir()
    command = ["docker", "create", "--name", name, "--network", "none", "--cpus", "4",
               "--memory", "16g", "--memory-swap", "16g", "--entrypoint", "sh",
               "-v", str(scenario_input.resolve()) + (":/input:ro" if batch else ":/input/scenario.json:ro"),
               "-v", str(output.resolve()) + ":/output",
               "-v", str(tools) + ":/bench:ro", image, "/bench/perf_measure.sh"]
    command += (["simulate-batch", "--batch-dir", "/input", "--out-dir", "/output", "--workers", str(workers)]
                if batch else ["simulate", "--config", "/input/scenario.json", "--out", "/output/trace.parquet"])
    telemetry_dir = output.parent / (output.name + "-telemetry")
    telemetry_dir.mkdir(parents=True, exist_ok=True)
    try:
        subprocess.run(command, check=True, capture_output=True, timeout=120)
        with (telemetry_dir / "container.log").open("w", encoding="utf8") as log:
            proc = subprocess.run(["docker", "start", "--attach", name], stdout=log,
                                  stderr=subprocess.STDOUT, timeout=timeout)
        state = json_command(["docker", "inspect", name])[0]["State"]
        if proc.returncode or state["ExitCode"] or state["OOMKilled"]:
            raise RuntimeError(f"Container failed: exit={state['ExitCode']}, OOM={state['OOMKilled']}; {telemetry_dir}")
        elapsed = (datetime.fromisoformat(state["FinishedAt"].replace("Z", "+00:00"))
                   - datetime.fromisoformat(state["StartedAt"].replace("Z", "+00:00"))).total_seconds()
        for source, target in (("t3_cpu_before", "cpu-before.txt"), ("t3_cpu_after", "cpu-after.txt"),
                               ("t3_memory_peak", "memory-peak.txt")):
            subprocess.run(["docker", "cp", f"{name}:/tmp/{source}", str(telemetry_dir / target)],
                           check=True, capture_output=True, timeout=30)
        before, after = cgroup_cpu(telemetry_dir / "cpu-before.txt"), cgroup_cpu(telemetry_dir / "cpu-after.txt")
        cpu_seconds = (after["usage_usec"] - before["usage_usec"]) / 1e6
        return {"container_sec": elapsed, "cpu_seconds": cpu_seconds,
                "average_cpu_cores": cpu_seconds / elapsed,
                "cpu_quota_utilization_pct": 100 * cpu_seconds / (4 * elapsed),
                "cgroup_peak_memory_bytes": int((telemetry_dir / "memory-peak.txt").read_text()),
                "throttled_cpu_sec": (after.get("throttled_usec", 0) - before.get("throttled_usec", 0)) / 1e6}
    finally:
        subprocess.run(["docker", "rm", "-f", name], capture_output=True, timeout=30)


def exact_parquet(left, right):
    """Compare complete typed values in bounded memory, regardless of row groups."""
    a, b = pq.ParquetFile(left), pq.ParquetFile(right)
    if a.metadata.num_rows != b.metadata.num_rows or not a.schema_arrow.equals(b.schema_arrow, check_metadata=False):
        return False
    ai, bi = iter(a.iter_batches(batch_size=65536)), iter(b.iter_batches(batch_size=65536))
    ar, br = next(ai, None), next(bi, None)
    while ar is not None and br is not None:
        n = min(ar.num_rows, br.num_rows)
        if not ar.slice(0, n).equals(br.slice(0, n), check_metadata=False):
            return False
        ar = ar.slice(n) if ar.num_rows > n else next(ai, None)
        br = br.slice(n) if br.num_rows > n else next(bi, None)
    return ar is None and br is None


def counts(path):
    count = Counter()
    for batch in pq.ParquetFile(path).iter_batches(batch_size=65536, columns=["msg_type"]):
        count.update(batch.column(0).to_pylist())
    return count


def inspect_outputs(output, unit, batch, reference_check):
    paths = sorted((unit / "scenarios").glob("*.json")) if batch else [unit / "scenario.json"]
    events = messages = order_commands = submissions = fills = 0
    loop_seconds = simulated_seconds = process_rss = 0.0
    for scenario_path in paths:
        scenario = json.loads(scenario_path.read_text())
        folder = output / scenario_path.stem if batch else output
        reference = unit / "checks/reference_data" / scenario_path.stem if batch else unit
        ev = json.loads((folder / "events.json").read_text())
        trace, ledger = folder / "trace.parquet", folder / "message_trace.parquet"
        rows, msgrows = pq.ParquetFile(trace).metadata.num_rows, pq.ParquetFile(ledger).metadata.num_rows
        if rows != ev["n_events"] or msgrows != ev["n_messages"]:
            raise ValueError("Declared event/message count differs from actual Parquet rows")
        if ev["scenario_id"] != scenario["scenario_id"] or ev["seed"] != scenario["seed"]:
            raise ValueError("Output scenario/seed mismatch")
        if ev["trace_sha256"] != sha256(trace) or ev["message_trace_sha256"] != sha256(ledger):
            raise ValueError("Output digest mismatch")
        if reference_check and not (exact_parquet(trace, reference / "trace.parquet")
                                    and exact_parquet(ledger, reference / "message_trace.parquet")):
            raise ValueError("Reference trace/ledger mismatch")
        kinds, msgkinds = counts(trace), counts(ledger)
        events += rows
        messages += msgrows
        order_commands += sum(msgkinds[k] for k in ORDER_COMMANDS)
        submissions += kinds["ORDER_SUBMITTED"]
        fills += kinds["ORDER_FILLED"] + kinds["PARTIAL_FILL"]
        loop_seconds += ev["simulation_loop_sec"]
        simulated_seconds += scenario["horizon_ns"] / 1e9
        process_rss = max(process_rss, ev["peak_memory_bytes"])
    aggregate = json.loads((output / ("batch_events.json" if batch else "events.json")).read_text())
    total = aggregate["total_events"] if batch else aggregate["n_events"]
    if total != events or (batch and aggregate["n_scenarios"] != len(paths)):
        raise ValueError("Invalid batch aggregation")
    reported = aggregate["events_per_sec"]
    if not (math.isfinite(reported) and math.isclose(reported, events / aggregate["wall_clock_sec"], rel_tol=.05)):
        raise ValueError("Inconsistent self-reported throughput")
    binding = {str(p.relative_to(output)): sha256(p) for p in sorted(output.rglob("*.parquet"))}
    return {"events": events, "messages": messages, "order_commands": order_commands,
            "submitted_orders": submissions, "fill_records": fills, "n_markets": len(paths),
            "self_reported_events_per_sec": reported, "sum_market_loop_wall_sec": loop_seconds,
            "simulated_market_seconds": simulated_seconds, "largest_process_peak_rss_bytes": int(process_rss),
            "binding": binding}


def summarize(runs):
    keys = ("events_per_sec", "messages_per_sec", "order_commands_per_sec", "jobs_per_sec", "container_sec",
            "self_reported_events_per_sec", "weighted_market_loop_events_per_sec", "average_cpu_cores",
            "cpu_quota_utilization_pct", "market_seconds_per_wall_second")
    out = {k: {"median": statistics.median(r[k] for r in runs),
               "min": min(r[k] for r in runs), "max": max(r[k] for r in runs)} for k in keys}
    out["peak_cgroup_memory_bytes"] = max(r["cgroup_peak_memory_bytes"] for r in runs)
    out["largest_process_peak_rss_bytes"] = max(r["largest_process_peak_rss_bytes"] for r in runs)
    return out


def prior_roster(folder):
    if not (folder / "run-times.json").exists():
        return None
    times = json.loads((folder / "run-times.json").read_text())
    host, declared = [], []
    for record in times:
        unit = folder / record["unit"]
        ev = json.loads((unit / ("batch_events.json" if (unit / "batch_events.json").exists() else "events.json")).read_text())
        n = ev.get("n_events", ev.get("total_events"))
        host.append(n / record["container_elapsed_sec"])
        declared.append(ev["events_per_sec"])
    return {"units": len(times), "path": str(folder), "protocol": "Single pass over the public roster; not repeat medians",
            "arithmetic_mean_full_container_events_per_sec": statistics.mean(host),
            "arithmetic_mean_self_reported_events_per_sec": statistics.mean(declared)}


def write_report(report, folder):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "performance.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf8")
    gpu = "GPU device attached" if report['environment']['gpu_attached_in_this_test'] else "no GPU attached"
    text = ["This report measures the current simulator on the local machine. Rates count exchange events, delivered messages, or order commands; they are not HTTP request capacity. Every measured run includes container startup, simulation and output writing. The Development board uses a different, self-reported clock, shown separately.\n",
            f"Image: `{report['image_id']}`. Local CPU: {report['environment']['cpu_model']}. Limits: four CPUs, 16 GiB RAM, offline, {gpu}.\n",
            f"Protocol: {report['runs']} measured runs after {report['warmups']} discarded warmups; byte-stable outputs and exact reference values required. Cgroup CPU and memory are collected with a small shell wrapper; its overhead remains included.\n",
            "| Workload | Workers | Events/s | Messages/s | Order commands/s | Job time (s) | Peak cgroup MiB | Avg CPU cores |",
            "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for case in report["cases"]:
        s = case["summary"]
        text.append(f"| {case['case']} | {case['workers'] or 1} | {s['events_per_sec']['median']:,.0f} | {s['messages_per_sec']['median']:,.0f} | {s['order_commands_per_sec']['median']:,.0f} | {s['container_sec']['median']:.3f} | {s['peak_cgroup_memory_bytes']/2**20:,.1f} | {s['average_cpu_cores']['median']:.2f} |")
    text += ["\nCPU cores is CPU-seconds divided by elapsed seconds: one busy core is 25% of the four-CPU quota. Peak cgroup memory includes all workers and charged file cache. Per-process RSS is also retained in JSON. Order commands include limit/market submission, cancel, modify, replace and partial cancel messages; they are not the leaderboard numerator.\n"]
    if report['leader_score'] == 231024.2407:
        text += [f"[Development leaderboard](https://www.agenthon.net/) reference verified on October 10, 2026: **fuzz — {report['leader_score']:,.4f} events/s**. This is a saved provisional Development score, not a controlled Final measurement or an automatically refreshed leaderboard.\n"]
    else:
        text += [f"User-specified comparison target: **{report['leader_score']:,.4f} events/s**.\n"]
    roster = report.get("prior_public_roster")
    if roster:
        declared = roster["arithmetic_mean_self_reported_events_per_sec"]
        text += [f"The complete {roster['units']}-unit local public pass in `{roster.get('path','saved public results')}` averaged **{declared:,.0f} self-reported events/s** and **{roster['arithmetic_mean_full_container_events_per_sec']:,.0f} full-container events/s**. Its self-reported mean is numerically **{declared/report['leader_score']:.2f}×** the comparison target. Hardware, timing controls and evaluation roster are not certified equivalent; these figures do not predict rank.\n"]
    text += ["An updated official score requires uploading this image and running the organizer's evaluation. Detailed repeats, ranges, CPU usage, memory, event/message/order counts and simulated-time speed are in `performance.json`. Three repeats provide a useful local comparison; they do not establish production tail-latency or reliability SLAs.\n"]
    (folder / "REPORT.md").write_text("\n".join(text) + "\n", encoding="utf8")


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", default="agenthon-t3:latest")
    parser.add_argument("--track-repo", type=Path, default=root.parent / "track3-simulation-public")
    parser.add_argument("--output-dir", type=Path, default=root / "results/performance")
    parser.add_argument('--public-results',type=Path,default=root/'results/final-public')
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--case", action="append", choices=[c[0] for c in DEFAULT_CASES])
    parser.add_argument("--leader-score", type=float, default=231024.2407)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    if args.runs < 1 or args.warmups < 0:
        parser.error("--runs must be positive and --warmups nonnegative")
    if not (math.isfinite(args.leader_score) and args.leader_score > 0) or args.timeout < 1:
        parser.error("--leader-score must be positive and finite; --timeout must be positive")
    info = json_command(["docker", "image", "inspect", args.image])[0]
    image_id = info["Id"]
    env = json_command(["docker", "run", "--rm", "--network", "none", "--cpus", "4", "--memory", "16g",
                        "--entrypoint", "python", "-v", str(root / "scripts") + ":/bench:ro", image_id,
                        "/bench/perf_environment.py"])
    report = {"rankable": False, "timestamp_utc": datetime.now(timezone.utc).isoformat(),
              "image_id": image_id, "image_label": args.image, "runs": args.runs, "warmups": args.warmups,
              "leader_score": args.leader_score, "leader_url": "https://www.agenthon.net/",
              "environment": env, "prior_public_roster": prior_roster(args.public_results), "cases": []}
    batch_binding = None
    for label, slug, workers in DEFAULT_CASES:
        if args.case and label not in args.case:
            continue
        unit = args.track_repo / "units" / slug
        case = {"case": label, "unit": slug, "workers": workers, "runs": [], "warmup_runs": []}
        previous_binding = None
        for repeat in range(args.warmups + args.runs):
            output = args.output_dir / label / str(repeat) / "output"
            measured = run_container(image_id, unit / "scenarios" if workers else unit / "scenario.json",
                                     output, workers, args.timeout)
            inspected = inspect_outputs(output, unit, bool(workers), repeat == 0)
            binding = inspected.pop("binding")
            if previous_binding is not None and binding != previous_binding:
                raise RuntimeError(f"Nondeterministic outputs: {label} repeat {repeat}")
            previous_binding = binding
            if workers:
                if batch_binding is not None and binding != batch_binding:
                    raise RuntimeError("Batch workers changed the isolated output bytes")
                batch_binding = binding
            run = {**measured, **inspected}
            elapsed = run["container_sec"]
            for count, rate in (("events", "events_per_sec"), ("messages", "messages_per_sec"),
                                ("order_commands", "order_commands_per_sec"), ("n_markets", "jobs_per_sec")):
                run[rate] = run[count] / elapsed
            run["weighted_market_loop_events_per_sec"] = run["events"] / run["sum_market_loop_wall_sec"]
            run["market_seconds_per_wall_second"] = run["simulated_market_seconds"] / elapsed
            run["repeat"] = repeat
            run["parquet_hashes"] = binding
            key = "warmup_runs" if repeat < args.warmups else "runs"
            case[key].append(run)
            print(f"{label} {'warmup' if key=='warmup_runs' else 'measured'} {repeat}: "
                  f"{run['events_per_sec']:,.0f} events/s, {run['messages_per_sec']:,.0f} messages/s, "
                  f"{run['order_commands_per_sec']:,.0f} order commands/s; "
                  f"{elapsed:.3f}s, {run['cgroup_peak_memory_bytes']/2**20:.1f} MiB", flush=True)
        case["summary"] = summarize(case["runs"])
        report["cases"].append(case)
        write_report(report, args.output_dir)
    print(args.output_dir / "REPORT.md", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
