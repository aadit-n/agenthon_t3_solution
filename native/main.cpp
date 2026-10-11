#include "simulation.hpp"
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <arrow/util/thread_pool.h>
#include <parquet/arrow/writer.h>
#include <parquet/arrow/schema.h>
#include <parquet/file_writer.h>
#include <openssl/evp.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <cstring>
#include <sstream>
#include <sys/resource.h>
#include <thread>

namespace fs=std::filesystem;
using namespace t3;
using Clock=std::chrono::steady_clock;
using Time=Clock::time_point;
double seconds(Time since) { return std::chrono::duration<double>(Clock::now()-since).count(); }
void checked(const arrow::Status& status) { if (!status.ok()) throw std::runtime_error(status.ToString()); }
template<class T> T checked(arrow::Result<T> result) {
    if (!result.ok()) throw std::runtime_error(result.status().ToString());return std::move(result).ValueUnsafe();
}
std::string env(const char* name,const char* fallback) { const char* value=std::getenv(name);return value?value:fallback; }
Json read_json(const fs::path& path) { std::ifstream file(path);if (!file) throw std::runtime_error("cannot read "+path.string());Json j;file>>j;return j; }
void write_json(const fs::path& path,const Json& value) { std::ofstream file(path);file<<value.dump(2)<<'\n';if (!file) throw std::runtime_error("cannot write "+path.string()); }
std::string sha256(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);if (!file) throw std::runtime_error("cannot hash "+path.string());
    using Context=std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)>;
    Context ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);if (!ctx||EVP_DigestInit_ex(ctx.get(),EVP_sha256(),nullptr)!=1) throw std::runtime_error("hash init");
    std::array<char,65536> bytes;
    while (file) { file.read(bytes.data(),bytes.size());if (EVP_DigestUpdate(ctx.get(),bytes.data(),size_t(file.gcount()))!=1) throw std::runtime_error("hash update"); }
    if (!file.eof()) throw std::runtime_error("hash read");
    unsigned char result[32];unsigned int size;
    if (EVP_DigestFinal_ex(ctx.get(),result,&size)!=1) throw std::runtime_error("hash finish");
    std::ostringstream output;output<<std::hex<<std::setfill('0');for (unsigned char b:result) output<<std::setw(2)<<int(b);return output.str();
}
int64_t peak_memory() { rusage usage{};getrusage(RUSAGE_SELF,&usage);return int64_t(usage.ru_maxrss)*1024; }

