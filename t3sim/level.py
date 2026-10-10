"""Exact visible-depth caching for ordinary FIFO price levels.

The matcher mutates only the head order during a partial fill. Synchronizing that
single quantity avoids repeatedly summing the whole level. Explicit changes go
through add/remove/pop/update hooks. Price-to-comply pairs fall back to the
original sum because a hidden execution can change a non-head visible order.
"""
from abides_markets.price_level import PriceLevel as OriginalPriceLevel


class CachedPriceLevel(OriginalPriceLevel):
    def __init__(self, orders):
        self._visible_quantity = 0
        self._front = None
        self._front_quantity = 0
        self._uncached = False
        super().__init__(orders)

    def _sync_front(self):
        if self._front is not None:
            quantity = self._front.quantity
            self._visible_quantity += quantity - self._front_quantity
            self._front_quantity = quantity

    def _refresh_front(self):
        self._front = self.visible_orders[0][0] if self.visible_orders else None
        self._front_quantity = self._front.quantity if self._front is not None else 0

    def add_order(self, order, metadata=None):
        self._sync_front()
        super().add_order(order, metadata)
        if not order.is_hidden:
            self._visible_quantity += order.quantity
        if order.is_price_to_comply:
            self._uncached = True
        self._refresh_front()

    def remove_order(self, order_id):
        self._sync_front()
        result = super().remove_order(order_id)
        if result is not None and not result[0].is_hidden:
            self._visible_quantity -= result[0].quantity
        self._refresh_front()
        return result

    def pop(self):
        self._sync_front()
        result = super().pop()
        if not result[0].is_hidden:
            self._visible_quantity -= result[0].quantity
        self._refresh_front()
        return result

    def update_order_quantity(self, order_id, new_quantity):
        self._sync_front()
        old = next((o.quantity for o, _ in self.visible_orders if o.order_id == order_id), None)
        result = super().update_order_quantity(order_id, new_quantity)
        if result and old is not None:
            self._visible_quantity += new_quantity - old
        self._refresh_front()
        return result

    @property
    def total_quantity(self):
        if self._uncached:
            return sum(order.quantity for order, _ in self.visible_orders)
        self._sync_front()
        return self._visible_quantity
