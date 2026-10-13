#include "simulation.hpp"
#include <cassert>
#include <iostream>
using namespace t3;
auto trace_key(const TraceRow& r) {return std::tie(r.time,r.price,r.size,r.order_id,r.rank,r.agent,r.kind,r.side);}
auto ledger_key(const LedgerRow& r) {return std::tie(r.id,r.send,r.recv,r.latency,r.parent,r.order_id,r.src,r.dst,r.kind);}
int main() {
    size_t cases=0,events=0,messages=0;bool modes[7]={};bool protocols[2]={};
    for(int latency=0;latency<5;++latency) for(int protocol=0;protocol<4;++protocol) for(int parallel=0;parallel<3;++parallel) {
        Json config={{"scenario_id","execution-mode-differential"},{"seed",uint32_t(517+latency*1009+protocol*123)},
            {"horizon_ns",200000000},
            {"exchange_config",{{"protocol_enforcement",true},{"stp_policy",protocol==0?Json():Json(protocol==1?"cancel_oldest":"cancel_newest")},
                {"ack_delay_ns",protocol==3?500:0},{"compute_delay_ns",protocol==0?250:50}}},
            {"oracle_config",{{"params",{{"initial_price",100000},{"kappa",.1},{"sigma",.004},{"jump_intensity",30.},
                {"jump_sigma",10.},{"scheduled_jump",{{"time_ns",123456789},{"magnitude",-50}}}}}}},
            {"agent_configs",Json::array({
                {{"agent_type","NoiseTrader"},{"count",4},{"params",{{"arrival_rate_hz",1000},{"order_size_mean",10.5},{"order_size_std",.3},{"price_offset_ticks",5}}}},
                {{"agent_type","MarketMaker"},{"count",2},{"params",{{"depth_levels",3},{"spread_ticks",2},{"size_per_level",10},{"rebalance_interval_ns",1000000}}}},
                {{"agent_type","ValueTrader"},{"count",2},{"params",{{"arrival_rate_hz",100},{"order_size_mean",25},{"threshold_ticks",2}}}},
                {{"agent_type","MomentumTrader"},{"count",1},{"params",{{"arrival_rate_hz",100},{"lookback",3},{"threshold_ticks",2}}}}
            })}};
        if(latency==0) config["latency_config"]={{"model","deterministic"},{"params",{{"mean_ns",0}}}};
        if(latency==1) config["latency_config"]={{"model","uniform"},{"params",{{"min_ns",.5},{"max_ns",2000.5}}}};
        if(latency==2) config["latency_config"]={{"model","log_normal"},{"params",{{"mean_ns",500.},{"sigma",.5},{"min_ns",0.},{"max_ns",1e7}}}};
        if(latency==3) config["latency_config"]={{"model","pareto"},{"params",{{"alpha",1.5},{"min_ns",100.},{"max_ns",1e7}}}};
        setenv("T3SIM_LATENCY_PREFETCH",parallel?"1":"off",1);
        setenv("T3SIM_NOISE_PREFETCH",parallel==2?"1":"off",1);
        setenv("T3SIM_EXECUTION_MODE","generic",1);
        Simulation original(config,parallel!=0);original.run();original.stop_latency();original.c.finish();
        unsetenv("T3SIM_EXECUTION_MODE");
        Simulation candidate(config,parallel!=0);candidate.run();candidate.stop_latency();candidate.c.finish();
        assert(original.c.trace.size()==candidate.c.trace.size());assert(original.c.ledger.size()==candidate.c.ledger.size());
        for(size_t i=0;i<original.c.trace.size();++i) assert(trace_key(original.c.trace[i])==trace_key(candidate.c.trace[i]));
        for(size_t i=0;i<original.c.ledger.size();++i) assert(ledger_key(original.c.ledger[i])==ledger_key(candidate.c.ledger[i]));
        assert(original.c.next_msg==candidate.c.next_msg&&original.c.next_order==candidate.c.next_order&&original.c.now==candidate.c.now);
        modes[candidate.execution_latency+1]=true;protocols[candidate.execution_plain_protocol]=true;
        events+=candidate.c.trace.size();messages+=candidate.c.ledger.size();++cases;
    }
    for(bool mode:modes) assert(mode);for(bool mode:protocols) assert(mode);
    std::cout<<cases<<" exact execution-policy cases, "<<events<<" exchange rows and "<<messages
        <<" ledger rows; generic/fixed/geographic/uniform/log-normal/pareto/prepared latency, both protocol paths and action prefetch passed\n";
}
