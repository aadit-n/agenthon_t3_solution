#pragma once
namespace t3 {
// Hole-based 4-ary heap. The comparator includes every original tie-breaker;
// timestamp monotonicity alone does not imply monotonic sender/recipient IDs.
class EventHeap {
    std::vector<CompactScheduled> heap;
    static bool earlier(const CompactScheduled& a,const CompactScheduled& b) {
        if(a.time!=b.time) return a.time<b.time;
        if(a.routing()!=b.routing()) return a.routing()<b.routing();
        return a.id<b.id;
    }
public:
    EventHeap() {heap.reserve(1024);}
    bool empty() const {return heap.empty();}
    const CompactScheduled& top() const {return heap.front();}
    void push(const CompactScheduled& value) {
        size_t i=heap.size();heap.emplace_back();
        while(i) {size_t parent=(i-1)/4;if(!earlier(value,heap[parent])) break;heap[i]=heap[parent];i=parent;}
        heap[i]=value;
    }
    void pop() {
        CompactScheduled value=heap.back();heap.pop_back();size_t n=heap.size();if(!n) return;
        size_t i=0;
        while(i*4+1<n) {
            size_t child=i*4+1,limit=std::min(child+4,n),best=child;
            for(size_t j=child+1;j<limit;++j) if(earlier(heap[j],heap[best])) best=j;
            if(!earlier(heap[best],value)) break;
            heap[i]=heap[best];i=best;
        }
        heap[i]=value;
    }
};
}
