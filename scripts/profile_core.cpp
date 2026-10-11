#include "simulation.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
int main(int argc,char** argv) {
    t3::Json config;std::ifstream file(argv[1]);file>>config;
    int runs=argc>2?std::stoi(argv[2]):10;
    auto start=std::chrono::steady_clock::now();long long rows=0;
    for(int i=0;i<runs;++i) {
        t3::Simulation simulation(config);simulation.run();simulation.c.finish();
        rows+=simulation.c.trace.size();
    }
    std::cout<<rows<<" events in "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" seconds\n";
}
