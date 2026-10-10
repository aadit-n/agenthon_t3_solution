# distutils: language = c++
# cython: language_level=3, boundscheck=False, wraparound=False, cdivision=True
"""Exact native event dispatch, exchange, agents and columnar trace collection.

Only the reference oracle and RandomState distributions cross into Python.
Message timestamps retain SEND-time arrival metadata even if dispatch is delayed.
"""
from libcpp.vector cimport vector
from libcpp cimport bool as cppbool
from libc.stdint cimport int64_t, int32_t, int8_t
from libc.math cimport nearbyint
from cpython.pycapsule cimport PyCapsule_GetPointer
import numpy as np
import pyarrow as pa

cdef extern from "native_types.hpp" namespace "t3":
    cdef cppclass NoiseRandom:
        NoiseRandom()
        void bind(void*, cppbool, double) noexcept
        double normal(double, double) noexcept
        long long randint(long long) noexcept
    cdef cppclass Order:
        Order()
        long long id, price, quantity, fill_price
        int agent, side
    cdef cppclass Msg:
        Msg()
        long long id, send, recv, latency, parent, bid, ask
        int src, dst, kind
        cppbool closed
        Order order
    cdef cppclass TraceRow:
        long long time, price, size, order_id, rank
        int agent, kind, side
    cdef cppclass LedgerRow:
        long long id, send, recv, latency, parent, order_id
        int src, dst, kind
    cdef cppclass AgentState:
        long long time, delay, interval, reference, size, threshold, offset
        int type, depth, spread, lookback
        double mean, stddev, sigma_n
        cppbool first, hours, closed, awaiting, log_orders
        vector[double] mids
    cdef cppclass Book:
        long long best_price(int) noexcept
        long long best_size(int) noexcept
        cppbool cross(const Order&) noexcept
        Order front(int) except +
        void enter(const Order&) except +
        Order take(int, long long) except +
        Order cancel(const Order&) except +
    cdef cppclass Core:
        Core(int) except +
        vector[AgentState] agents
        vector[int] subscriptions
        vector[TraceRow] trace
        vector[LedgerRow] ledger
        Book book
        long long now, stop, open, close, next_msg, next_order, causal
        long long ack_delay, compute_delay
        int stp
        Msg make(int) noexcept
        void wake(int, long long) except +
        void enqueue(const Msg&) except +
        void delivered(const Msg&) except +
        cppbool empty() noexcept
        Msg pop() except +
        void postpone(const Msg&, long long) except +
        void log(int, int, const Order&) except +
        void quotes() except +
        vector[Order] open_orders(int) except +
        void executed(int, const Order&) except +
        void cancelled(int, long long) except +
        void remember(int, const Order&) except +
        void finish() except +

cdef enum:
    WAKE=0
    CLOSE_REQ=1
    HOURS_REQ=2
    HOURS=3
    QUERY=4
    SPREAD=5
    LIMIT=6
    CANCEL=7
    ACCEPTED=8
    EXECUTED=9
    CANCELLED=10
    CLOSED=11
    CLOSE_PRICE=12
    SUBMITTED=0
    ACCEPT=1
    EXEC=2
    CANCEL_EVENT=3

