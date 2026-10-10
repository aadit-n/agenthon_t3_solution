"""Hot operations, ahead-of-time compiled with Cython in the submission image."""
from copy import deepcopy
from heapq import heappop, heappush

import numpy as np
from abides_core.message import MessageBatch, WakeupMsg
from abides_markets.orders import LimitOrder, MarketOrder


class HeapQueue:
    """The identical tuple comparator as PriorityQueue, without synchronization."""
    def __init__(self):
        self.queue = []

    def put(self, value):
        heappush(self.queue, value)

    def get(self):
        return heappop(self.queue)

    def empty(self):
        return not self.queue


class BufferedLatency:
    """Vector draws from the *same* private RandomState stream, in the same order.

    Latency RNG is independent of all agent/oracle RNGs. Prefetching therefore
    changes neither their random choices nor event ordering. Normal, lognormal,
    uniform and Pareto scalar/vector sequences are checked by the test suite.
    """
    def __init__(self, original):
        self.original = original
        self.values = np.empty(0, dtype=np.int64)
        self.position = 0

    def get_latency(self, sender_id, recipient_id):
        if sender_id == recipient_id:
            return 0
        if self.position >= len(self.values):
            obj = self.original
            n = 4096
            if obj._model == "log_normal":
                values = obj.random_state.lognormal(obj._mu, obj._sigma, size=n)
            elif obj._model == "uniform":
                values = obj.random_state.uniform(obj._min_ns, obj._max_ns, size=n)
            elif obj._model == "pareto":
                base = obj._min_ns if obj._min_ns > 0 else 1.0
                values = base * (1.0 + obj.random_state.pareto(obj._alpha, size=n))
            else:
                return int(round(float(np.clip(obj._mean_ns, obj._min_ns, obj._max_ns))))
            self.values = np.rint(np.clip(values, obj._min_ns, obj._max_ns)).astype(np.int64)
            self.position = 0
        result = int(self.values[self.position])
        self.position += 1
        return result


def clone_order(self, memo=None):
    """Copy order state without running constructors; mutable tags stay isolated."""
    result = type(self).__new__(type(self))
    result.__dict__ = self.__dict__.copy()
    if self.tag is not None:
        result.tag = deepcopy(self.tag)
    return result


def copy_market_value(value, memo=None):
    # Skip generic deepcopy's registry/memo machinery for the two scalar order
    # records. Fall back for all other types rather than assuming immutability.
    if type(value) is LimitOrder or type(value) is MarketOrder:
        return clone_order(value, memo)
    return deepcopy(value, memo)


def fast_runner(self, agent_actions=None):
    """Exact kernel dispatch with cached callbacks and the original heap tuples."""
    if agent_actions is not None or self.gym_agents:
        raise ValueError("The Track 3 dispatcher supports scenario agents, not ABIDES-Gym")
    heap = self.messages.queue
    times = self.agent_current_times
    delays = self.agent_computation_delays
    wake = [agent.wakeup for agent in self.agents]
    receive = [agent.receive_message for agent in self.agents]
    seqmap = self._deliver_seq_by_key
    ledger = self._msg_ledger
    while heap and self.current_time and self.current_time <= self.stop_time:
        current, (sender, recipient, message) = heappop(heap)
        self.current_time = current
        self.ttl_messages += 1
        self.current_agent_additional_delay = 0
        if times[recipient] > current:
            heappush(heap, (times[recipient], (sender, recipient, message)))
            continue
        times[recipient] = current
        if isinstance(message, WakeupMsg):
            uid = message.message_id
            self._current_causal_uid = uid
            seqmap[(uid, recipient)] = self._deliver_seq
            self._deliver_seq += 1
            ledger.append({"message_id": uid, "src_id": recipient, "dst_id": recipient,
                           "t_send_ns": None, "t_recv_ns": current, "latency_ns": 0,
                           "msg_type": "AGENT_WAKEUP", "order_id": None, "causal_parent": None})
            result = wake[recipient](current)
            times[recipient] += delays[recipient] + self.current_agent_additional_delay
            if result is not None:
                return {"done": False, "result": result}
        else:
            batch = message.messages if isinstance(message, MessageBatch) else (message,)
            for child in batch:
                # Upstream adds the delay BEFORE receive_message, whereas wakeup
                # adds it AFTER. Preserve this subtle asymmetry and batch behavior.
                times[recipient] += delays[recipient] + self.current_agent_additional_delay
                uid = child.message_id
                self._current_causal_uid = uid
                seqmap[(uid, recipient)] = self._deliver_seq
                self._deliver_seq += 1
                receive[recipient](current, sender, child)
    return {"done": True, "result": None}


def order_for_log(self):
    # log_event snapshots every scalar immediately. No mutable order is retained.
    return self


_ORDER_TYPES = {"ORDER_SUBMITTED", "ORDER_ACCEPTED", "ORDER_EXECUTED",
                "ORDER_CANCELLED", "ORDER_REPLACED"}


def log_event(self, event_type, event="", append_summary_log=False, deepcopy_event=True):
    """Collect only the canonical exchange trace at the original log chokepoint."""
    if not self.log_events:
        return
    if event_type in _ORDER_TYPES:
        if isinstance(event, dict):
            side = event.get("side")
            row = (self.current_time, event.get("agent_id", self.id), event_type,
                   side.value if side is not None else None,
                   (event.get("fill_price") if event_type == "ORDER_EXECUTED"
                    else event.get("limit_price")) or 0,
                   event.get("quantity", 0), event.get("order_id", -1))
        else:
            row = (self.current_time, event.agent_id, event_type, event.side.value,
                   (event.fill_price if event_type == "ORDER_EXECUTED"
                    else getattr(event, "limit_price", 0)) or 0,
                   event.quantity, event.order_id)
        self._trace_orders.append(row)
    elif event_type in ("BEST_BID", "BEST_ASK"):
        parts = event.split(",")
        side = "BID" if event_type == "BEST_BID" else "ASK"
        if len(parts) == 3:
            self._trace_quotes[(self.current_time, side)] = (
                self.current_time, self.id, "QUOTE_UPDATE", side,
                int(parts[1]), int(parts[2]), -1)


def no_op(*args, **kwargs):
    return None
