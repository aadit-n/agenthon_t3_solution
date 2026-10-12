#define T3_STANDALONE
#define T3_FAST_TRACE_SORT
#define T3_COMPACT_TRACE
#define T3_MOMENTUM_RING
#include "core.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <deque>
using namespace t3;
auto key(const TraceRow& r) {return std::tie(r.time,r.order_id,r.agent,r.rank);}
int main() {
    std::mt19937_64 rng(66174);size_t rows=0,cases=0;
    for(size_t count:{0,1,2,3,4,7,8,16,17,32,65,128,257,1024}) for(int variant=0;variant<100;++variant) {
        std::vector<TraceRow> expected;
        for(size_t i=0;i<count;++i) {
            i64 order=variant%3==0?i64(i):variant%3==1?-1:i64(rng());
            expected.push_back({1612483200000000000LL,100001,3,order,i64(i),int32_t(rng()),int(i%7),int(i%2)});
        }
        if(variant%4==0) std::reverse(expected.begin(),expected.end());
        auto actual=expected;
        std::sort(expected.begin(),expected.end(),[](const TraceRow& a,const TraceRow& b){return key(a)<key(b);});
        sort_trace_group(actual,0,actual.size());
        for(size_t i=0;i<count;++i) {assert(key(actual[i])==key(expected[i]));assert(actual[i].kind==expected[i].kind&&actual[i].side==expected[i].side);}
        rows+=count;++cases;
    }
    Core core(2);core.now=200;Msg message=core.make(WAKE);message.src=message.dst=1;core.postpone(message,100);core.pop();assert(!core.trace_monotone);
    core.trace={{200,1,1,2,0,1,SUBMITTED,0},{100,1,1,1,1,1,SUBMITTED,0}};core.finish(false);assert(core.trace[0].time==100);
    size_t observations=0;
    for(int lookback:{0,1,2,5,32,129,4096,65536}) {
        AgentState state;state.lookback=lookback;std::deque<double> reference;
        for(int i=0;i<150000;++i) {
            double value=double(rng()%1000000)/2;
            state.record_mid(value);reference.push_back(value);if(reference.size()>size_t(lookback)+1) reference.pop_front();
            assert(state.mids.size()==reference.size());assert(state.mids[state.mids_head]==reference.front());++observations;
        }
    }
    std::cout<<cases<<" tied-sort cases, "<<rows<<" rows, backward-time fallback and "<<observations<<" momentum observations: exact\n";
}