cdef class NativeSimulation:
    cdef Core* c
    cdef vector[NoiseRandom] noise
    cdef object oracle, latency, rngs, latency_rng, latency_array
    cdef long long[::1] latency_values
    cdef Py_ssize_t latency_position
    cdef int latency_kind
    cdef long long fixed_latency
    cdef double latency_mu, latency_sigma, latency_min, latency_max, latency_alpha

    def __cinit__(self):
        self.c=NULL

    def __dealloc__(self):
        if self.c!=NULL:
            del self.c

    def __init__(self, config):
        agents=config['agents']
        self.c=new Core(len(agents))
        cdef Core* c=self.c
        c.now=config['start_time']
        c.stop=config['stop_time']
        c.open=agents[0].mkt_open
        c.close=agents[0].mkt_close
        c.ack_delay=agents[0].pipeline_delay
        c.compute_delay=agents[0].computation_delay
        c.stp=1 if agents[0].stp_policy=='cancel_oldest' else (2 if agents[0].stp_policy else 0)
        self.oracle=config['custom_properties']['oracle']
        self.rngs=[a.random_state for a in agents]
        self.noise.resize(len(agents))
        cdef int i
        cdef AgentState* a
        for i in range(len(agents)):
            a=&c.agents[i]
            a.time=c.now
            a.delay=config['default_computation_delay']
            if i==0:
                continue
            original=agents[i]
            a.interval=original.interval_ns
            a.log_orders=original.log_orders
            a.type={'NoiseTrader':1,'MarketMaker':2,'ValueTrader':3,'MomentumTrader':4}[original.type]
            if a.type==1:
                state=original.random_state.get_state()
                self.noise[i].bind(PyCapsule_GetPointer(original.random_state._bit_generator.capsule,b'BitGenerator'),state[3],state[4])
            a.reference=getattr(original,'reference_price',0)
            a.mean=getattr(original,'order_size_mean',0)
            a.stddev=getattr(original,'order_size_std',0)
            a.offset=getattr(original,'price_offset_ticks',0)
            a.depth=getattr(original,'depth_levels',0)
            a.spread=getattr(original,'spread_ticks',0)
            a.size=getattr(original,'size_per_level',0)
            a.threshold=getattr(original,'threshold_ticks',0)
            a.lookback=getattr(original,'lookback',0)
            a.sigma_n=getattr(original,'sigma_n',0)
        self.latency=config['agent_latency_model']
        self.latency_kind=0
        if hasattr(self.latency,'_model'):
            self.latency_kind={'uniform':1,'log_normal':2,'pareto':3}.get(self.latency._model,4)
            self.latency_rng=self.latency.random_state
            self.latency_mu=self.latency._mu
            self.latency_sigma=self.latency._sigma
            self.latency_min=self.latency._min_ns
            self.latency_max=self.latency._max_ns
            self.latency_alpha=self.latency._alpha
            if self.latency_kind==4:
                self.fixed_latency=int(round(float(np.clip(self.latency._mean_ns,self.latency_min,self.latency_max))))
        self.latency_array=np.empty(0,dtype=np.int64)
        self.latency_values=self.latency_array
        self.latency_position=0

    cdef long long latency_for(self, int sender, int recipient) except *:
        if sender==recipient:
            return 0
        if self.latency_kind==0:
            return int(self.latency.get_latency(sender_id=sender,recipient_id=recipient))
        if self.latency_kind==4:
            return self.fixed_latency
        if self.latency_position==self.latency_values.shape[0]:
            if self.latency_kind==1:
                values=self.latency_rng.uniform(self.latency_min,self.latency_max,size=4096)
            elif self.latency_kind==2:
                values=self.latency_rng.lognormal(self.latency_mu,self.latency_sigma,size=4096)
            else:
                values=(self.latency_min if self.latency_min>0 else 1.0)*(1.0+self.latency_rng.pareto(self.latency_alpha,size=4096))
            self.latency_array=np.rint(np.clip(values,self.latency_min,self.latency_max)).astype(np.int64)
            self.latency_values=self.latency_array
            self.latency_position=0
        cdef long long result=self.latency_values[self.latency_position]
        self.latency_position+=1
        return result

    cdef void send(self, int sender, int recipient, Msg msg) except *:
        msg.src=sender
        msg.dst=recipient
        msg.parent=self.c.causal
        msg.send=self.c.now+self.c.agents[sender].delay
        if sender==0 and msg.kind in (ACCEPTED,EXECUTED,CANCELLED):
            msg.send+=self.c.ack_delay
        msg.latency=self.latency_for(sender,recipient)
        msg.recv=msg.send+msg.latency
        self.c.enqueue(msg)

    cdef void simple(self, int sender, int recipient, int kind) except *:
        self.send(sender,recipient,self.c.make(kind))

    cdef void order_message(self, int sender, int kind, Order order) except *:
        cdef Msg msg=self.c.make(kind)
        msg.order=order
        self.send(sender,0 if sender else order.agent,msg)

    cdef void place(self, int agent, long long size, int side, long long price) except *:
        cdef Order o=Order()
        o.id=self.c.next_order
        self.c.next_order+=1
        o.agent=agent
        o.quantity=size
        o.side=side
        o.price=price
        self.c.remember(agent,o)
        self.order_message(agent,LIMIT,o)
        self.c.log(agent,SUBMITTED,o)

    cdef void act(self, int agent, long long bid, long long ask) except *:
        cdef AgentState* a=&self.c.agents[agent]
        cdef long long size, offset, anchor, mid_int, fundamental
        cdef int buy, level
        cdef double mid, past
        cdef vector[Order] orders
        cdef size_t j
        if a.type==1:
            size=max(1,<long long>nearbyint(self.noise[agent].normal(a.mean,a.stddev)))
            buy=self.noise[agent].randint(2)
            offset=self.noise[agent].randint(a.offset+1)
            if buy:
                anchor=ask if ask else (bid if bid else a.reference)
                self.place(agent,size,0,anchor+offset)
            else:
                anchor=bid if bid else (ask if ask else a.reference)
                self.place(agent,size,1,anchor-offset)
        elif a.type==2:
            # Nonnegative market prices make this division equal to Python //.
            mid_int=(bid+ask)//2 if bid and ask else a.reference
            orders=self.c.open_orders(agent)
            for j in range(orders.size()):
                self.order_message(agent,CANCEL,orders[j])
            for level in range(a.depth):
                self.place(agent,a.size,0,mid_int-a.spread//2-level)
                self.place(agent,a.size,1,mid_int+a.spread//2+level)
        else:
            if bid and ask:
                mid=(<double>bid+<double>ask)/2.0
            elif bid:
                mid=bid
            elif ask:
                mid=ask
            else:
                return
            size=max(1,round(a.mean))
            if a.type==3:
                fundamental=self.oracle.observe_price('ABM',self.c.now,self.rngs[agent],sigma_n=a.sigma_n)
                if mid<fundamental-a.threshold and ask:
                    self.place(agent,size,0,ask)
                elif mid>fundamental+a.threshold and bid:
                    self.place(agent,size,1,bid)
            else:
                a.mids.push_back(mid)
                if a.mids.size()><size_t>(a.lookback+1):
                    a.mids.erase(a.mids.begin())
                if a.mids.size()<=<size_t>a.lookback:
                    return
                past=a.mids[0]
                if mid>past+a.threshold and ask:
                    self.place(agent,size,0,ask)
                elif mid<past-a.threshold and bid:
                    self.place(agent,size,1,bid)

    cdef void limit(self, Order order) except *:
        if order.quantity<=0 or order.price<0:
            return
        cdef Order resting, filled
        while True:
            if self.c.book.cross(order):
                resting=self.c.book.front(order.side)
                if self.c.stp and resting.agent==order.agent:
                    if self.c.stp==1:
                        resting=self.c.book.cancel(resting)
                        self.order_message(0,CANCELLED,resting)
                        continue
                    self.order_message(0,CANCELLED,order)
                    break
                resting=self.c.book.take(order.side,order.quantity)
                filled=order
                filled.quantity=resting.quantity
                filled.fill_price=resting.fill_price
                order.quantity-=filled.quantity
                self.order_message(0,EXECUTED,resting)
                self.order_message(0,EXECUTED,filled)
                if not order.quantity:
                    break
            else:
                self.c.book.enter(order)
                self.order_message(0,ACCEPTED,order)
                break
        self.c.quotes()

    cdef void exchange(self, Msg msg) except *:
        cdef Core* c=self.c
        cdef Msg response
        cdef Order cancelled
        c.agents[0].delay=c.compute_delay
        if c.now>c.close and msg.kind not in (QUERY,):
            self.simple(0,msg.src,CLOSED)
            return
        if msg.kind==HOURS_REQ:
            c.agents[0].delay=0
            self.simple(0,msg.src,HOURS)
        elif msg.kind==CLOSE_REQ:
            c.subscriptions.push_back(msg.src)
        elif msg.kind==QUERY:
            response=c.make(SPREAD)
            response.bid=c.book.best_price(0)
            response.ask=c.book.best_price(1)
            response.closed=c.now>c.close
            self.send(0,msg.src,response)
        elif msg.kind==LIMIT:
            self.limit(msg.order)
        elif msg.kind==CANCEL:
            cancelled=c.book.cancel(msg.order)
            if cancelled.id>=0:
                self.order_message(0,CANCELLED,cancelled)
        else:
            raise RuntimeError('Unexpected native exchange message')

    cdef void receive(self, Msg msg) except *:
        cdef AgentState* a=&self.c.agents[msg.dst]
        if msg.kind==HOURS:
            if not a.hours:
                a.hours=True
                if self.c.open<self.c.now:
                    raise ValueError('set_wakeup() called with requested time not in future')
                self.c.wake(msg.dst,self.c.open)
        elif msg.kind==CLOSED:
            a.closed=True
        elif msg.kind==EXECUTED:
            self.c.log(msg.dst,EXEC,msg.order)
            self.c.executed(msg.dst,msg.order)
        elif msg.kind==ACCEPTED:
            self.c.log(msg.dst,ACCEPT,msg.order)
        elif msg.kind==CANCELLED:
            self.c.log(msg.dst,CANCEL_EVENT,msg.order)
            self.c.cancelled(msg.dst,msg.order.id)
        elif msg.kind==SPREAD:
            if msg.closed:
                a.closed=True
            if a.awaiting:
                if not a.closed:
                    self.act(msg.dst,msg.bid,msg.ask)
                a.awaiting=False
        elif msg.kind!=CLOSE_PRICE:
            raise RuntimeError('Unexpected native agent message')

    def run(self):
        cdef Core* c=self.c
        cdef Msg msg, broadcast
        cdef AgentState* a
        cdef int i
        cdef size_t j
        c.wake(0,c.close)
        for i in range(c.agents.size()):
            c.wake(i,c.now)
        while not c.empty() and c.now and c.now<=c.stop:
            msg=c.pop()
            a=&c.agents[msg.dst]
            if a.time>c.now:
                c.postpone(msg,a.time)
                continue
            a.time=c.now
            c.causal=msg.id
            if msg.kind==WAKE:
                msg.recv=c.now
                c.delivered(msg)
                if msg.dst==0:
                    if c.now>=c.close:
                        broadcast=c.make(CLOSE_PRICE)
                        for j in range(c.subscriptions.size()):
                            self.send(0,c.subscriptions[j],broadcast)
                else:
                    if a.first:
                        a.first=False
                        self.simple(msg.dst,0,CLOSE_REQ)
                    if not a.hours:
                        self.simple(msg.dst,0,HOURS_REQ)
                    if a.hours and not a.closed:
                        c.wake(msg.dst,c.now+a.interval)
                        self.simple(msg.dst,0,QUERY)
                        a.awaiting=True
                a.time+=a.delay
            else:
                a.time+=a.delay
                c.delivered(msg)
                if msg.dst==0:
                    self.exchange(msg)
                else:
                    self.receive(msg)
        c.finish()
        return self.tables()

    cdef object tables(self):
        from t3sim.schemas import TRACE_SCHEMA, MESSAGE_SCHEMA, numeric_array, string_array
        cdef Py_ssize_t n=self.c.trace.size(), m=self.c.ledger.size(), i
        trace=[np.empty(n,dtype=d) for d in ('int64','int32','int8','int8','int64','int64','int64')]
        cdef int64_t[::1] tt=trace[0], tp=trace[4], ts=trace[5], to=trace[6]
        cdef int32_t[::1] ta=trace[1]
        cdef int8_t[::1] tk=trace[2], td=trace[3]
        cdef TraceRow* r
        for i in range(n):
            r=&self.c.trace[i]
            tt[i]=r.time; ta[i]=r.agent; tk[i]=r.kind; td[i]=r.side
            tp[i]=r.price; ts[i]=r.size; to[i]=r.order_id
        arrays=[numeric_array(x) for x in trace]
        arrays[2]=pa.DictionaryArray.from_arrays(arrays[2],string_array([
            'ORDER_SUBMITTED','ORDER_ACCEPTED','ORDER_EXECUTED','ORDER_CANCELLED',
            'QUOTE_UPDATE','ORDER_FILLED','PARTIAL_FILL'])).cast(pa.string())
        arrays[3]=pa.DictionaryArray.from_arrays(arrays[3],string_array(['BID','ASK'])).cast(pa.string())
        trace_table=pa.Table.from_arrays(arrays,schema=TRACE_SCHEMA)
        ledger=[np.empty(m,dtype=d) for d in ('int64','int64','int64','int64','int32','int32','int64','int8','int64','int64')]
        cdef int64_t[::1] ls=ledger[0], lr=ledger[1], lt=ledger[2], ll=ledger[3]
        cdef int32_t[::1] lsrc=ledger[4], ldst=ledger[5]
        cdef int64_t[::1] lid=ledger[6], lo=ledger[8], lp=ledger[9]
        cdef int8_t[::1] lk=ledger[7]
        cdef LedgerRow* msg
        for i in range(m):
            msg=&self.c.ledger[i]
            ls[i]=i; lr[i]=msg.recv; lt[i]=msg.send; ll[i]=msg.latency
            lsrc[i]=msg.src; ldst[i]=msg.dst; lid[i]=msg.id
            lk[i]=msg.kind; lo[i]=msg.order_id; lp[i]=msg.parent
        arrays=[numeric_array(x,j in (2,8,9)) for j,x in enumerate(ledger)]
        arrays[7]=pa.DictionaryArray.from_arrays(arrays[7],string_array([
            'AGENT_WAKEUP','MarketClosePriceRequestMsg','MarketHoursRequestMsg','MarketHoursMsg',
            'QuerySpreadMsg','QuerySpreadResponseMsg','LimitOrderMsg','CancelOrderMsg',
            'OrderAcceptedMsg','OrderExecutedMsg','OrderCancelledMsg','MarketClosedMsg','MarketClosePriceMsg'])).cast(pa.string())
        return trace_table,pa.Table.from_arrays(arrays,schema=MESSAGE_SCHEMA)


def run_native(config):
    return NativeSimulation(config).run()


def noise_draws(random_state, int count, double mean, double stddev, long long upper):
    """Differential-test seam: same mixed draw order as a real NoiseTrader."""
    if upper<=0 or count<0:
        raise ValueError('positive upper and nonnegative count required')
    cdef NoiseRandom rng
    state=random_state.get_state()
    rng.bind(PyCapsule_GetPointer(random_state._bit_generator.capsule,b'BitGenerator'),state[3],state[4])
    normals=np.empty(count,dtype=np.float64)
    integers=np.empty((count,2),dtype=np.int64)
    cdef double[::1] n=normals
    cdef long long[:,::1] k=integers
    cdef int i
    for i in range(count):
        n[i]=rng.normal(mean,stddev)
        k[i,0]=rng.randint(2)
        k[i,1]=rng.randint(upper)
    return normals,integers
