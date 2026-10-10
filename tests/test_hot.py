"""Meaningful edge checks for RNG sequence preservation and mutable order copies."""
import unittest
from copy import deepcopy

import numpy as np
from abides_fork.config import ScenarioLatencyModel
from abides_markets.orders import LimitOrder, Side
from t3sim.hot import BufferedLatency, HeapQueue, clone_order
from t3sim.level import CachedPriceLevel
from abides_markets.price_level import PriceLevel


class HotTests(unittest.TestCase):
    def test_latency_matches_scalar_rng_across_refills(self):
        for model in ("uniform", "log_normal", "pareto", "deterministic"):
            for seed in (0, 42, 2**32 - 1):
                cfg = {"model": model, "params": {"mean_ns": 500, "sigma": .8,
                       "min_ns": 100, "max_ns": 10000, "alpha": 1.5}}
                a = ScenarioLatencyModel(3, cfg, np.random.RandomState(seed))
                b = BufferedLatency(ScenarioLatencyModel(3, cfg, np.random.RandomState(seed)))
                scalar = [a.get_latency(1, 2 if i % 13 else 1) for i in range(8300)]
                buffered = [b.get_latency(1, 2 if i % 13 else 1) for i in range(8300)]
                self.assertEqual(scalar, buffered, (model, seed))

    def test_clone_keeps_flags_and_isolates_mutable_tag(self):
        order = LimitOrder(1, 100, "ABM", 10, Side.BID, 1000,
                           is_hidden=True, is_price_to_comply=True, is_post_only=True,
                           insert_by_id=True, tag={"notes": ["a"]})
        order.fill_price = 999
        expected = deepcopy(order)
        actual = clone_order(order)
        self.assertEqual(expected, actual)
        actual.tag["notes"].append("b")
        self.assertEqual(order.tag, {"notes": ["a"]})

    def test_heap_uses_full_original_tie_comparator(self):
        import queue
        from abides_core.message import WakeupMsg
        values = [(5, (2, 3, WakeupMsg())), (5, (1, 3, WakeupMsg())),
                  (5, (1, 3, WakeupMsg())), (4, (2, 2, WakeupMsg()))]
        reference, optimized = queue.PriorityQueue(), HeapQueue()
        for value in reversed(values):
            reference.put(value)
            optimized.put(value)
        self.assertEqual([reference.get() for _ in values], [optimized.get() for _ in values])

    def test_cached_depth_matches_fifo_mutations(self):
        rng = np.random.RandomState(123)
        initial = LimitOrder(1, 1, "ABM", 10, Side.BID, 100)
        a, b = PriceLevel([(deepcopy(initial), {})]), CachedPriceLevel([(deepcopy(initial), {})])
        for step in range(2000):
            action = rng.randint(5)
            if action == 0 or a.is_empty:
                order = LimitOrder(2, step, "ABM", int(rng.randint(1, 100)), Side.BID, 100,
                                   is_hidden=bool(rng.randint(2)), insert_by_id=bool(rng.randint(2)))
                a.add_order(deepcopy(order)); b.add_order(deepcopy(order))
            elif action == 1:
                a.pop(); b.pop()
            elif action == 2:
                oid = a.peek()[0].order_id
                a.remove_order(oid); b.remove_order(oid)
            elif action == 3:
                visible = a.visible_orders
                if visible:
                    oid = visible[int(rng.randint(len(visible)))][0].order_id
                    qty = int(rng.randint(1, 100))
                    self.assertEqual(a.update_order_quantity(oid, qty), b.update_order_quantity(oid, qty))
            else:
                # The real matcher changes the front quantity directly on partial execution.
                a.peek()[0].quantity = max(1, a.peek()[0].quantity - 1)
                b.peek()[0].quantity = max(1, b.peek()[0].quantity - 1)
            self.assertEqual(a.total_quantity, b.total_quantity, step)
            self.assertEqual(a.is_empty, b.is_empty, step)
            self.assertEqual([o.order_id for o, _ in a.visible_orders],
                             [o.order_id for o, _ in b.visible_orders], step)

    def test_price_to_comply_falls_back_for_non_head_mutation(self):
        orders = [LimitOrder(1, i, "ABM", 10, Side.BID, 100,
                             is_price_to_comply=True) for i in range(3)]
        level = CachedPriceLevel([(o, {}) for o in orders])
        orders[2].quantity = 2
        self.assertEqual(level.total_quantity, 22)


if __name__ == "__main__":
    unittest.main()
