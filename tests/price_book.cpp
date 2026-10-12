#define T3_STANDALONE
#include "core.hpp"
#include "random.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <limits>
using namespace t3;
bool equal(const Order& a,const Order& b) {
    return std::tie(a.id,a.price,a.quantity,a.fill_price,a.agent,a.side)==std::tie(b.id,b.price,b.quantity,b.fill_price,b.agent,b.side);
}
void check(const LegacyBook& reference,const FlatPriceBook& candidate) {
    for(int side=0;side<2;++side) {assert(reference.best_price(side)==candidate.best_price(side));assert(reference.best_size(side)==candidate.best_size(side));}
}
int main() {
    size_t actions=0;
    for(int scenario=0;scenario<4;++scenario) {
        std::mt19937 rng(61357+scenario);LegacyBook reference;FlatPriceBook candidate;std::vector<Order> history;
        for(int i=0;i<150000;++i) {
            if(!history.empty()&&rng()%3==0) {
                Order request=history[rng()%history.size()];if(rng()%5==0) request.price+=1;if(rng()%9==0) request.side=1-request.side;
                assert(equal(reference.cancel(request),candidate.cancel(request)));
            } else {
                i64 price=100000+(i64)(rng()%(scenario==0?201:scenario==1?4097:scenario==2?65537:9))-100;
                Order order{(i64)history.size()*3+7,price,1+(i64)(rng()%100),0,int(rng()%9),int(rng()%2)};
                history.push_back(order);
                while(reference.cross(order)) {
                    assert(candidate.cross(order));assert(equal(reference.front(order.side),candidate.front(order.side)));
                    auto a=reference.take(order.side,order.quantity),b=candidate.take(order.side,order.quantity);assert(equal(a,b));
                    order.quantity-=a.quantity;if(!order.quantity) break;
                }
                if(order.quantity) {assert(!candidate.cross(order));reference.enter(order);candidate.enter(order);}
            }
            check(reference,candidate);++actions;
        }
    }
    // Force both tree transitions, cache collisions, recycled levels and int64 extrema.
    for(int side=0;side<2;++side) {
        LegacyBook reference;FlatPriceBook candidate;
        std::vector<Order> orders;
        for(int i=0;i<5000;++i) {
            i64 price=i==0?INT64_MIN:i==4999?INT64_MAX:i64(i)*256;
            Order order{i,price,3,0,i%9,side};orders.push_back(order);reference.enter(order);candidate.enter(order);check(reference,candidate);++actions;
        }
        for(int i=0;i<5000;i+=3) {assert(equal(reference.cancel(orders[i]),candidate.cancel(orders[i])));check(reference,candidate);++actions;}
        while(reference.best_size(side)) {assert(equal(reference.take(1-side,1),candidate.take(1-side,1)));check(reference,candidate);++actions;}
        for(auto order:orders) {reference.enter(order);candidate.enter(order);check(reference,candidate);++actions;}
    }
    size_t words=0;
    for(uint32_t seed:{0U,1U,123U,20261012U,UINT32_MAX}) {
        Random candidate(seed);std::mt19937 reference(seed);
        for(int i=0;i<1000000;++i) {assert(candidate.word()==reference());++words;}
    }
    std::mt19937 rng(25791);TombstoneOpenOrders open;std::map<i64,Order> expected;i64 id=0;
    for(int i=0;i<300000;++i) {
        if(expected.size()<512&&rng()%3) {
            Order order;order.id=id+=1+rng()%3;order.quantity=1+rng()%100;
            open.emplace(order.id,order);expected.emplace(order.id,order);
            open.emplace(order.id,order); // Existing IDs must stay unique.
        } else {
            i64 request=expected.empty()?0:std::next(expected.begin(),rng()%expected.size())->first;
            auto a=open.find(request);auto b=expected.find(request);assert((a==open.end())==(b==expected.end()));
            if(a!=open.end()) {assert(equal(a->second,b->second));open.erase(a);expected.erase(b);open.erase(request);}
        }
        assert(open.size()==expected.size());
        if(i%1000==0) {const auto& view=open;auto it=expected.begin();for(const auto& item:view) {assert(item.first==it->first&&equal(item.second,it->second));++it;}assert(it==expected.end());}
    }
    std::cout<<actions<<" price-book actions, "<<words<<" MT19937 words and 300000 open-order actions: exact\n";
}
