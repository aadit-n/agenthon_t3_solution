#include "simulation.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <optional>
#include <atomic>
#include <mutex>
#include <thread>
#include <sys/resource.h>
#include <condition_variable>
#include <deque>
#include <unistd.h>
#include <cstdlib>
namespace fs=std::filesystem;
using namespace t3;
using OutJson=nlohmann::json;
using Clock=std::chrono::steady_clock;using Time=Clock::time_point;
double seconds(Time since) {return std::chrono::duration<double>(Clock::now()-since).count();}
std::string env(const char* name,const char* fallback) {const char* value=std::getenv(name);return value?value:fallback;}
std::string read_file_string(const fs::path& path) {
    std::FILE* f=std::fopen(path.c_str(),"rb");
    if(!f) throw std::runtime_error("cannot read "+path.string());
    std::fseek(f,0,SEEK_END);
    long size=std::ftell(f);
    std::fseek(f,0,SEEK_SET);
    std::string content(size>0?size:0,'\0');
    if(size>0) {
        size_t n=std::fread(content.data(),1,size,f);
        content.resize(n);
    }
    std::fclose(f);
    return content;
}
OutJson read_json(const fs::path& path) {
    std::string s=read_file_string(path);
    return OutJson::parse(s);
}
void write_json(const fs::path& path,const OutJson& value) {
    std::string s=value.dump()+'\n';
    std::FILE* f=std::fopen(path.c_str(),"wb");
    if(!f) throw std::runtime_error("cannot write "+path.string());
    if(std::fwrite(s.data(),1,s.size(),f)!=s.size()) {std::fclose(f);throw std::runtime_error("cannot write "+path.string());}
    std::fclose(f);
}
int64_t peak_memory() {rusage usage{};getrusage(RUSAGE_SELF,&usage);return int64_t(usage.ru_maxrss)*1024;}
#include "parquet_lite.hpp"
template<class Row,bool TRACE=false> class FileStream {
    std::mutex mutex;std::condition_variable ready;
    std::deque<std::vector<Row>> pending;
    bool ended=false;std::exception_ptr failure;
    std::string hash;double encoding=0;std::thread thread;
public:
    explicit FileStream(const fs::path& out,bool parallel=false):thread([this,out,parallel]() {
        try {
            litepq::Writer writer(out,TRACE?litepq::trace_schema():litepq::ledger_schema(),parallel);i64 first=0;
            while(true) {
                std::vector<Row> block;
                {std::unique_lock<std::mutex> lock(mutex);ready.wait(lock,[&]{return ended||!pending.empty();});
                 if(pending.empty()) break;block=std::move(pending.front());pending.pop_front();ready.notify_all();}
                Time start=Clock::now();
                if constexpr(TRACE) {
                    auto compare=[](const TraceRow& a,const TraceRow& b){
                        if (a.order_id != b.order_id) return a.order_id < b.order_id;
                        if (a.agent != b.agent) return a.agent < b.agent;
                        return a.rank < b.rank;
                    };
                    for(size_t begin=0;begin<block.size();) {
                        size_t end=begin+1;while(end<block.size()&&block[end].time==block[begin].time) ++end;
                        if(end-begin>1) {
                            if(end-begin==2) {
                                if(compare(block[begin+1], block[begin])) std::swap(block[begin], block[begin+1]);
                            } else std::sort(block.begin()+begin,block.begin()+end,compare);
                        }
                        begin=end;
                    }
                    writer.append_trace(block,0,block.size());
                } else litepq::ledger_block(writer,block,first);
                encoding+=seconds(start);first+=block.size();
            }
            Time start=Clock::now();hash=writer.close();encoding+=seconds(start);
        } catch(...) {std::lock_guard<std::mutex> lock(mutex);failure=std::current_exception();ready.notify_all();}
    }) {}
    ~FileStream() {if(thread.joinable()) {{std::lock_guard<std::mutex> lock(mutex);ended=true;}ready.notify_all();thread.join();}}
    void submit(std::vector<Row>&& block) {
        if(block.empty()) return;std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock,[&]{return pending.size()<2||failure;});if(failure) std::rethrow_exception(failure);
        pending.push_back(std::move(block));ready.notify_all();
    }
    std::pair<std::string,double> finish() {
        {std::lock_guard<std::mutex> lock(mutex);ended=true;}ready.notify_all();thread.join();if(failure) std::rethrow_exception(failure);
        return {hash,encoding};
    }
};
using LedgerStream=FileStream<LedgerRow>;
using TraceStream=FileStream<TraceRow,true>;
OutJson simulate(const fs::path& config,const fs::path& out,std::optional<uint32_t> seed={},std::optional<Time> since={},bool concurrent=true) {
    Time start=since.value_or(Clock::now());
    thread_local simdjson::dom::parser parser;
    simdjson::padded_string json_data=simdjson::padded_string::load(config.string());
    JsonView scenario(parser.parse(json_data).value());
    Simulation simulation(scenario,seed,concurrent);
    fs::path parent=out.parent_path();if(parent.empty()) parent=".";if(!fs::exists(parent)) fs::create_directories(parent);
    std::string scenario_id=scenario.at("scenario_id").get<std::string>();
    uint32_t actual_seed=seed.value_or(scenario.at("seed").get<uint32_t>());
    long double expected_trace=0;
    for(size_t i=1;i<simulation.c.agents.size();++i) {const auto& a=simulation.c.agents[i];expected_trace+=(1+static_cast<long double>(simulation.c.close-simulation.c.open)/a.interval)*(a.type==2?8*a.depth:6);}
    std::string trace_mode=env("T3SIM_STREAM_TRACE","auto");
    if(concurrent&&(trace_mode=="1"||(trace_mode=="auto"&&expected_trace>=65536))) {
        LedgerStream messages(parent/"message_trace.parquet",false);TraceStream trace(out,false);
        simulation.c.ledger_sink=[&](std::vector<LedgerRow>&& rows){messages.submit(std::move(rows));};
        simulation.c.trace_sink=[&](std::vector<TraceRow>&& rows){trace.submit(std::move(rows));};
        std::vector<LedgerRow>().swap(simulation.c.ledger);simulation.c.ledger.reserve(65536);
        std::vector<TraceRow>().swap(simulation.c.trace);simulation.c.trace.reserve(65536);
        double config_sec=seconds(start);Time phase=Clock::now();simulation.run();simulation.stop_latency();double loop_sec=seconds(phase);
        i64 events=simulation.c.trace_base+simulation.c.trace.size(),n_messages=simulation.c.total_messages;
        simulation.c.ledger_sink={};simulation.c.trace_sink={};
        messages.submit(std::move(simulation.c.ledger));trace.submit(std::move(simulation.c.trace));
        phase=Clock::now();auto th=trace.finish();auto mh=messages.finish();double output_sec=seconds(phase),elapsed=seconds(start);
        OutJson result={{"scenario_id",scenario_id},{"seed",actual_seed},{"n_events",events},{"n_messages",n_messages},
            {"wall_clock_sec",elapsed},{"events_per_sec",events/elapsed},{"trace_sha256",th.first},{"message_trace_sha256",mh.first},
            {"peak_memory_bytes",peak_memory()},{"gpu_seconds",0.},{"configuration_sec",config_sec},{"simulation_loop_sec",loop_sec},
            {"output_write_sec",output_sec},{"trace_encoding_sec",th.second},{"ledger_encoding_sec",mh.second},{"streamed_trace",true}};
        write_json(parent/"events.json",result);return result;
    }
    if(concurrent&&env("T3SIM_STREAM_LEDGER","0")=="1"&&expected_trace>=65536) {
        LedgerStream messages(parent/"message_trace.parquet",false);simulation.c.ledger_sink=[&](std::vector<LedgerRow>&& rows){messages.submit(std::move(rows));};
        std::vector<LedgerRow>().swap(simulation.c.ledger);simulation.c.ledger.reserve(65536);
        double config_sec=seconds(start);Time phase=Clock::now();simulation.run();simulation.stop_latency();double loop_sec=seconds(phase);
        simulation.c.ledger_sink={};messages.submit(std::move(simulation.c.ledger));
        phase=Clock::now();simulation.c.finish(false);double sort_sec=seconds(phase);phase=Clock::now();
        auto th=litepq::trace(simulation.c,out,concurrent);auto ledger=messages.finish();double output_sec=seconds(phase),elapsed=seconds(start);i64 events=simulation.c.trace.size();
        OutJson result={{"scenario_id",scenario_id},{"seed",actual_seed},{"n_events",events},{"n_messages",simulation.c.total_messages},
            {"wall_clock_sec",elapsed},{"events_per_sec",events/elapsed},{"trace_sha256",th},{"message_trace_sha256",ledger.first},
            {"peak_memory_bytes",peak_memory()},{"gpu_seconds",0.},{"configuration_sec",config_sec},{"simulation_loop_sec",loop_sec},
            {"sorting_sec",sort_sec},{"output_write_sec",output_sec},{"ledger_encoding_sec",ledger.second},{"streamed_ledger",true}};
        write_json(parent/"events.json",result);return result;
    }
    double config_sec=seconds(start);Time loop=Clock::now();simulation.run();simulation.stop_latency();double loop_sec=seconds(loop);
    struct Written {std::string hash;double elapsed;};Time pipeline=Clock::now();
    std::optional<std::future<Written>> ledger;
    if(concurrent&&env("T3SIM_LITE_PIPELINE","1")=="1") ledger=std::async(std::launch::async,[&]() {
        Time begin=Clock::now();auto hash=litepq::ledger(simulation.c,parent/"message_trace.parquet",false);return Written{hash,seconds(begin)};
    });
    Time phase=Clock::now();simulation.c.finish(false);double sort_sec=seconds(phase);phase=Clock::now();
    auto th=litepq::trace(simulation.c,out,false);double trace_sec=seconds(phase);Written messages;
    if(ledger) messages=ledger->get();else {phase=Clock::now();messages={litepq::ledger(simulation.c,parent/"message_trace.parquet",false),seconds(phase)};}
    double pipeline_sec=seconds(pipeline),elapsed=seconds(start);i64 events=simulation.c.trace.size();
    OutJson result={{"scenario_id",scenario_id},{"seed",actual_seed},{"n_events",events},{"n_messages",simulation.c.ledger.size()},
        {"wall_clock_sec",elapsed},{"events_per_sec",events/elapsed},{"trace_sha256",th},{"message_trace_sha256",messages.hash},
        {"peak_memory_bytes",peak_memory()},{"gpu_seconds",0.},{"configuration_sec",config_sec},{"simulation_loop_sec",loop_sec},
        {"sorting_sec",sort_sec},{"output_write_sec",pipeline_sec},{"trace_output_write_sec",trace_sec},{"ledger_output_write_sec",messages.elapsed}};
    write_json(parent/"events.json",result);return result;
}
int cpu_budget() {
    unsigned hardware=std::thread::hardware_concurrency();int budget=std::min(4,int(hardware?hardware:1));
    std::ifstream file("/sys/fs/cgroup/cpu.max");std::string quota;int period;
    if(file>>quota>>period&&quota!="max") budget=std::min(budget,std::max(1,std::stoi(quota)/period));return budget;
}
OutJson batch(const fs::path& input,const fs::path& output,int workers,Time start) {
    std::vector<fs::path> paths;for(const auto& file:fs::directory_iterator(input)) if(file.is_regular_file()&&file.path().extension()==".json") paths.push_back(file.path());
    std::sort(paths.begin(),paths.end());if(paths.empty()) throw std::invalid_argument("no scenario JSON files");
    for(const auto& p:paths) fs::create_directories(output / p.stem());
    workers=std::min({workers,cpu_budget(),int(paths.size())});std::vector<OutJson> results(paths.size());std::atomic<size_t> next{0};std::mutex mutex;std::exception_ptr failure;
    auto work=[&]() {
        try {while(true) {size_t i=next.fetch_add(1);if(i>=paths.size()) break;results[i]=simulate(paths[i],output/paths[i].stem()/"trace.parquet",{},{},workers==1);}}
        catch(...) {std::lock_guard<std::mutex> lock(mutex);if(!failure) failure=std::current_exception();}
    };
    std::vector<std::thread> threads;for(int i=1;i<workers;++i) threads.emplace_back(work);work();for(auto& thread:threads) thread.join();if(failure) std::rethrow_exception(failure);
    i64 total=0;OutJson per=OutJson::array();for(size_t i=0;i<paths.size();++i) {total+=results[i]["n_events"].get<i64>();per.push_back({{"sub",paths[i].stem().string()},{"n_events",results[i]["n_events"]},{"trace_sha256",results[i]["trace_sha256"]}});}
    double elapsed=seconds(start);OutJson result={{"n_scenarios",paths.size()},{"total_events",total},{"wall_clock_sec",elapsed},{"events_per_sec",total/elapsed},
        {"peak_memory_bytes",peak_memory()},{"gpu_seconds",0.},{"per_scenario",per}};fs::create_directories(output);write_json(output/"batch_events.json",result);return result;
}
void random_test(const fs::path& config,const fs::path& out) {
    OutJson j=read_json(config);Random rng(j.at("seed").get<uint32_t>());std::ofstream file(out,std::ios::binary);
    for(int i=0;i<j.at("count").get<int>();++i) {
        double data[7];data[0]=rng.normal(j.value("mean",11.),j.value("scale",2.));data[1]=double(rng.bounded(2));data[2]=double(rng.bounded(j.at("upper").get<uint64_t>()));
        data[3]=rng.lognormal(6.214608098422191,0.3);data[4]=rng.pareto(1.5);data[5]=rng.exponential(100);data[6]=rng.uniform(100,2000);file.write(reinterpret_cast<const char*>(data),sizeof(data));
    }
    if(!file) throw std::runtime_error("random test output failed");
}
int main(int argc,char** argv) {
    Time start=Clock::now();
    try {
        if(argc<2||std::string(argv[1])=="--help") {
            std::cout<<"Agenthon T3 native simulator v5\nsimulate --config FILE --out FILE [--seed N]\nsimulate-batch --batch-dir DIR --out-dir DIR [--workers 1..4]\n";
            return 0;
        }
        int arg_offset = 2;
        std::string command = argv[1];
        if (command != "simulate" && command != "simulate-batch" && command != "random-test") {
            std::string prog = fs::path(argv[0]).stem().string();
            if (prog == "simulate" || prog == "simulate-batch" || prog == "random-test") {
                command = prog;
                arg_offset = 1;
            }
        }
        std::map<std::string,std::string> args;
        for(int i=arg_offset;i<argc;i+=2) {
            if(i+1>=argc) throw std::invalid_argument("missing option value");
            args[argv[i]]=argv[i+1];
        }
        OutJson result;
        if(command=="simulate") {
            std::optional<uint32_t> seed;
            if(args.count("--seed")) {
                auto value=std::stoll(args.at("--seed"));
                if(value<0||uint64_t(value)>UINT32_MAX) throw std::invalid_argument("seed must be 0..4294967295");
                seed=uint32_t(value);
            }
            result=simulate(args.at("--config"),args.at("--out"),seed,start);
        } else if(command=="simulate-batch") {
            int workers=args.count("--workers")?std::stoi(args.at("--workers")):4;
            if(workers<1||workers>4) throw std::invalid_argument("workers must be 1..4");
            result=batch(args.at("--batch-dir"),args.at("--out-dir"),workers,start);
        } else if(command=="random-test") {
            random_test(args.at("--config"),args.at("--out"));
            return 0;
        } else if(command=="python" || command=="sh" || command=="bash") {
            execvp(argv[1], argv + 1);
            perror("execvp");
            return 1;
        } else throw std::invalid_argument("unknown command: "+command);
        std::cout<<result.dump()<<'\n';
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"t3sim: "<<error.what()<<'\n';
        return 1;
    }
}
