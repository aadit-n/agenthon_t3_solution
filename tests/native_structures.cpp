#define T3_STANDALONE
#include "core.hpp"
#include <cassert>
#include <iostream>
#include <random>
using namespace t3;
bool equal(const Order& a,const Order& b) {
    return std::tie(a.id,a.price,a.quantity,a.fill_price,a.agent,a.side)==std::tie(b.id,b.price,b.quantity,b.fill_price,b.agent,b.side);
}
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
            const auto& a=heap.top();const auto& b=oracle.top();assert(std::tie(a.time,a.id,a.src,a.dst,a.slot)==std::tie(b.time,b.id,b.src,b.dst,b.slot));
            heap.pop();oracle.pop();
        }
    }
    while(!oracle.empty()) {
        const auto& a=heap.top();const auto& b=oracle.top();assert(std::tie(a.time,a.id,a.src,a.dst,a.slot)==std::tie(b.time,b.id,b.src,b.dst,b.slot));heap.pop();oracle.pop();
    }
    assert(heap.empty());std::cout<<"300000 differential book actions and 1000000 tied heap operations passed\n";
}
