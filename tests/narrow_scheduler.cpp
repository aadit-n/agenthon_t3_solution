#define T3_STANDALONE
#include "core.hpp"
#include <cassert>
#include <iostream>
#include <random>
using namespace t3;
auto key(const CompactScheduled& e) {return std::make_tuple(e.time,e.source(),e.recipient(),e.id,e.slot);}
int main() {
    std::mt19937 rng(20261013);size_t compared=0;
    for(int mode=0;mode<4;++mode) {
        NarrowRadixQueue candidate;std::priority_queue<CompactScheduled> reference;
        i64 last=1612483200000000000LL;
        for(int i=0;i<1000000;++i) {
            if(reference.empty()||rng()%2) {
                i64 time=mode==0?last+i64(rng()%100000):mode==1?last+i64(rng()%8):mode==2?i64(rng()%1000)-500:last+i64(rng()%20);
                i64 id=mode==3&&i>=4096?i64(UINT32_MAX)-4099+i:i;
                CompactScheduled e{time,id,int32_t(rng()),int32_t(rng()),uint32_t(i)};
                candidate.push(e);reference.push(e);
            } else {
                auto a=candidate.top(),b=reference.top();assert(key(a)==key(b));++compared;
                last=b.time;candidate.pop();reference.pop();
            }
        }
        while(!reference.empty()) {assert(key(candidate.top())==key(reference.top()));++compared;candidate.pop();reference.pop();}
        assert(candidate.empty());if(mode<2) assert(!candidate.full_width());else assert(candidate.full_width());
    }
    for(i64 wide:{i64(-1),i64(INT64_MIN),i64(UINT32_MAX)+1,i64(INT64_MAX)}) {
        NarrowRadixQueue candidate;std::priority_queue<CompactScheduled> reference;
        for(int i=0;i<256;++i) {CompactScheduled e{i%17,i,INT32_MIN+i,INT32_MAX-i,uint32_t(i)};candidate.push(e);reference.push(e);}
        CompactScheduled e{8,wide,INT32_MIN,INT32_MAX,UINT32_MAX};candidate.push(e);reference.push(e);
        assert(candidate.full_width());
        while(!reference.empty()) {assert(key(candidate.top())==key(reference.top()));++compared;candidate.pop();reference.pop();}
    }
    NarrowRadixQueue extremes;
    for(i64 time:{i64(INT64_MAX),i64(INT64_MIN),i64(0),i64(-1),i64(1)}) extremes.push({time,42,INT32_MIN,INT32_MAX,0});
    for(i64 time:{i64(INT64_MIN),i64(-1),i64(0),i64(1),i64(INT64_MAX)}) {assert(extremes.top().time==time);extremes.pop();++compared;}
    std::cout<<"4000000 queue operations and "<<compared<<" exact pops; monotone/tied/backwards timestamps, signed routing, ID overflow/negative fallback and int64 timestamp extremes passed\n";
}
