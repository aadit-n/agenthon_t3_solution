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
    parser.add_argument('--extended',action='store_true',help='add adversarial native-port cases')
    parser.add_argument('--reference-dir',type=Path,help='reuse immutable baseline outputs from an earlier exact run')
    parser.add_argument('--candidate-env',action='append',default=[],help='KEY=VALUE overrides for concurrency/encoding checks')
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
    if args.extended:
        import random
        generator=random.Random(20261010)
        for index in range(12):
            scenario=copy.deepcopy(original)
            scenario.update(seed=generator.randrange(2**32),scenario_id=f'custom-native-port-{index:02}',horizon_ns=750_000_000)
            scenario['exchange_config'].update(protocol_enforcement=True,stp_policy=('cancel_newest','cancel_oldest',None)[index%3],
                ack_delay_ns=(0,1,10000)[index%3],compute_delay_ns=(0,50,500)[index%3])
            scenario['oracle_config']['params'].update(initial_price=(3,100001,234567)[index%3],kappa=(0,.001,3)[index%3],
                sigma=(0,.004,.3)[index%3],jump_intensity=(0,30,350)[index%3],jump_sigma=(0,1.5,-10)[index%3],
                scheduled_jump={'time_ns':234_567_891,'magnitude':(-50,1,1000)[index%3]})
            models=[{'model':'deterministic','params':{'mean_ns':5.5}},
                    {'model':'uniform','params':{'min_ns':.5,'max_ns':100000.5}},
                    {'model':'log_normal','params':{'mean_ns':7.3,'sigma':5,'min_ns':0,'max_ns':1e9}},
                    {'model':'pareto','params':{'alpha':.5,'min_ns':.5,'max_ns':1e9}}]
            scenario['latency_config']=copy.deepcopy(models[index%4])
            for group in scenario['agent_configs']:
                group['count']=generator.randrange(1,5)
                group['params']['arrival_rate_hz']=float(generator.choice((17,43,127)))
                if group['agent_type']=='MarketMaker':group['params'].update(depth_levels=index%5+1,rebalance_interval_ns=17_000_000)
                if group['agent_type']=='NoiseTrader':group['params'].update(order_size_mean=10.5,order_size_std=(0,.3,4)[index%3],price_offset_ticks=index%7)
            variants.append(scenario)
    import subprocess
    args.output_dir.mkdir(parents=True,exist_ok=True)
    (args.output_dir/'image.json').write_text(json.dumps({'candidate':json.loads(subprocess.check_output(['docker','image','inspect',args.candidate]))[0]['Id'],
        'baseline':json.loads(subprocess.check_output(['docker','image','inspect',args.baseline]))[0]['Id']},indent=2)+'\n')
    if args.reference_dir:
        previous=json.loads((args.reference_dir/'image.json').read_text())
        current=json.loads((args.output_dir/'image.json').read_text())
        if previous['baseline']!=current['baseline']:raise ValueError('Baseline reference image differs')
    report = []
    for scenario in variants:
        out = args.output_dir / scenario["scenario_id"]
        out.mkdir(parents=True, exist_ok=True)
        config = out / "scenario.json"
        config.write_text(json.dumps(scenario))
        reference=out/'baseline'
        if args.reference_dir:
            prior=args.reference_dir/scenario['scenario_id']
            if json.loads((prior/'scenario.json').read_text())!=scenario:raise ValueError('Reference scenario differs')
            reference=prior/'baseline'
        for label, image in (("baseline", args.baseline), ("candidate", args.candidate)):
            if label=='baseline' and args.reference_dir:continue
            launch(image, config, out / label, "simulate",
                   ["--config", "/input/scenario.json", "--out", "/output/trace.parquet"],
                   dict(value.split('=',1) for value in args.candidate_env) if label=='candidate' else None)
        equality = {file: pq.read_table(reference / file).equals(
                    pq.read_table(out / "candidate" / file), check_metadata=False)
                    for file in ("trace.parquet", "message_trace.parquet")}
        report.append({"scenario": scenario["scenario_id"], "equal": equality,
                       "passed": all(equality.values())})
        print(report[-1], flush=True)
    (args.output_dir / 'validation.json').write_text(json.dumps(report, indent=2) + "\n")
    return not all(r["passed"] for r in report)


if __name__ == "__main__":
    raise SystemExit(main())