std::shared_ptr<arrow::Schema> schema(const std::vector<std::pair<std::string,std::shared_ptr<arrow::DataType>>>& fields,bool ledger) {
    arrow::FieldVector af;Json columns=Json::array();
    for (const auto& f:fields) {
        af.push_back(arrow::field(f.first,f.second));bool str=f.second->id()==arrow::Type::STRING||f.second->id()==arrow::Type::DICTIONARY;
        std::string type=str?"string":f.second->ToString();
        bool nullable=ledger&&(f.first=="t_send_ns"||f.first=="order_id"||f.first=="causal_parent");
        columns.push_back({{"name",f.first},{"field_name",f.first},{"pandas_type",str?"unicode":type},{"numpy_type",nullable?"Int64":type},{"metadata",nullptr}});
    }
    Json pandas={{"index_columns",Json::array()},{"column_indexes",Json::array()},{"columns",columns},
                 {"creator",{{"library","pyarrow"},{"version","15.0.2"}}},{"pandas_version","1.5.3"}};
    return arrow::schema(af,arrow::key_value_metadata({"pandas"},{pandas.dump()}));
}
template<class T,class F>
std::shared_ptr<arrow::Array> numbers(int64_t n,const std::shared_ptr<arrow::DataType>& type,F get,bool nullable=false) {
    std::shared_ptr<arrow::Buffer> buffer=checked(arrow::AllocateBuffer(n*sizeof(T)));
    T* values=reinterpret_cast<T*>(buffer->mutable_data());
    std::shared_ptr<arrow::Buffer> validity;int64_t nulls=0;
    if (nullable) { validity=checked(arrow::AllocateBitmap(n));std::memset(validity->mutable_data(),0,size_t((n+7)/8)); }
    for (int64_t i=0;i<n;++i) {
        values[i]=get(i);
        if (nullable) { if (values[i]>=0) validity->mutable_data()[i>>3]|=uint8_t(1<<(i&7));else ++nulls; }
    }
    return arrow::MakeArray(arrow::ArrayData::Make(type,n,{validity,buffer},nulls));
}
template<class F>
std::shared_ptr<arrow::Array> strings(int64_t n,const std::vector<std::string>& labels,F get) {
    int64_t length=0;for (int64_t i=0;i<n;++i) length+=labels[get(i)].size();
    std::shared_ptr<arrow::Buffer> offsets=checked(arrow::AllocateBuffer((n+1)*sizeof(int32_t)));
    std::shared_ptr<arrow::Buffer> data=checked(arrow::AllocateBuffer(length));
    auto* indices=reinterpret_cast<int32_t*>(offsets->mutable_data());char* output=reinterpret_cast<char*>(data->mutable_data());
    int32_t cursor=0;indices[0]=0;
    for (int64_t i=0;i<n;++i) { const auto& s=labels[get(i)];std::memcpy(output+cursor,s.data(),s.size());cursor+=s.size();indices[i+1]=cursor; }
    return arrow::MakeArray(arrow::ArrayData::Make(arrow::utf8(),n,{nullptr,offsets,data},0));
}
std::shared_ptr<arrow::Table> trace_table_legacy(const Core& c) {
    int64_t n=c.trace.size();arrow::ArrayVector arrays;
    arrays.push_back(numbers<i64>(n,arrow::int64(),[&](int64_t i){return c.trace[i].time;}));
    arrays.push_back(numbers<int32_t>(n,arrow::int32(),[&](int64_t i){return c.trace[i].agent;}));
    arrays.push_back(strings(n,{"ORDER_SUBMITTED","ORDER_ACCEPTED","ORDER_EXECUTED","ORDER_CANCELLED","QUOTE_UPDATE","ORDER_FILLED","PARTIAL_FILL"},[&](int64_t i){return c.trace[i].kind;}));
    arrays.push_back(strings(n,{"BID","ASK"},[&](int64_t i){return c.trace[i].side;}));
    arrays.push_back(numbers<i64>(n,arrow::int64(),[&](int64_t i){return c.trace[i].price;}));
    arrays.push_back(numbers<i64>(n,arrow::int64(),[&](int64_t i){return c.trace[i].size;}));
    arrays.push_back(numbers<i64>(n,arrow::int64(),[&](int64_t i){return c.trace[i].order_id;}));
    return arrow::Table::Make(schema({{"t_ns",arrow::int64()},{"agent_id",arrow::int32()},{"msg_type",arrow::utf8()},
       {"side",arrow::utf8()},{"price",arrow::int64()},{"size",arrow::int64()},{"order_id",arrow::int64()}},false),arrays);
}
std::shared_ptr<arrow::Table> ledger_table_legacy(const Core& c) {
    int64_t n=c.ledger.size();arrow::ArrayVector arrays;
    arrays.push_back(numbers<i64>(n,arrow::int64(),[](int64_t i){return i;}));
    arrays.push_back(numbers<i64>(n,arrow::int64(),[&](int64_t i){return c.ledger[i].recv;}));
    arrays.push_back(numbers<i64>(n,arrow::int64(),[&](int64_t i){return c.ledger[i].send;},true));
    arrays.push_back(numbers<i64>(n,arrow::int64(),[&](int64_t i){return c.ledger[i].latency;}));
    arrays.push_back(numbers<int32_t>(n,arrow::int32(),[&](int64_t i){return c.ledger[i].src;}));
    arrays.push_back(numbers<int32_t>(n,arrow::int32(),[&](int64_t i){return c.ledger[i].dst;}));
    arrays.push_back(numbers<i64>(n,arrow::int64(),[&](int64_t i){return c.ledger[i].id;}));
    arrays.push_back(strings(n,{"AGENT_WAKEUP","MarketClosePriceRequestMsg","MarketHoursRequestMsg","MarketHoursMsg","QuerySpreadMsg", "QuerySpreadResponseMsg","LimitOrderMsg","CancelOrderMsg","OrderAcceptedMsg","OrderExecutedMsg","OrderCancelledMsg","MarketClosedMsg","MarketClosePriceMsg"},[&](int64_t i){return c.ledger[i].kind;}));
    arrays.push_back(numbers<i64>(n,arrow::int64(),[&](int64_t i){return c.ledger[i].order_id;},true));
    arrays.push_back(numbers<i64>(n,arrow::int64(),[&](int64_t i){return c.ledger[i].parent;},true));
    return arrow::Table::Make(schema({{"seq",arrow::int64()},{"t_recv_ns",arrow::int64()},{"t_send_ns",arrow::int64()},
        {"latency_ns",arrow::int64()},{"src_id",arrow::int32()},{"dst_id",arrow::int32()},{"message_id",arrow::int64()},
        {"msg_type",arrow::utf8()},{"order_id",arrow::int64()},{"causal_parent",arrow::int64()}},true),arrays);
}
template<class T> struct NumericColumn {
    int64_t n,nulls=0;
    std::shared_ptr<arrow::Buffer> data,bitmap;
    T* values;
    NumericColumn(int64_t count,bool nullable=false):n(count),data(checked(arrow::AllocateBuffer(count*sizeof(T)))) {
        values=reinterpret_cast<T*>(data->mutable_data());
        if (nullable) { bitmap=checked(arrow::AllocateBitmap(n));std::memset(bitmap->mutable_data(),0,size_t((n+7)/8)); }
    }
    void put(int64_t i,T value) {
        values[i]=value;
        if (bitmap) { if (value>=0) bitmap->mutable_data()[i>>3]|=uint8_t(1<<(i&7));else ++nulls; }
    }
    std::shared_ptr<arrow::Array> finish() {
        auto type=sizeof(T)==8?arrow::int64():arrow::int32();
        return arrow::MakeArray(arrow::ArrayData::Make(type,n,{bitmap,data},nulls));
    }
};
struct StringColumn {
    int64_t n;int32_t cursor=0;
    std::vector<std::string> labels;
    std::shared_ptr<arrow::Buffer> offsets,data;
    int32_t* indices;char* bytes;
    bool dictionary;
    int8_t* codes=nullptr;
    StringColumn(int64_t count,std::vector<std::string> names):n(count),labels(std::move(names)) {
        std::string mode=env("T3SIM_STRINGS","auto");
        dictionary=mode=="dictionary"||(mode=="auto"&&(n>=262144||env("T3SIM_WRITER","arrow")=="pages"));
        if (dictionary) { data=checked(arrow::AllocateBuffer(n));codes=reinterpret_cast<int8_t*>(data->mutable_data());return; }
        size_t maximum=0;for (const auto& s:labels) maximum=std::max(maximum,s.size());
        offsets=checked(arrow::AllocateBuffer((n+1)*sizeof(int32_t)));data=checked(arrow::AllocateBuffer(n*maximum));
        indices=reinterpret_cast<int32_t*>(offsets->mutable_data());bytes=reinterpret_cast<char*>(data->mutable_data());indices[0]=0;
    }
    void put(int64_t i,int code) {
        if (dictionary) { codes[i]=int8_t(code);return; }
        const auto& s=labels[code];std::memcpy(bytes+cursor,s.data(),s.size());cursor+=s.size();indices[i+1]=cursor;
    }
    std::shared_ptr<arrow::Array> finish() {
        if (dictionary) {
            auto index=arrow::MakeArray(arrow::ArrayData::Make(arrow::int8(),n,{nullptr,data},0));
            auto names=strings(labels.size(),labels,[](int64_t i){return i;});
            return checked(arrow::DictionaryArray::FromArrays(arrow::dictionary(arrow::int8(),arrow::utf8()),index,names));
        }
        return arrow::MakeArray(arrow::ArrayData::Make(arrow::utf8(),n,{nullptr,offsets,arrow::SliceBuffer(data,0,cursor)},0));
    }
};
std::shared_ptr<arrow::DataType> string_type(int64_t n) {
    std::string mode=env("T3SIM_STRINGS","auto");
    return mode=="dictionary"||(mode=="auto"&&(n>=262144||env("T3SIM_WRITER","arrow")=="pages"))?arrow::dictionary(arrow::int8(),arrow::utf8()):arrow::utf8();
}
std::shared_ptr<arrow::Table> trace_table(const Core& c) {
    if (env("T3SIM_COLUMN_ASSEMBLY","one-pass")=="legacy") return trace_table_legacy(c);
    int64_t n=c.trace.size();NumericColumn<i64> time(n),price(n),size(n),id(n);NumericColumn<int32_t> agent(n);
    StringColumn kind(n,{"ORDER_SUBMITTED","ORDER_ACCEPTED","ORDER_EXECUTED","ORDER_CANCELLED","QUOTE_UPDATE","ORDER_FILLED","PARTIAL_FILL"}),side(n,{"BID","ASK"});
    for (int64_t i=0;i<n;++i) {
        const auto& row=c.trace[i];time.put(i,row.time);price.put(i,row.price);size.put(i,row.size);id.put(i,row.order_id);
        agent.put(i,row.agent);kind.put(i,row.kind);side.put(i,row.side);
    }
    return arrow::Table::Make(schema({{"t_ns",arrow::int64()},{"agent_id",arrow::int32()},{"msg_type",string_type(n)},
       {"side",string_type(n)},{"price",arrow::int64()},{"size",arrow::int64()},{"order_id",arrow::int64()}},false),
       arrow::ArrayVector{time.finish(),agent.finish(),kind.finish(),side.finish(),price.finish(),size.finish(),id.finish()});
}
std::shared_ptr<arrow::Table> ledger_table(const Core& c) {
    if (env("T3SIM_COLUMN_ASSEMBLY","one-pass")=="legacy") return ledger_table_legacy(c);
    int64_t n=c.ledger.size();NumericColumn<i64> seq(n),recv(n),send(n,true),latency(n),id(n),order(n,true),parent(n,true);
    NumericColumn<int32_t> src(n),dst(n);
    StringColumn kind(n,{"AGENT_WAKEUP","MarketClosePriceRequestMsg","MarketHoursRequestMsg","MarketHoursMsg","QuerySpreadMsg", "QuerySpreadResponseMsg","LimitOrderMsg","CancelOrderMsg","OrderAcceptedMsg","OrderExecutedMsg","OrderCancelledMsg","MarketClosedMsg","MarketClosePriceMsg"});
    for (int64_t i=0;i<n;++i) {
        const auto& row=c.ledger[i];seq.put(i,i);recv.put(i,row.recv);send.put(i,row.send);latency.put(i,row.latency);
        src.put(i,row.src);dst.put(i,row.dst);id.put(i,row.id);kind.put(i,row.kind);order.put(i,row.order_id);parent.put(i,row.parent);
    }
    return arrow::Table::Make(schema({{"seq",arrow::int64()},{"t_recv_ns",arrow::int64()},{"t_send_ns",arrow::int64()},
        {"latency_ns",arrow::int64()},{"src_id",arrow::int32()},{"dst_id",arrow::int32()},{"message_id",arrow::int64()},
        {"msg_type",string_type(n)},{"order_id",arrow::int64()},{"causal_parent",arrow::int64()}},true),
        arrow::ArrayVector{seq.finish(),recv.finish(),send.finish(),latency.finish(),src.finish(),dst.finish(),id.finish(),kind.finish(),order.finish(),parent.finish()});
}
class HashingOutput : public arrow::io::OutputStream {
    std::shared_ptr<arrow::io::OutputStream> output;
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(),EVP_MD_CTX_free};
public:
    explicit HashingOutput(const fs::path& path):output(checked(arrow::io::FileOutputStream::Open(path.string()))) {
        int64_t buffer_bytes=std::stoll(env("T3SIM_BUFFER_BYTES","1048576"));
        if (buffer_bytes>0) output=checked(arrow::io::BufferedOutputStream::Create(buffer_bytes,arrow::default_memory_pool(),output));
        set_mode(arrow::io::FileMode::WRITE);
        if (!context||EVP_DigestInit_ex(context.get(),EVP_sha256(),nullptr)!=1) throw std::runtime_error("hash init");
    }
    arrow::Status Write(const void* data,int64_t bytes) override {
        auto status=output->Write(data,bytes);if (!status.ok()) return status;
        if (EVP_DigestUpdate(context.get(),data,size_t(bytes))!=1) return arrow::Status::IOError("hash update");
        return arrow::Status::OK();
    }
    arrow::Result<int64_t> Tell() const override { return output->Tell(); }
    bool closed() const override { return output->closed(); }
    arrow::Status Close() override { return output->Close(); }
    arrow::Status Flush() override { return output->Flush(); }
    std::string digest() {
        unsigned char result[32];unsigned int size;
        if (EVP_DigestFinal_ex(context.get(),result,&size)!=1) throw std::runtime_error("hash finish");
        std::ostringstream stream;stream<<std::hex<<std::setfill('0');for (unsigned char b:result) stream<<std::setw(2)<<int(b);return stream.str();
    }
};
#include "fast_parquet.hpp"
std::string write_parquet(const std::shared_ptr<arrow::Table>& table,const fs::path& path) {
    if(env("T3SIM_WRITER","arrow")=="pages") return fastpq::write(table,path);
    parquet::WriterProperties::Builder properties;
    std::string codec=env("T3SIM_COMPRESSION","snappy");
    properties.compression(codec=="none"?parquet::Compression::UNCOMPRESSED:codec=="lz4"?parquet::Compression::LZ4:parquet::Compression::SNAPPY);
    properties.version(parquet::ParquetVersion::PARQUET_2_6);
    if (env("T3SIM_STATISTICS","on")=="off") properties.disable_statistics();
    std::string encoding=env("T3SIM_PARQUET_ENCODING","delta");
    if (encoding!="legacy") {
        properties.disable_dictionary();
        for (const auto& field:table->schema()->fields()) {
            if (field->type()->id()==arrow::Type::STRING||field->type()->id()==arrow::Type::DICTIONARY) properties.enable_dictionary(field->name());
            else if (encoding=="delta") properties.encoding(field->name(),parquet::Encoding::DELTA_BINARY_PACKED);
        }
    }
    parquet::ArrowWriterProperties::Builder arrow_properties;
    // Without an embedded dictionary Arrow schema, ordinary Parquet reads
    // reconstruct UTF8 columns, as required by the public output interface.
    bool dictionary=false;for (const auto& field:table->schema()->fields()) dictionary|=field->type()->id()==arrow::Type::DICTIONARY;
    if (!dictionary) arrow_properties.store_schema();
    arrow_properties.set_use_threads(false);
    auto output=std::make_shared<HashingOutput>(path);
    auto parquet_properties=properties.build();auto arrow_options=arrow_properties.build();
    if (dictionary) {
        // Arrow 15's store_schema=false drops the schema metadata as well as
        // the dictionary schema. Attach pandas metadata to the Parquet footer
        // explicitly so nullable nanoseconds stay Int64 on pandas reads.
        std::shared_ptr<parquet::SchemaDescriptor> descriptor;
        checked(parquet::arrow::ToParquetSchema(table->schema().get(),*parquet_properties,*arrow_options,&descriptor));
        auto root=std::static_pointer_cast<parquet::schema::GroupNode>(descriptor->schema_root());
        auto parquet_writer=parquet::ParquetFileWriter::Open(output,root,parquet_properties,table->schema()->metadata());
        std::unique_ptr<parquet::arrow::FileWriter> writer;
        checked(parquet::arrow::FileWriter::Make(arrow::default_memory_pool(),std::move(parquet_writer),table->schema(),arrow_options,&writer));
        checked(writer->WriteTable(*table,262144));checked(writer->Close());
    } else checked(parquet::arrow::WriteTable(*table,arrow::default_memory_pool(),output,262144,parquet_properties,arrow_options));
    checked(output->Close());return env("T3SIM_HASH_MODE","stream")=="reread"?sha256(path):output->digest();
}
Json simulate(const fs::path& config_path,const fs::path& out,std::optional<uint32_t> seed={},std::optional<Time> since={},bool parallel_output=true) {
    Time start=since.value_or(Clock::now());Json scenario=read_json(config_path);if (seed) scenario["seed"]=*seed;
    Simulation simulation(scenario);double config_sec=seconds(start);Time loop=Clock::now();simulation.run();double loop_sec=seconds(loop);
    if(env("T3SIM_PIPELINE","0")=="1"&&parallel_output) {
        fs::path parent=out.parent_path();if(parent.empty()) parent=".";fs::create_directories(parent);
        i64 n_events=simulation.c.trace.size(),n_messages=simulation.c.ledger.size();
        struct Written {std::string hash;double assembly,writing;};
        auto ledger_future=std::async(std::launch::async,[&]() {
            Time phase=Clock::now();auto ledger=ledger_table(simulation.c);
            std::vector<LedgerRow>().swap(simulation.c.ledger);double assembly=seconds(phase);
            phase=Clock::now();auto hash=write_parquet(ledger,parent/"message_trace.parquet");return Written{hash,assembly,seconds(phase)};
        });
        Time phase=Clock::now();simulation.c.finish();double sort_sec=seconds(phase);
        phase=Clock::now();auto trace=trace_table(simulation.c);std::vector<TraceRow>().swap(simulation.c.trace);double assembly_sec=seconds(phase);
        phase=Clock::now();auto th=write_parquet(trace,out);double write_sec=seconds(phase);auto ledger=ledger_future.get();double elapsed=seconds(start);
        Json result={{"scenario_id",scenario.at("scenario_id")},{"seed",scenario.at("seed")},{"n_events",n_events},{"n_messages",n_messages},
            {"wall_clock_sec",elapsed},{"events_per_sec",n_events/elapsed},{"trace_sha256",th},{"message_trace_sha256",ledger.hash},
            {"peak_memory_bytes",peak_memory()},{"gpu_seconds",0.},{"configuration_sec",config_sec},{"simulation_loop_sec",loop_sec},
            {"sorting_sec",sort_sec},{"column_assembly_sec",std::max(assembly_sec,ledger.assembly)},
            {"output_write_sec",std::max(write_sec,ledger.writing)},{"pipeline",true},
            {"trace_column_assembly_sec",assembly_sec},{"ledger_column_assembly_sec",ledger.assembly},
            {"trace_output_write_sec",write_sec},{"ledger_output_write_sec",ledger.writing}};
        write_json(parent/"events.json",result);return result;
    }
    Time sort=Clock::now();simulation.c.finish();double sort_sec=seconds(sort);
    Time assembly=Clock::now();auto trace=trace_table(simulation.c);
    std::vector<TraceRow>().swap(simulation.c.trace);
    auto ledger=ledger_table(simulation.c);std::vector<LedgerRow>().swap(simulation.c.ledger);
    double assembly_sec=seconds(assembly);
    fs::path parent=out.parent_path();if (parent.empty()) parent=".";fs::create_directories(parent);
    Time writing=Clock::now();std::string th,mh;
    if (env("T3SIM_IO_THREADS",parallel_output?"2":"1")=="1") { th=write_parquet(trace,out);mh=write_parquet(ledger,parent/"message_trace.parquet"); }
    else {
        auto future=std::async(std::launch::async,[&](){return write_parquet(trace,out);});
        mh=write_parquet(ledger,parent/"message_trace.parquet");th=future.get();
    }
    double write_sec=seconds(writing),elapsed=seconds(start);
    Json result={{"scenario_id",scenario.at("scenario_id")},{"seed",scenario.at("seed")},{"n_events",trace->num_rows()},
        {"n_messages",ledger->num_rows()},{"wall_clock_sec",elapsed},{"events_per_sec",trace->num_rows()/elapsed},
        {"trace_sha256",th},{"message_trace_sha256",mh},{"peak_memory_bytes",peak_memory()},{"gpu_seconds",0.},
        {"simulation_loop_sec",loop_sec},{"configuration_sec",config_sec},{"sorting_sec",sort_sec},{"column_assembly_sec",assembly_sec},{"output_write_sec",write_sec}};
    write_json(parent/"events.json",result);return result;
}
int cpu_budget() {
    unsigned hardware=std::thread::hardware_concurrency();int budget=std::min(4,int(hardware?hardware:1));
    std::ifstream file("/sys/fs/cgroup/cpu.max");std::string quota;int period;
    if (file>>quota>>period && quota!="max") budget=std::min(budget,std::max(1,std::stoi(quota)/period));return budget;
}
Json batch(const fs::path& input,const fs::path& output,int workers,Time start) {
    std::vector<fs::path> paths;for (const auto& file:fs::directory_iterator(input)) if (file.is_regular_file()&&file.path().extension()==".json") paths.push_back(file.path());
    std::sort(paths.begin(),paths.end());if (paths.empty()) throw std::invalid_argument("no scenario JSON files");
    workers=std::min({workers,cpu_budget(),int(paths.size())});std::vector<Json> results(paths.size());std::atomic<size_t> next{0};
    std::mutex mutex;std::exception_ptr failure;
    auto work=[&]() {
        try { while (true) { size_t i=next.fetch_add(1);if (i>=paths.size()) break;results[i]=simulate(paths[i],output/paths[i].stem()/"trace.parquet",{},{},workers==1); } }
        catch (...) { std::lock_guard<std::mutex> lock(mutex);if (!failure) failure=std::current_exception(); }
    };
    std::vector<std::thread> threads;for (int i=1;i<workers;++i) threads.emplace_back(work);work();for (auto& thread:threads) thread.join();if (failure) std::rethrow_exception(failure);
    i64 total=0;Json per=Json::array();for (size_t i=0;i<paths.size();++i) {
        total+=results[i]["n_events"].get<i64>();per.push_back({{"sub",paths[i].stem().string()},{"n_events",results[i]["n_events"]},{"trace_sha256",results[i]["trace_sha256"]}});
    }
    double elapsed=seconds(start);Json result={{"n_scenarios",paths.size()},{"total_events",total},{"wall_clock_sec",elapsed},
        {"events_per_sec",total/elapsed},{"gpu_seconds",0.},{"peak_memory_bytes",peak_memory()},{"per_scenario",per}};
    fs::create_directories(output);write_json(output/"batch_events.json",result);return result;
}
// Test-only deterministic draw stream; not used by simulation or scoring.
void random_test(const fs::path& config,const fs::path& out) {
    Json j=read_json(config);Random rng(j.at("seed").get<uint32_t>());std::ofstream file(out,std::ios::binary);
    for (int i=0;i<j.at("count").get<int>();++i) {
        double data[7];data[0]=rng.normal(j.value("mean",11.),j.value("scale",2.));data[1]=double(rng.bounded(2));data[2]=double(rng.bounded(j.at("upper").get<uint64_t>()));
        data[3]=rng.lognormal(6.214608098422191,0.3);data[4]=rng.pareto(1.5);data[5]=rng.exponential(100);data[6]=rng.uniform(100,2000);
        file.write(reinterpret_cast<const char*>(data),sizeof(data));
    }
    if (!file) throw std::runtime_error("random test output failed");
}
int main(int argc,char** argv) {
    Time start=Clock::now();
    try {
        if (argc<2||std::string(argv[1])=="--help") { std::cout<<"Agenthon Track 3 C++17 exact simulator\nsimulate --config FILE --out FILE [--seed N]\nsimulate-batch --batch-dir DIR --out-dir DIR [--workers 1..4]\n";return 0; }
        std::map<std::string,std::string> args;
        for (int i=2;i<argc;i+=2) { if (i+1>=argc) throw std::invalid_argument("missing option value");args[argv[i]]=argv[i+1]; }
        checked(arrow::SetCpuThreadPoolCapacity(1));std::string command=argv[1];Json result;
        if (command=="simulate") {
            std::optional<uint32_t> seed;
            if (args.count("--seed")) {
                auto value=std::stoll(args.at("--seed"));
                if (value<0||uint64_t(value)>UINT32_MAX) throw std::invalid_argument("seed must be 0..4294967295");seed=uint32_t(value);
            }
            result=simulate(args.at("--config"),args.at("--out"),seed,start);
        } else if (command=="simulate-batch") {
            int workers=args.count("--workers")?std::stoi(args.at("--workers")):4;
            if (workers<1||workers>4) throw std::invalid_argument("workers must be 1..4");result=batch(args.at("--batch-dir"),args.at("--out-dir"),workers,start);
        } else if (command=="random-test") { random_test(args.at("--config"),args.at("--out"));return 0; }
        else throw std::invalid_argument("unknown command: "+command);
        std::cout<<result.dump()<<'\n';return 0;
    } catch (const std::exception& error) { std::cerr<<"t3sim: "<<error.what()<<'\n';return 1; }
}
