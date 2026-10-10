"""Lean configuration with the reference's exact seed-draw and oracle initialization order."""
from types import SimpleNamespace
import numpy as np


def random_state():
    return np.random.RandomState(seed=np.random.randint(0,2**32,dtype='uint64'))


def build_native_config(scenario):
    if not scenario.get('latency_config'):
        # Rare geographic-latency configurations retain the original adapter.
        from abides_fork.config import build_config
        return build_config(scenario)
    from t3sim.oracle import SparseMeanRevertingOracle
    np.random.seed(int(scenario['seed']))
    params=scenario['oracle_config'].get('params',{})
    exchange=scenario['exchange_config']
    date=int(np.datetime64('2021-02-05','ns').astype(np.int64))
    opened=date+34_200_000_000_000
    closed=opened+int(scenario['horizon_ns'])
    reference=int(params.get('initial_price',100000))
    kappa=float(params.get('kappa',0))
    rate=float(params.get('jump_intensity',0))
    jump=params.get('scheduled_jump')
    symbols={'ABM':{'r_bar':reference, 'kappa':kappa/1e9 if kappa>0 else 1.67e-16,
                    'sigma_s':0, 'fund_vol':float(params.get('sigma',5e-5)),
                    'megashock_lambda_a':rate/1e9 if rate>0 else 2.77778e-18,
                    'megashock_mean':float(params.get('jump_sigma',0)) or 1000.,
                    'megashock_var':50000, 'random_state':random_state(),
                    'scheduled_jumps':[{'time_ns':opened+int(jump['time_ns']),
                                        'magnitude':int(jump['magnitude']), '_consumed':False}] if jump else []}}
    oracle=SparseMeanRevertingOracle(opened,date+57_600_000_000_000,symbols)
    protocol=bool(exchange.get('protocol_enforcement',False))
    agents=[SimpleNamespace(mkt_open=opened,mkt_close=closed,random_state=random_state(),
                            pipeline_delay=int(exchange.get('ack_delay_ns',0)) if protocol else 0,
                            computation_delay=int(exchange.get('compute_delay_ns',0)) if protocol else 0,
                            stp_policy=str(exchange['stp_policy']) if protocol and exchange.get('stp_policy') else None)]
    for group in scenario['agent_configs']:
        kind=str(group['agent_type'])
        p=group.get('params',{})
        hz=float(p.get('arrival_rate_hz',1.))
        interval=int(p['rebalance_interval_ns']) if 'rebalance_interval_ns' in p else int(1e9/hz) if hz>0 else 1000000000
        fields={'type':kind, 'interval_ns':int(max(1,interval)), 'log_orders':True}
        if kind=='NoiseTrader':
            fields.update(order_size_mean=float(p.get('order_size_mean',10)),
                          order_size_std=float(p.get('order_size_std',2)),
                          price_offset_ticks=int(p.get('price_offset_ticks',5)),reference_price=reference)
            if fields['price_offset_ticks']<0 or fields['order_size_std']<0:
                raise ValueError('NoiseTrader requires nonnegative offset and normal scale')
        elif kind=='MarketMaker':
            fields.update(spread_ticks=int(max(2,p.get('spread_ticks',2))),
                          depth_levels=int(max(1,p.get('depth_levels',3))),
                          size_per_level=int(max(1,p.get('size_per_level',10))),reference_price=reference)
        elif kind=='ValueTrader':
            fields.update(order_size_mean=float(p.get('order_size_mean',25)),
                          threshold_ticks=int(p.get('threshold_ticks',2)),sigma_n=1000.)
        elif kind=='MomentumTrader':
            fields.update(order_size_mean=float(p.get('order_size_mean',15)),
                          threshold_ticks=int(p.get('threshold_ticks',2)),
                          lookback=int(max(1,p.get('lookback',5))))
        else:
            raise KeyError('unsupported agent_type: '+kind)
        for _ in range(int(group['count'])):
            agents.append(SimpleNamespace(**fields,random_state=random_state()))
    latency=scenario['latency_config']
    p=latency.get('params',{})
    mean=float(p.get('mean_ns',0))
    model=SimpleNamespace(_model=str(latency.get('model','deterministic')),
                          _mean_ns=mean,_mu=float(np.log(mean)) if mean>0 else 0.,
                          _sigma=float(p.get('sigma',0)),_min_ns=float(p.get('min_ns',0)),
                          _max_ns=float(p.get('max_ns',1e12)),_alpha=float(p.get('alpha',1.5)),
                          random_state=random_state())
    random_state()  # Kernel's otherwise unused draw remains in the global stream.
    return {'agents':agents, 'start_time':date, 'stop_time':closed+1000000000,
            'agent_latency_model':model, 'default_computation_delay':50,
            'custom_properties':{'oracle':oracle}}
