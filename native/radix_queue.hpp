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
            return std::make_pair(a.routing(),a.id)>std::make_pair(b.routing(),b.id);
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
        if(!general&&value<last) {
            for(auto& bucket:buckets) {for(const auto& old:bucket) fallback.push(old);bucket.clear();}
            general=true;occupied=0;
        }
        ++count;
        if(general) {fallback.push(event);return;}
        unsigned dest=index(value^last);auto& bucket=buckets[dest];bucket.push_back(event);
        if(dest) occupied|=UINT64_C(1)<<(dest-1);
        else std::push_heap(bucket.begin(),bucket.end(),Tied{});
    }
    const CompactScheduled& top() {
        if(general) return fallback.top();prepare();return buckets[0].front();
    }
    void pop() {
        --count;if(general) {fallback.pop();return;}
        prepare();auto& bucket=buckets[0];std::pop_heap(bucket.begin(),bucket.end(),Tied{});bucket.pop_back();
    }
};
}
