#pragma once
// Native market state. No Python object, lock, or allocation per trace field.
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <list>
#include <map>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <functional>
#ifndef T3_STANDALONE
#include <numpy/random/bitgen.h>
#endif

namespace t3 {
using i64 = long long;
enum Kind { WAKE, CLOSE_REQ, HOURS_REQ, HOURS, QUERY, SPREAD, LIMIT, CANCEL,
            ACCEPTED, EXECUTED, CANCELLED, CLOSED, CLOSE_PRICE };
enum Event { SUBMITTED, ACCEPT, EXEC, CANCEL_EVENT, QUOTE, FILL, PARTIAL };
// Use NumPy's own MT19937 callback/state, with the legacy polar Gaussian and
// masked integer selection. In particular, Generator's newer distributions
// would consume different words and change the market path.
#ifndef T3_STANDALONE
class NoiseRandom {
    bitgen_t* generator=nullptr;
    bool cached=false;
    double spare=0;
public:
    void bind(void* capsule, bool has_spare, double value) {
        generator=static_cast<bitgen_t*>(capsule); cached=has_spare; spare=value;
    }
    double normal(double mean, double scale) {
        double gaussian;
        if (cached) { gaussian=spare; cached=false; spare=0; }
        else {
            double u,v,radius;
            do {
                u=2*generator->next_double(generator->state)-1;
                v=2*generator->next_double(generator->state)-1;
                radius=u*u+v*v;
            } while (radius==0 || radius>=1);
            double factor=std::sqrt((-2*std::log(radius))/radius);
            spare=u*factor; cached=true; gaussian=v*factor;
        }
        return mean+scale*gaussian;
    }
    i64 randint(i64 upper) {
        uint64_t maximum=static_cast<uint64_t>(upper-1);
        if (maximum==0) return 0;
        uint64_t mask=maximum;
        mask|=mask>>1; mask|=mask>>2; mask|=mask>>4;
        mask|=mask>>8; mask|=mask>>16; mask|=mask>>32;
        uint64_t sample;
        do {
            sample=(maximum<=UINT32_MAX?generator->next_uint32(generator->state):
                    generator->next_uint64(generator->state))&mask;
        } while (sample>maximum);
        return static_cast<i64>(sample);
    }
};
#endif
struct Order {
    i64 id=-1, price=0, quantity=0, fill_price=0;
    int agent=0, side=0;
};
struct Msg {
#ifdef T3_COMPACT_MESSAGES
    i64 id=0, send=-1, recv=0, parent=-1;
    int src=0, dst=0, kind=0;
    Order order;
    i64 delivery_latency() const {return kind==WAKE?0:recv-send;}
    void arrive(i64 latency) {recv=send+latency;}
    void spread(i64 bid,i64 ask,bool closed) {order.price=bid;order.quantity=ask;order.fill_price=closed;}
    i64 spread_bid() const {return order.price;} i64 spread_ask() const {return order.quantity;}
    bool spread_closed() const {return order.fill_price!=0;}
#else
    i64 id=0, send=-1, recv=0, latency=0, parent=-1, bid=0, ask=0;
    int src=0, dst=0, kind=0;
    bool closed=false;
    Order order;
    i64 delivery_latency() const {return latency;}
    void arrive(i64 value) {latency=value;recv=send+value;}
    void spread(i64 b,i64 a,bool c) {bid=b;ask=a;closed=c;}
    i64 spread_bid() const {return bid;} i64 spread_ask() const {return ask;}
    bool spread_closed() const {return closed;}
#endif
};
struct Scheduled {
    i64 time;
    Msg msg;
    bool operator<(const Scheduled& other) const {
        return std::tie(time,msg.src,msg.dst,msg.id) >
               std::tie(other.time,other.msg.src,other.msg.dst,other.msg.id);
    }
};
#ifdef T3_STANDALONE
struct CompactScheduled {
    i64 time=0,id=0;
#ifdef T3_PACKED_ROUTE
    uint64_t route=0;
#else
    int src,dst;
#endif
    uint32_t slot=0;
    CompactScheduled()=default;
    CompactScheduled(i64 t,i64 message,int sender,int recipient,uint32_t s):time(t),id(message),slot(s) {
#ifdef T3_PACKED_ROUTE
        route=(uint64_t(uint32_t(sender)^0x80000000U)<<32)|(uint32_t(recipient)^0x80000000U);
#else
        src=sender;dst=recipient;
#endif
    }
    uint64_t routing() const {
#ifdef T3_PACKED_ROUTE
        return route;
#else
        return (uint64_t(uint32_t(src)^0x80000000U)<<32)|(uint32_t(dst)^0x80000000U);
#endif
    }
    int source() const {
#ifdef T3_PACKED_ROUTE
        return int32_t(uint32_t(route>>32)^0x80000000U);
#else
        return src;
#endif
    }
    int recipient() const {
#ifdef T3_PACKED_ROUTE
        return int32_t(uint32_t(route)^0x80000000U);
#else
        return dst;
#endif
    }
    bool operator<(const CompactScheduled& other) const {
#ifdef T3_PACKED_ROUTE
        return std::tie(time,route,id)>std::tie(other.time,other.route,other.id);
#else
        return std::tie(time,src,dst,id)>std::tie(other.time,other.src,other.dst,other.id);
#endif
    }
};
#endif
struct TraceRow {
    i64 time, price, size, order_id, rank;
#ifdef T3_COMPACT_TRACE
    int agent;
    uint8_t kind,side;
    TraceRow()=default;
    TraceRow(i64 t,i64 p,i64 s,i64 o,i64 r,int a,int k,int direction):
        time(t),price(p),size(s),order_id(o),rank(r),agent(a),kind(uint8_t(k)),side(uint8_t(direction)) {}
#else
    int agent, kind, side;
#endif
};
#ifdef T3_COMPACT_TRACE
static_assert(sizeof(TraceRow)==48,"compact trace row layout");
#endif
struct LedgerRow {
    i64 id, send, recv, latency, parent, order_id;
    int src, dst, kind;
};
#ifdef T3_FAST_TRACE_SORT
inline void sort_trace_group(std::vector<TraceRow>& rows,size_t begin,size_t end) {
    if(end-begin<2) return;
    auto compare=[](const TraceRow& a,const TraceRow& b) {return std::tie(a.order_id,a.agent,a.rank)<std::tie(b.order_id,b.agent,b.rank);};
    auto first=rows.begin()+begin,last=rows.begin()+end;
    if(end-begin==2) {if(compare(first[1],first[0])) std::iter_swap(first,first+1);}
    else if(!std::is_sorted(first,last,compare)) std::sort(first,last,compare);
}
#endif
}
#include "open_orders.hpp"
namespace t3 {
struct AgentState {
    i64 time=0, delay=50, interval=0, reference=0, size=0, threshold=0, offset=0;
    int type=0, depth=0, spread=0, lookback=0;
    double mean=0, stddev=0, sigma_n=0;
    uint64_t offset_mask=0;
    bool first=true, hours=false, closed=false, awaiting=false, log_orders=true;
#ifdef T3_TOMBSTONE_OPEN
    TombstoneOpenOrders open;
#elif defined(T3_FLAT_OPEN)
    FlatOpenOrders open;
#else
    std::map<i64,Order> open;
#endif
    std::vector<double> mids;
#ifdef T3_MOMENTUM_RING
    size_t mids_head=0;
    void record_mid(double mid) {
        if(mids.size()==size_t(lookback)+1) {
            mids[mids_head]=mid;if(++mids_head==mids.size()) mids_head=0;
        } else mids.push_back(mid);
    }
#endif
};
struct Level { std::list<i64> fifo; i64 quantity=0; };
struct Resting { Order order; std::list<i64>::iterator position; };
class LegacyBook {
    std::map<i64,Level> bids, asks;
    std::unordered_map<i64,Resting> resting;
public:
    i64 best_price(int side) const {
        const auto& levels=side==0?bids:asks;
        if (levels.empty()) return 0;
        return side==0?levels.rbegin()->first:levels.begin()->first;
    }
    i64 best_size(int side) const {
        const auto& levels=side==0?bids:asks;
        if (levels.empty()) return 0;
        return side==0?levels.rbegin()->second.quantity:levels.begin()->second.quantity;
    }
    bool cross(const Order& o) const {
        const auto& levels=o.side==0?asks:bids;
        if (levels.empty()) return false;
        return o.side==0?o.price>=levels.begin()->first:o.price<=levels.rbegin()->first;
    }
    Order front(int incoming_side) const {
        const auto& levels=incoming_side==0?asks:bids;
        auto it=incoming_side==0?levels.begin():std::prev(levels.end());
        return resting.at(it->second.fifo.front()).order;
    }
    void enter(const Order& o) {
        auto& level=(o.side==0?bids:asks)[o.price];
        level.fifo.push_back(o.id);
        level.quantity+=o.quantity;
        resting.emplace(o.id,Resting{o,std::prev(level.fifo.end())});
    }
    Order take(int incoming_side, i64 quantity) {
        auto& levels=incoming_side==0?asks:bids;
        auto level=incoming_side==0?levels.begin():std::prev(levels.end());
        auto item=resting.find(level->second.fifo.front());
        Order result=item->second.order;
        result.quantity=std::min(quantity,result.quantity);
        result.fill_price=result.price;
        item->second.order.quantity-=result.quantity;
        level->second.quantity-=result.quantity;
        if (!item->second.order.quantity) {
            level->second.fifo.pop_front();
            resting.erase(item);
            if (level->second.fifo.empty()) levels.erase(level);
        }
        return result;
    }
    Order cancel(const Order& request) {
        auto item=resting.find(request.id);
        if (item==resting.end() || item->second.order.side!=request.side ||
            item->second.order.price!=request.price) return Order{};
        Order result=item->second.order;
        auto& levels=result.side==0?bids:asks;
        auto level=levels.find(result.price);
        level->second.quantity-=result.quantity;
        level->second.fifo.erase(item->second.position);
        resting.erase(item);
        if (level->second.fifo.empty()) levels.erase(level);
        return result;
    }
};
}
#include "pooled_book.hpp"
#include "event_heap.hpp"
#include "radix_queue.hpp"
#include "recipient_queue.hpp"
#include "message_pool.hpp"
#include "flat_price_book.hpp"
namespace t3 {
#ifdef T3_FLAT_PRICE_BOOK
using Book=FlatPriceBook;
#elif defined(T3_POOL_BOOK)
using Book=PooledBook;
#else
using Book=LegacyBook;
#endif
class Core {
public:
#ifdef T3_STANDALONE
#ifdef T3_RECIPIENT_QUEUE
    RecipientQueue queue;
#elif defined(T3_RADIX_QUEUE)
    RadixQueue queue;
#elif defined(T3_FOUR_HEAP)
    EventHeap queue;
#else
    std::priority_queue<CompactScheduled> queue;
#endif
#ifdef T3_REF_MESSAGES
    MessagePool messages;
    uint32_t held_message=UINT32_MAX;
#else
    std::vector<Msg> messages;
#endif
    std::vector<uint32_t> free_slots;
    bool track_all_orders=false;
#else
    std::priority_queue<Scheduled> queue;
#endif
    std::vector<AgentState> agents;
    std::vector<int> subscriptions;
    std::vector<TraceRow> trace;
    std::function<void(std::vector<TraceRow>&&)> trace_sink;
    i64 trace_base=0;
    std::vector<LedgerRow> ledger;
    std::function<void(std::vector<LedgerRow>&&)> ledger_sink;
    i64 total_messages=0;
    Book book;
    i64 now=0, stop=0, open=0, close=0, next_msg=1, next_order=0, causal=-1;
#ifdef T3_FAST_TRACE_SORT
    bool trace_monotone=true;
#endif
    i64 ack_delay=0, compute_delay=0;
    int stp=0;
    i64 quote_time[2]={-1,-1};
    size_t quote_index[2]={0,0};
    Core(int count) : agents(count) {
        trace.reserve(65536); ledger.reserve(65536);
#ifdef T3_STANDALONE
        const char* option=std::getenv("T3SIM_TRACK_ALL_ORDERS");
        track_all_orders=option&&std::strcmp(option,"1")==0;
        messages.reserve(1024);free_slots.reserve(1024);
#endif
    }
    Msg make(int kind) { Msg m; m.id=next_msg++; m.kind=kind; return m; }
    void wake(int agent, i64 time) {
        Msg m=make(WAKE); m.src=agent; m.dst=agent; m.recv=time;
        postpone(m,time);
    }
    void enqueue(const Msg& m) { postpone(m,m.recv); }
    void delivered(const Msg& m) {
        ledger.push_back(LedgerRow{m.id,m.send,m.recv,m.delivery_latency(),m.parent,m.order.id,m.src,m.dst,m.kind});
        ++total_messages;
        if(ledger_sink&&ledger.size()==65536) {
            ledger_sink(std::move(ledger));ledger=std::vector<LedgerRow>();ledger.reserve(65536);
        }
    }
    bool empty() const { return queue.empty(); }
    void trace_boundary(i64 next_time) {
        if(trace_sink&&next_time!=now&&trace.size()>=16384) {
            i64 count=trace.size();trace_sink(std::move(trace));trace_base+=count;
            trace=std::vector<TraceRow>();trace.reserve(16384);
        }
    }
#ifdef T3_REF_MESSAGES
    Msg& pop() {
#else
    Msg pop() {
#endif
#ifdef T3_STANDALONE
#ifdef T3_RECIPIENT_QUEUE
        queue.before_pop(stop);
#endif
        CompactScheduled s=queue.top();queue.pop();
#ifdef T3_FAST_TRACE_SORT
        if(s.time<now) trace_monotone=false;
#endif
        trace_boundary(s.time);now=s.time;
#ifdef T3_REF_MESSAGES
        if(held_message!=UINT32_MAX) free_slots.push_back(held_message);
        held_message=s.slot;return messages[s.slot];
#else
        Msg m=messages[s.slot];free_slots.push_back(s.slot);return m;
#endif
#else
        Scheduled s=queue.top(); queue.pop(); now=s.time;
        return s.msg;
#endif
    }
    void recipient_ready(int agent,i64 time) {
#ifdef T3_RECIPIENT_QUEUE
        queue.ready(agent,time);
#endif
    }
    void postpone(const Msg& m, i64 time) {
#ifdef T3_STANDALONE
        uint32_t slot;
#ifdef T3_REF_MESSAGES
        if(held_message!=UINT32_MAX && &m==&messages[held_message]) {slot=held_message;held_message=UINT32_MAX;}
        else
#endif
        if (free_slots.empty()) { slot=messages.size();messages.push_back(m); }
        else { slot=free_slots.back();free_slots.pop_back();messages[slot]=m; }
        queue.push(CompactScheduled{time,m.id,m.src,m.dst,slot});
#else
        queue.push(Scheduled{time,m});
#endif
    }
    void log(int agent, int kind, const Order& o) {
        if (agents[agent].log_orders)
            trace.push_back(TraceRow{now,kind==EXEC?o.fill_price:o.price,o.quantity,
                                     o.id,trace_base+(i64)trace.size(),o.agent,kind,o.side});
    }
    void quotes() {
        for (int side=0; side<2; ++side) {
            if (!book.best_size(side)) continue;
            TraceRow row{now,book.best_price(side),book.best_size(side),-1,0,0,QUOTE,side};
            if (quote_time[side]==now) {
                row.rank=trace[quote_index[side]].rank;
                trace[quote_index[side]]=row;
            } else {
                row.rank=trace_base+(i64)trace.size(); quote_time[side]=now;
                quote_index[side]=trace.size(); trace.push_back(row);
            }
        }
    }
    std::vector<Order> open_orders(int agent) const {
        std::vector<Order> result; result.reserve(agents[agent].open.size());
        for (const auto& item:agents[agent].open) result.push_back(item.second);
        return result;
    }
    void executed(int agent, const Order& o) {
#ifdef T3_STANDALONE
        if(!track_all_orders&&agents[agent].type!=2) return;
#endif
        auto& orders=agents[agent].open;
        auto it=orders.find(o.id);
        if (it!=orders.end()) {
            if (o.quantity>=it->second.quantity) orders.erase(it);
            else it->second.quantity-=o.quantity;
        }
    }
    void cancelled(int agent, i64 id) { agents[agent].open.erase(id); }
    void remember(int agent, const Order& o) {
#ifdef T3_STANDALONE
        // Only MarketMaker decisions inspect known open orders; other agents'
        // callbacks log the incoming snapshot and never read this bookkeeping.
        if (!track_all_orders&&agents[agent].type!=2) return;
#endif
        agents[agent].open.emplace(o.id,o);
    }
    void finish(bool finalize_types=true) {
#ifdef T3_STANDALONE
        const char* option=std::getenv("T3SIM_FULL_SORT");
        bool full=option&&std::strcmp(option,"1")==0;
#ifdef T3_FAST_TRACE_SORT
        if(!trace_monotone) full=true;
#else
        if (!full&&!std::is_sorted(trace.begin(),trace.end(),[](const TraceRow& a,const TraceRow& b){return a.time<b.time;})) full=true;
#endif
        auto compare=[](const TraceRow& a,const TraceRow& b) {
            return std::tie(a.time,a.order_id,a.agent,a.rank)<std::tie(b.time,b.order_id,b.agent,b.rank);
        };
        if (full) std::sort(trace.begin(),trace.end(),compare);
        else {
            // Event dispatch already emits timestamps in order. Only equal-time
            // runs need canonical order-ID/agent/stable-rank sorting.
            for (size_t first=0;first<trace.size();) {
                size_t last=first+1;while (last<trace.size()&&trace[last].time==trace[first].time) ++last;
#ifdef T3_FAST_TRACE_SORT
                sort_trace_group(trace,first,last);
#else
                if (last-first>1) std::sort(trace.begin()+first,trace.begin()+last,compare);
#endif
                first=last;
            }
        }
        if(!finalize_types) return;
        std::vector<uint8_t> seen(size_t(next_order),0);
        for (auto it=trace.rbegin();it!=trace.rend();++it) if (it->kind==EXEC) {
            it->kind=seen[size_t(it->order_id)]?PARTIAL:FILL;seen[size_t(it->order_id)]=1;
        }
#else
        std::sort(trace.begin(),trace.end(),[](const TraceRow& a,const TraceRow& b) {
            return std::tie(a.time,a.order_id,a.agent,a.rank)<std::tie(b.time,b.order_id,b.agent,b.rank);
        });
        std::unordered_map<i64,size_t> final;
        for (size_t i=0;i<trace.size();++i) if (trace[i].kind==EXEC) final[trace[i].order_id]=i;
        for (size_t i=0;i<trace.size();++i) if (trace[i].kind==EXEC)
            trace[i].kind=final.at(trace[i].order_id)==i?FILL:PARTIAL;
#endif
    }
};
}
