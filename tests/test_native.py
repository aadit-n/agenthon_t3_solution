"""Compare native mixed random draws to the actual pinned NumPy implementation."""
import unittest
import numpy as np
from t3sim.native import noise_draws


class NativeTests(unittest.TestCase):
    def test_nullable_nanoseconds_survive_pandas_roundtrip(self):
        from t3sim.schemas import numeric_array,MESSAGE_SCHEMA
        import pyarrow as pa
        arrays=[]
        for field in MESSAGE_SCHEMA:
            if pa.types.is_string(field.type):
                arrays.append(pa.array(['AGENT_WAKEUP','LimitOrderMsg']))
            else:
                values=np.array([-1,1612524600000000123] if field.name=='t_send_ns' else [0,1],
                                dtype='int32' if pa.types.is_int32(field.type) else 'int64')
                arrays.append(numeric_array(values,field.name=='t_send_ns'))
        df=pa.Table.from_arrays(arrays,schema=MESSAGE_SCHEMA).to_pandas()
        self.assertEqual(str(df.t_send_ns.dtype),'Int64')
        self.assertEqual(int(df.t_send_ns.iloc[1]),1612524600000000123)

    def test_lean_config_preserves_seed_draw_order_and_oracle(self):
        from abides_fork.config import build_config
        from t3sim.config import build_native_config
        scenario={'seed':20261010,'scenario_id':'native-config-edge','horizon_ns':2000000000,
                  'exchange_config':{'symbol':'ABM','protocol_enforcement':True,
                                     'stp_policy':'cancel_oldest','ack_delay_ns':75,'compute_delay_ns':17},
                  'oracle_config':{'model':'mean_reverting','params':{'initial_price':100001,'kappa':.7,
                                      'sigma':.003,'jump_intensity':5.,'jump_sigma':10.,
                                      'scheduled_jump':{'time_ns':500000000,'magnitude':-5}}},
                  'latency_config':{'model':'pareto','params':{'min_ns':0,'max_ns':9000,'alpha':1.5}},
                  'agent_configs':[{'agent_type':kind,'count':2,'params':{'arrival_rate_hz':21}}
                                   for kind in ('NoiseTrader','MarketMaker','ValueTrader','MomentumTrader')]}
        reference=build_config(scenario)
        ref_global=np.random.get_state()
        native=build_native_config(scenario)
        np.testing.assert_array_equal(np.random.get_state()[1],ref_global[1])
        self.assertEqual(np.random.get_state()[2],ref_global[2])
        for a,b in zip(reference['agents'],native['agents']):
            np.testing.assert_array_equal(a.random_state.get_state()[1],b.random_state.get_state()[1])
            for key in ('interval_ns','order_size_mean','depth_levels','lookback','threshold_ticks'):
                if hasattr(a,key):
                    self.assertEqual(getattr(a,key),getattr(b,key))
        a=reference['custom_properties']['oracle']
        b=native['custom_properties']['oracle']
        self.assertEqual(a.r,b.r)
        self.assertEqual(a.megashocks,b.megashocks)
        for current in range(a.mkt_open,a.mkt_open+2000000000,17000000):
            initial=np.random.get_state()
            av=a.advance_fundamental_value_series(current,'ABM')
            after=np.random.get_state()
            np.random.set_state(initial)
            bv=b.advance_fundamental_value_series(current,'ABM')
            self.assertEqual(av,bv)
            self.assertEqual(a.r,b.r)
            self.assertEqual(a.megashocks,b.megashocks)
            np.testing.assert_array_equal(np.random.get_state()[1],after[1])
            self.assertEqual(np.random.get_state()[2],after[2])

    def test_interleaved_normal_and_integer_draws_match_randomstate_exactly(self):
        for seed in (0, 1234567, 2**32-1):
            for upper in (1, 2, 6, 17, 65537, 2**32+3):
                for cached in (False, True):
                    reference=np.random.RandomState(seed)
                    if cached:
                        reference.normal()
                    native=np.random.RandomState()
                    native.set_state(reference.get_state())
                    expected=np.array([(reference.normal(10.3,4.2), reference.randint(0,2),
                                        reference.randint(0,upper)) for _ in range(5000)])
                    normal, integers=noise_draws(native,5000,10.3,4.2,upper)
                    np.testing.assert_array_equal(normal,expected[:,0],err_msg=str((seed,upper,cached)))
                    np.testing.assert_array_equal(integers,expected[:,1:],err_msg=str((seed,upper,cached)))
                    # All MT19937 words and the cursor must match, across twist/refill boundaries.
                    np.testing.assert_array_equal(native.get_state()[1],reference.get_state()[1])
                    self.assertEqual(native.get_state()[2],reference.get_state()[2])


if __name__=='__main__':
    unittest.main()
