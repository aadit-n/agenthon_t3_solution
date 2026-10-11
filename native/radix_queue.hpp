#pragma once
#include <array>
#include <limits>

namespace t3 {
// Exact monotone timestamp queue. Each radix bucket holds an unsorted vector;
// only equal timestamps use a heap for the sender/recipient/message-ID order.
// Unusual input that schedules into the past switches to the general heap.
class RadixQueue {
    std::array<std::vector<CompactScheduled>,65> buckets;
    uint64_t last=0,occupied=0;
    size_t count=0;
    bool general=false;
    std::priority_queue<CompactScheduled> fallback;
    static uint64_t key(i64 time) {return uint64_t(time)^UINT64_C(0x8000000000000000);}
    static unsigned index(uint64_t value) {return value?64-__builtin_clzll(value):0;}
    struct Tied {
        bool operator()(const CompactScheduled& a,const CompactScheduled& b) const {
            return a.routing()!=b.routing() ? a.routing()>b.routing() : a.id>b.id;
        }
    };
    void prepare() {
        if(!buckets[0].empty()) return;
        unsigned source=__builtin_ctzll(occupied)+1;
        auto& pending=buckets[source];uint64_t next=UINT64_MAX;
        for(const auto& event:pending) next=std::min(next,key(event.time));
        last=next;occupied&=~(UINT64_C(1)<<(source-1));
        for(const auto& event:pending) {
            unsigned dest=index(key(event.time)^last);buckets[dest].push_back(event);
            if(dest) occupied|=UINT64_C(1)<<(dest-1);
        }
        pending.clear();std::make_heap(buckets[0].begin(),buckets[0].end(),Tied{});
    }
public:
    bool empty() const {return count==0;}
    void push(const CompactScheduled& event) {
        uint64_t value=key(event.time);
        if(__builtin_expect(!general&&value<last, 0)) {
            for(auto& bucket:buckets) {for(const auto& old:bucket) fallback.push(old);bucket.clear();}
            general=true;occupied=0;
        }
        ++count;
        if(__builtin_expect(general, 0)) {fallback.push(event);return;}
        unsigned dest=index(value^last);auto& bucket=buckets[dest];bucket.push_back(event);
        if(dest) occupied|=UINT64_C(1)<<(dest-1);
        else std::push_heap(bucket.begin(),bucket.end(),Tied{});
    }
    const CompactScheduled& top() {
        if(__builtin_expect(general, 0)) return fallback.top();
        prepare();
        return buckets[0].front();
    }
    void pop() {
        --count;
        if(__builtin_expect(general, 0)) {fallback.pop();return;}
        auto& bucket=buckets[0];
        if(bucket.size()>1) std::pop_heap(bucket.begin(),bucket.end(),Tied{});
        bucket.pop_back();
    }
};
}
