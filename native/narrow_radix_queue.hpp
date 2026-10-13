#pragma once
#include <array>

namespace t3 {
// Store small message IDs in 24-byte queue entries. Public/output IDs stay
// int64. Negative or >uint32 IDs, and backwards timestamps, migrate every
// pending event to the full-width comparison heap before accepting the event.
class NarrowRadixQueue {
    struct Entry {
        i64 time;
        uint64_t route;
        uint32_t id,slot;
        explicit Entry(const CompactScheduled& e):time(e.time),route(e.routing()),id(uint32_t(e.id)),slot(e.slot) {}
        CompactScheduled full() const {
            return {time,i64(id),int32_t(uint32_t(route>>32)^0x80000000U),
                    int32_t(uint32_t(route)^0x80000000U),slot};
        }
    };
    static_assert(sizeof(Entry)==24,"compact scheduler entry");
    struct Tied {
        bool operator()(const Entry& a,const Entry& b) const {
            return std::tie(a.route,a.id)>std::tie(b.route,b.id);
        }
    };
    std::array<std::vector<Entry>,65> buckets;
    uint64_t last=0,occupied=0;
    size_t count=0;
    bool general=false;
    std::priority_queue<CompactScheduled> fallback;
    static uint64_t key(i64 time) {return uint64_t(time)^UINT64_C(0x8000000000000000);}
    static unsigned index(uint64_t value) {return value?64-__builtin_clzll(value):0;}
    void migrate() {
        for(auto& bucket:buckets) {for(const auto& e:bucket) fallback.push(e.full());bucket.clear();}
        occupied=0;general=true;
    }
    void prepare() {
        if(!buckets[0].empty()) return;
        unsigned source=__builtin_ctzll(occupied)+1;auto& pending=buckets[source];uint64_t next=UINT64_MAX;
        for(const auto& e:pending) next=std::min(next,key(e.time));
        last=next;occupied&=~(UINT64_C(1)<<(source-1));
        for(const auto& e:pending) {
            unsigned dest=index(key(e.time)^last);buckets[dest].push_back(e);
            if(dest) occupied|=UINT64_C(1)<<(dest-1);
        }
        pending.clear();std::make_heap(buckets[0].begin(),buckets[0].end(),Tied{});
    }
public:
    bool empty() const {return count==0;}
    bool full_width() const {return general;}
    void push(const CompactScheduled& event) {
        uint64_t value=key(event.time);
        if(!general&&(event.id<0||uint64_t(event.id)>UINT32_MAX||value<last)) migrate();
        ++count;if(general) {fallback.push(event);return;}
        unsigned dest=index(value^last);auto& bucket=buckets[dest];bucket.emplace_back(event);
        if(dest) occupied|=UINT64_C(1)<<(dest-1);else std::push_heap(bucket.begin(),bucket.end(),Tied{});
    }
    CompactScheduled top() {
        if(general) return fallback.top();prepare();return buckets[0].front().full();
    }
    void pop() {
        --count;if(general) {fallback.pop();return;}
        prepare();auto& bucket=buckets[0];std::pop_heap(bucket.begin(),bucket.end(),Tied{});bucket.pop_back();
    }
};
}
