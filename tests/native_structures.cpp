#define T3_STANDALONE
#include "core.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include "random.hpp"
#include <cfenv>
#include <limits>
using namespace t3;
bool equal(const Order& a,const Order& b) {
    return std::tie(a.id,a.price,a.quantity,a.fill_price,a.agent,a.side)==std::tie(b.id,b.price,b.quantity,b.fill_price,b.agent,b.side);
}
auto key(const CompactScheduled& event) {return std::make_tuple(event.time,event.source(),event.recipient(),event.id,event.slot);}
int main() {
    std::mt19937 rng(61357);LegacyBook reference;PooledBook candidate;std::vector<Order> history;
    for(int i=0;i<300000;++i) {
        if(!history.empty()&&rng()%3==0) {
            Order request=history[rng()%history.size()];if(rng()%5==0) request.price+=1;
            assert(equal(reference.cancel(request),candidate.cancel(request)));
        } else {
            Order order{(i64)history.size()*3+7,100000+(i64)(rng()%201)-100,1+(i64)(rng()%100),0,int(rng()%9),int(rng()%2)};
            history.push_back(order);
            while(reference.cross(order)) {
                assert(candidate.cross(order));assert(equal(reference.front(order.side),candidate.front(order.side)));
                auto a=reference.take(order.side,order.quantity),b=candidate.take(order.side,order.quantity);assert(equal(a,b));
                order.quantity-=a.quantity;if(!order.quantity) break;
            }
            if(order.quantity) {assert(!candidate.cross(order));reference.enter(order);candidate.enter(order);}
        }
        for(int side=0;side<2;++side) {
            assert(reference.best_price(side)==candidate.best_price(side));assert(reference.best_size(side)==candidate.best_size(side));
        }
    }
    EventHeap heap;std::priority_queue<CompactScheduled> oracle;
    for(int i=0;i<1000000;++i) {
        if(oracle.empty()||rng()%3) {
            CompactScheduled item{(i64)(rng()%20),(i64)i,int(rng()%6),int(rng()%6),uint32_t(i)};heap.push(item);oracle.push(item);
        } else {
            const auto& a=heap.top();const auto& b=oracle.top();assert(key(a)==key(b));
            heap.pop();oracle.pop();
        }
    }
    while(!oracle.empty()) {
        const auto& a=heap.top();const auto& b=oracle.top();assert(key(a)==key(b));heap.pop();oracle.pop();
    }
    assert(heap.empty());
    EventHeap packed;std::priority_queue<Scheduled> independently_ordered;
    for(int i=0;i<100000;++i) {
        int src=int32_t(rng()),dst=int32_t(rng());i64 time=rng()%20;
        packed.push({time,i,src,dst,uint32_t(i)});Msg message;message.id=i;message.src=src;message.dst=dst;
        independently_ordered.push({time,message});
    }
    while(!independently_ordered.empty()) {
        auto a=packed.top();auto b=independently_ordered.top();
        assert(std::make_tuple(a.time,a.source(),a.recipient(),a.id)==std::make_tuple(b.time,b.msg.src,b.msg.dst,b.msg.id));
        packed.pop();independently_ordered.pop();
    }
    MessagePool stable_messages;Msg first;first.id=1511;stable_messages.push_back(first);Msg* address=&stable_messages[0];
    for(int i=0;i<10000;++i) {Msg m;m.id=i;stable_messages.push_back(m);}
    assert(address==&stable_messages[0]&&address->id==1511);
    for(int variant=0;variant<3;++variant) {
        RadixQueue radix;std::priority_queue<CompactScheduled> expected;i64 last=variant==2?INT64_MIN:1612483200000000000LL;
        for(int i=0;i<1000000;++i) {
            if(expected.empty()||rng()%2) {
                i64 time=variant==0?last+i64(rng()%100000):variant==1?last+i64(rng()%8):i64(rng()%1000)-500;
                CompactScheduled item{time,i,int(rng()%12),int(rng()%12),uint32_t(i)};radix.push(item);expected.push(item);
            } else {
                auto a=radix.top(),b=expected.top();assert(key(a)==key(b));
                last=b.time;radix.pop();expected.pop();
            }
        }
        while(!expected.empty()) {
            auto a=radix.top(),b=expected.top();assert(key(a)==key(b));radix.pop();expected.pop();
        }
        assert(radix.empty());
    }
    RadixQueue extremes;
    for(i64 time:{i64(INT64_MAX),i64(INT64_MIN),i64(0),i64(-1),i64(1)}) extremes.push({time,time,0,0,0});
    for(i64 time:{i64(INT64_MIN),i64(-1),i64(0),i64(1),i64(INT64_MAX)}) {assert(extremes.top().time==time);extremes.pop();}
    FlatOpenOrders flat;std::map<i64,Order> ordered;i64 id=0;
    for(int i=0;i<300000;++i) {
        if(ordered.size()<512&&rng()%3) {Order order;order.id=id+=1+rng()%3;order.quantity=1+rng()%100;flat.emplace(order.id,order);ordered.emplace(order.id,order);}
        else {
            i64 request=ordered.empty()?0:std::next(ordered.begin(),rng()%ordered.size())->first;
            auto a=flat.find(request);auto b=ordered.find(request);
            assert((a==flat.end())==(b==ordered.end()));
            if(a!=flat.end()) {assert(equal(a->second,b->second));flat.erase(a);ordered.erase(b);}
        }
        if(i%1000==0) {assert(flat.size()==ordered.size());auto b=ordered.begin();for(const auto& a:flat) {assert(a.first==b->first&&equal(a.second,b->second));++b;}}
    }
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}) {
        std::fesetround(mode);
        for(int i=0;i<100000;++i) {
            double value=i%2?double(int(rng()%200000)-100000)+0.5:double(int(rng()))/1000.;
            assert(rounded_integer(value)==int64_t(std::nearbyint(value)));
        }
    }
    std::fesetround(FE_TONEAREST);
    Random a(123),b(123);
    for(int i=0;i<100000;++i) {uint64_t maximum=i%2?UINT64_C(0xffffffffaabbccdd):rng();assert(a.bounded(maximum+1)==b.bounded_prepared(maximum,Random::mask_for(maximum)));}
    size_t deliveries=0;
    for(int trial=0;trial<1000;++trial) {
        RecipientQueue coalesced;std::priority_queue<CompactScheduled> original;std::vector<i64> ready(8,0);
        i64 original_now=0,candidate_now=0,stop=trial%3==0?0:trial%3==1?100:10000,id=0;
        for(int dst=0;dst<8;++dst) coalesced.ready(dst,0);
        auto add=[&](i64 time,int src,int dst) {CompactScheduled event{time,id++,src,dst,uint32_t(id)};original.push(event);coalesced.push(event);};
        for(int i=0;i<100;++i) add(rng()%100,int(rng()%8),int(rng()%8));
        while(true) {
            CompactScheduled x{},y{};bool has_original=false,has_candidate=false;
            while(!original.empty()&&original_now<=stop) {
                x=original.top();original.pop();original_now=x.time;
                if(ready[x.recipient()]>x.time) {x.time=ready[x.recipient()];original.push(x);continue;}
                has_original=true;break;
            }
            while(!coalesced.empty()&&candidate_now<=stop) {
                coalesced.before_pop(stop);y=coalesced.top();coalesced.pop();candidate_now=y.time;
                if(ready[y.recipient()]>y.time) {y.time=ready[y.recipient()];coalesced.push(y);continue;}
                has_candidate=true;break;
            }
            assert(has_original==has_candidate);if(!has_original) break;
            assert(key(x)==key(y));++deliveries;
            int dst=x.recipient();ready[dst]=x.time+i64(rng()%200);coalesced.ready(dst,ready[dst]);
            for(int i=0;i<int(rng()%4);++i) add(x.time+i64(rng()%1000),dst,int(rng()%8));
        }
    }
    std::cout<<"300000 book actions, 1000000 heap operations, 3000000 radix operations, 300000 open-order actions, 400000 rounding cases, 100000 prepared random draws and "<<deliveries<<" coalesced-scheduler deliveries across 1000 stop boundaries passed\n";
}
