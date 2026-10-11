"""Generate synthetic compiler-training workloads; no references or answer data."""
import json
from pathlib import Path
import sys

out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
models=[{'model':'deterministic','params':{'mean_ns':0}},
        {'model':'uniform','params':{'min_ns':.5,'max_ns':2000.5}},
        {'model':'log_normal','params':{'mean_ns':500,'sigma':.5,'min_ns':0,'max_ns':1e7}},
        {'model':'pareto','params':{'alpha':1.5,'min_ns':100,'max_ns':1e7}},None]
for i in range(15):
    groups=[{'agent_type':'NoiseTrader','count':12+i%4*8,'params':{'arrival_rate_hz':100,'order_size_mean':10.5,'order_size_std':2,'price_offset_ticks':5}},
            {'agent_type':'MarketMaker','count':1+i%3,'params':{'depth_levels':1+i%4,'spread_ticks':2,'size_per_level':10,'rebalance_interval_ns':20_000_000}},
            {'agent_type':'ValueTrader','count':3+i%4,'params':{'arrival_rate_hz':50,'threshold_ticks':2,'order_size_mean':25}},
            {'agent_type':'MomentumTrader','count':2+i%3,'params':{'arrival_rate_hz':50,'threshold_ticks':2,'order_size_mean':15,'lookback':3+i%7}}]
    scenario={'scenario_id':f'compiler-training-{i}','schema_version':2,'seed':5171+i*29,'horizon_ns':6_000_000_000 if i>=10 else 2_000_000_000,
              'agent_configs':groups,'exchange_config':{'protocol_enforcement':True,'ack_delay_ns':i%3*500,'compute_delay_ns':i%4*500,
                 'stp_policy':('cancel_newest','cancel_oldest',None)[i%3]},
              'oracle_config':{'type':'mean_reverting','params':{'initial_price':100000,'kappa':.05,'sigma':.001,
                 'jump_intensity':i%3*5,'jump_sigma':10,'scheduled_jump':{'time_ns':1_123_456_789,'magnitude':-50}}},
              'latency_config':models[i%len(models)]}
    (out/f'{i:02}.json').write_text(json.dumps(scenario))
