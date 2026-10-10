"""Compare unseen seeds/parameters, tie latency and fallback latency with ABIDES."""
import copy
import os
os.environ.setdefault('OPENBLAS_NUM_THREADS','1')
os.environ.setdefault('OMP_NUM_THREADS','1')
import argparse
import json
from pathlib import Path

import pyarrow.parquet as pq

from validate import launch


def main():
    root = Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser()
    parser.add_argument('--candidate',default='agenthon-t3:latest')
    parser.add_argument('--baseline',default='agenthon-t3-baseline:local')
    parser.add_argument('--output-dir',type=Path,default=root/'results/variants')
    args=parser.parse_args()
    original = json.loads((root.parent / "track3-simulation-public/units/t3-as01-base-mix/scenario.json").read_text())
    variants = []
    tie = copy.deepcopy(original)
    tie.update(seed=20261010, scenario_id="custom-zero-latency-ties", horizon_ns=1_000_000_000)
    tie["latency_config"] = {"model": "uniform", "params": {"min_ns": 0, "max_ns": 0, "mean_ns": 0}}
    variants.append(tie)
    shock = copy.deepcopy(original)
    shock.update(seed=987654321, scenario_id="custom-shock-and-stp", horizon_ns=2_000_000_000)
    shock["exchange_config"].update(protocol_enforcement=True, stp_policy="cancel_oldest",
                                    ack_delay_ns=2000, compute_delay_ns=75)
    shock["oracle_config"]["params"]["scheduled_jump"] = {"time_ns": 1_000_000_000, "magnitude": -250}
    shock["latency_config"]["params"].update(mean_ns=1700, sigma=2.0, min_ns=0, max_ns=1_000_000)
    variants.append(shock)
    fallback = copy.deepcopy(original)
    fallback.update(seed=54321, scenario_id="custom-geographic-latency", horizon_ns=1_000_000_000)
    fallback.pop("latency_config")
    variants.append(fallback)
    congested=copy.deepcopy(tie)
    congested.update(seed=123,scenario_id='custom-busy-exchange',horizon_ns=100_000_000)
    congested['exchange_config'].update(protocol_enforcement=True,stp_policy='cancel_newest',
                                         ack_delay_ns=10_000_000,compute_delay_ns=500_000)
    variants.append(congested)
    thin=copy.deepcopy(tie)
    thin.update(seed=888,scenario_id='custom-low-price-and-half-rounding')
    thin['oracle_config']['params']['initial_price']=2
    thin['agent_configs']=[{'agent_type':'NoiseTrader','count':4,
                           'params':{'arrival_rate_hz':100,'order_size_mean':10.5,
                                     'order_size_std':0,'price_offset_ticks':0}}]
    variants.append(thin)
    jumps=copy.deepcopy(shock)
    jumps.update(seed=31337,scenario_id='custom-frequent-random-jumps')
    jumps['oracle_config']['params'].update(jump_intensity=25.0,jump_sigma=7.0,sigma=.0005)
    variants.append(jumps)
    report = []
    for scenario in variants:
        out = args.output_dir / scenario["scenario_id"]
        out.mkdir(parents=True, exist_ok=True)
        config = out / "scenario.json"
        config.write_text(json.dumps(scenario))
        for label, image in (("baseline", args.baseline), ("candidate", args.candidate)):
            launch(image, config, out / label, "simulate",
                   ["--config", "/input/scenario.json", "--out", "/output/trace.parquet"])
        equality = {file: pq.read_table(out / "baseline" / file).equals(
                    pq.read_table(out / "candidate" / file), check_metadata=False)
                    for file in ("trace.parquet", "message_trace.parquet")}
        report.append({"scenario": scenario["scenario_id"], "equal": equality,
                       "passed": all(equality.values())})
        print(report[-1], flush=True)
    (args.output_dir / 'validation.json').write_text(json.dumps(report, indent=2) + "\n")
    return not all(r["passed"] for r in report)


if __name__ == "__main__":
    raise SystemExit(main())
