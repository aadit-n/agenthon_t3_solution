#include "simulation.hpp"
#include <cassert>
#include <iostream>
#include <thread>
using namespace t3;
int main() {
    // Verify draw order across buffer refills and frequent early shutdowns.
    for(int cycle=0;cycle<10000;++cycle) {
        i64 generated=0;
        LatencyStream stream([&]{return generated++;});
        int consume=cycle%4==0?0:cycle%4==1?1:cycle%4==2?2049:6145;
        for(int i=0;i<consume;++i) assert(stream.next()==i);
        if(cycle%4==0) std::this_thread::yield();
        stream.stop();stream.stop();
    }
    for(int cycle=0;cycle<2000;++cycle) {
        i64 latency=0;std::array<i64,3> draws{};
        ActionStream stream(3,{1,2},[&](int agent){i64 i=draws[agent]++;return NoiseAction{i,agent,int(i%2)};},[&]{return latency++;});
        int consume=cycle%4==0?0:cycle%4==1?1:cycle%4==2?513:2051;
        for(int i=0;i<consume;++i) {
            assert(stream.next_latency()==i);
            for(int agent:{2,1}) {auto a=stream.next_noise(agent);assert(a.size==i&&a.offset==agent&&a.buy==i%2);}
        }
        if(!consume) std::this_thread::yield();
    }
    std::cout<<"12000 latency/action worker lifetimes: exact refills, early shutdown and repeated stop passed\n";
}
