#pragma once
#include <snappy.h>
#include <openssl/evp.h>
#include <cstdio>
#include <functional>
#include <limits>
#include <type_traits>

// Independently authored implementation of the flat subset required by T3:
// optional INT32/INT64/UTF8, Snappy, delta integers, categorical dictionaries,
// data pages v1, compact-protocol footer, and pandas nullable-integer metadata.
// No Arrow tables, object conversion, reflection, or general nested schemas.
namespace litepq {
using Bytes=std::vector<uint8_t>;
void varint(Bytes& out,uint64_t value) {
    while(value>=128) {out.push_back(uint8_t(value)|128);value>>=7;}out.push_back(uint8_t(value));
}
uint64_t zigzag(int64_t value) {return (uint64_t(value)<<1)^uint64_t(value>>63);}
void little32(Bytes& out,uint32_t value) {for(int i=0;i<4;++i) out.push_back(uint8_t(value>>(8*i)));}
struct Compact {
    Bytes& bytes;int last=0;
    void field(int id,int type) {
        int delta=id-last;
        if(delta>0&&delta<16) bytes.push_back(uint8_t(delta*16+type));
        else {bytes.push_back(uint8_t(type));varint(bytes,zigzag(id));}
        last=id;
    }
    void integer(int id,int64_t value,int type=5) {field(id,type);varint(bytes,zigzag(value));}
    void binary_value(const std::string& value) {varint(bytes,value.size());bytes.insert(bytes.end(),value.begin(),value.end());}
    void binary(int id,const std::string& value) {field(id,8);binary_value(value);}
    void list(int id,int type,size_t count) {field(id,9);if(count<15) bytes.push_back(uint8_t(count*16+type));else {bytes.push_back(uint8_t(240+type));varint(bytes,count);}}
    template<class F> void object(int id,F write) {field(id,12);Compact child{bytes};write(child);child.end();}
    void end() {bytes.push_back(0);}
};
struct Output {
    std::FILE* file=nullptr;int64_t position=0;std::vector<char> buffering;
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> hash{EVP_MD_CTX_new(),EVP_MD_CTX_free};
    explicit Output(const fs::path& path):buffering(1048576) {
        file=std::fopen(path.c_str(),"wb");if(!file) throw std::runtime_error("cannot open "+path.string());
        std::setvbuf(file,buffering.data(),_IOFBF,buffering.size());
        if(!hash||EVP_DigestInit_ex(hash.get(),EVP_sha256(),nullptr)!=1) {std::fclose(file);file=nullptr;throw std::runtime_error("hash init");}
    }
    ~Output() {if(file) std::fclose(file);}
    void write(const void* data,size_t bytes) {
        if(std::fwrite(data,1,bytes,file)!=bytes) throw std::runtime_error("Parquet write failed");
        if(EVP_DigestUpdate(hash.get(),data,bytes)!=1) throw std::runtime_error("hash update");position+=bytes;
    }
    void write(const Bytes& bytes) {write(bytes.data(),bytes.size());}
    std::string close() {
        int status=std::fclose(file);file=nullptr;if(status) throw std::runtime_error("Parquet close failed");
        unsigned char result[32];unsigned int size;
        if(EVP_DigestFinal_ex(hash.get(),result,&size)!=1) throw std::runtime_error("hash finish");
        static const char digits[]="0123456789abcdef";std::string out(64,'0');
        for(int i=0;i<32;++i) {out[i*2]=digits[result[i]>>4];out[i*2+1]=digits[result[i]&15];}return out;
    }
};
template<int WIDTH> void pack32(Bytes& out,const uint64_t* values) {
    if constexpr(WIDTH>0) {
        uint64_t words[WIDTH/2+1]={};
        #pragma GCC unroll 32
        for(int i=0;i<32;++i) {
            int bit=i*WIDTH,index=bit/64,shift=bit%64;words[index]|=values[i]<<shift;
            if(shift+WIDTH>64) words[index+1]|=values[i]>>(64-shift);
        }
        size_t offset=out.size();out.resize(offset+WIDTH*4);std::memcpy(out.data()+offset,words,WIDTH*4);
    }
}
void bitpack32(Bytes& out,const uint64_t* values,int width) {
    #define LITE_PACK_CASE(W) case W: pack32<W>(out,values);break
    switch(width) {
        LITE_PACK_CASE(0);LITE_PACK_CASE(1);LITE_PACK_CASE(2);LITE_PACK_CASE(3);LITE_PACK_CASE(4);LITE_PACK_CASE(5);LITE_PACK_CASE(6);LITE_PACK_CASE(7);
        LITE_PACK_CASE(8);LITE_PACK_CASE(9);LITE_PACK_CASE(10);LITE_PACK_CASE(11);LITE_PACK_CASE(12);LITE_PACK_CASE(13);LITE_PACK_CASE(14);LITE_PACK_CASE(15);
        LITE_PACK_CASE(16);LITE_PACK_CASE(17);LITE_PACK_CASE(18);LITE_PACK_CASE(19);LITE_PACK_CASE(20);LITE_PACK_CASE(21);LITE_PACK_CASE(22);LITE_PACK_CASE(23);
        LITE_PACK_CASE(24);LITE_PACK_CASE(25);LITE_PACK_CASE(26);LITE_PACK_CASE(27);LITE_PACK_CASE(28);LITE_PACK_CASE(29);LITE_PACK_CASE(30);LITE_PACK_CASE(31);
        LITE_PACK_CASE(32);LITE_PACK_CASE(33);LITE_PACK_CASE(34);LITE_PACK_CASE(35);LITE_PACK_CASE(36);LITE_PACK_CASE(37);LITE_PACK_CASE(38);LITE_PACK_CASE(39);
        LITE_PACK_CASE(40);LITE_PACK_CASE(41);LITE_PACK_CASE(42);LITE_PACK_CASE(43);LITE_PACK_CASE(44);LITE_PACK_CASE(45);LITE_PACK_CASE(46);LITE_PACK_CASE(47);
        LITE_PACK_CASE(48);LITE_PACK_CASE(49);LITE_PACK_CASE(50);LITE_PACK_CASE(51);LITE_PACK_CASE(52);LITE_PACK_CASE(53);LITE_PACK_CASE(54);LITE_PACK_CASE(55);
        LITE_PACK_CASE(56);LITE_PACK_CASE(57);LITE_PACK_CASE(58);LITE_PACK_CASE(59);LITE_PACK_CASE(60);LITE_PACK_CASE(61);LITE_PACK_CASE(62);LITE_PACK_CASE(63);LITE_PACK_CASE(64);
        default:throw std::invalid_argument("invalid bit width");
    }
    #undef LITE_PACK_CASE
}
int bit_width(uint64_t value) {return value?64-__builtin_clzll(value):0;}
template<class T> Bytes delta(const std::vector<T>& values) {
    Bytes out;out.reserve(values.size()*sizeof(T)/2+64);
    varint(out,128);varint(out,4);varint(out,values.size());varint(out,zigzag(values.empty()?0:values[0]));
    using U=std::make_unsigned_t<T>;T previous=values.empty()?0:values[0];
    for(size_t first=1;first<values.size();first+=128) {
        size_t n=std::min(size_t(128),values.size()-first);T differences[128],minimum=std::numeric_limits<T>::max();
        for(size_t i=0;i<n;++i) {
            U bits=U(values[first+i])-U(previous);T difference;std::memcpy(&difference,&bits,sizeof(T));
            differences[i]=difference;minimum=std::min(minimum,difference);previous=values[first+i];
        }
        varint(out,zigzag(minimum));uint64_t normalized[128]={};int widths[4]={};
        for(size_t i=0;i<n;++i) normalized[i]=U(differences[i])-U(minimum);
        for(int mini=0;mini<4;++mini) {
            uint64_t maximum=0;for(int i=mini*32;i<mini*32+32;++i) maximum|=normalized[i];
            widths[mini]=bit_width(maximum);out.push_back(uint8_t(widths[mini]));
        }
        for(int mini=0;mini<4;++mini) bitpack32(out,normalized+mini*32,widths[mini]);
    }
    return out;
}
template<class F> Bytes dictionary_indices(int64_t n,int width,F get) {
    Bytes out;out.reserve(size_t((n+7)/8*width+16));out.push_back(uint8_t(width));varint(out,uint64_t((n+7)/8)*2+1);
    for(int64_t first=0;first<n;first+=8) {
        __uint128_t packed=0;
        for(int i=0;i<8&&first+i<n;++i) packed|=__uint128_t(get(first+i))<<(i*width);
        for(int i=0;i<width;++i) out.push_back(uint8_t(packed>>(i*8)));
    }
    return out;
}
struct Field {std::string name;int physical;bool nullable=false,categorical=false;std::vector<std::string> labels;};
struct Chunk {int physical,encoding;int64_t n,data_offset=0,dict_offset=-1,compressed=0,uncompressed=0;};
struct Group {int64_t n,bytes=0,compressed=0;std::vector<Chunk> columns;};
std::string pandas(const std::vector<Field>& fields) {
    Json columns=Json::array();
    for(const auto& f:fields) {
        bool string=f.physical==6;std::string type=string?"unicode":f.physical==1?"int32":"int64";
        columns.push_back({{"name",f.name},{"field_name",f.name},{"pandas_type",type},{"numpy_type",f.nullable?"Int64":string?"string":type},{"metadata",nullptr}});
    }
    return Json({{"index_columns",Json::array()},{"column_indexes",Json::array()},{"columns",columns},
        {"creator",{{"library","agenthon-t3"},{"version","4.0"}}},{"pandas_version","1.5.3"}}).dump();
}
Bytes footer(const std::vector<Field>& fields,const std::vector<Group>& groups,int64_t rows) {
    Bytes bytes;Compact file{bytes};file.integer(1,2);
    file.list(2,12,fields.size()+1);
    {Compact root{bytes};root.binary(4,"schema");root.integer(5,fields.size());root.end();}
    for(const auto& field:fields) {
        Compact node{bytes};node.integer(1,field.physical);node.integer(3,1);node.binary(4,field.name);
        if(field.physical==6) {node.integer(6,0);node.object(10,[](Compact& logical){logical.object(1,[](Compact&){});});}
        node.end();
    }
    file.integer(3,rows,6);file.list(4,12,groups.size());
    for(const auto& group:groups) {
        Compact rowgroup{bytes};rowgroup.list(1,12,fields.size());
        for(size_t i=0;i<fields.size();++i) {
            const auto& column=group.columns[i];Compact chunk{bytes};
            chunk.integer(2,column.dict_offset>=0?column.dict_offset:column.data_offset,6);
            chunk.object(3,[&](Compact& meta) {
                meta.integer(1,column.physical);
                meta.list(2,5,column.dict_offset>=0?3:2);varint(bytes,zigzag(3));varint(bytes,zigzag(column.encoding));if(column.dict_offset>=0) varint(bytes,0);
                meta.list(3,8,1);meta.binary_value(fields[i].name);meta.integer(4,1);
                meta.integer(5,column.n,6);meta.integer(6,column.uncompressed,6);meta.integer(7,column.compressed,6);
                meta.integer(9,column.data_offset,6);if(column.dict_offset>=0) meta.integer(11,column.dict_offset,6);
            });chunk.end();
        }
        rowgroup.integer(2,group.bytes,6);rowgroup.integer(3,group.n,6);rowgroup.integer(6,group.compressed,6);rowgroup.end();
    }
    file.list(5,12,1);{Compact kv{bytes};kv.binary(1,"pandas");kv.binary(2,pandas(fields));kv.end();}
    file.binary(6,"agenthon-t3 native flat writer 4.0");file.end();return bytes;
}
class Writer {
    Output output;std::vector<Field> fields;std::vector<Group> groups;std::string compressed;
    int64_t rows=0;
    bool deferred_types=false;
    std::vector<uint8_t> type_codes;
    std::vector<size_t> last_execution;
    void page(Chunk& chunk,const Bytes& raw,int count,bool dictionary) {
        compressed.resize(snappy::MaxCompressedLength(raw.size()));size_t size=compressed.size();
        snappy::RawCompress(reinterpret_cast<const char*>(raw.data()),raw.size(),compressed.data(),&size);compressed.resize(size);
        if(raw.size()>INT32_MAX||size>INT32_MAX) throw std::runtime_error("Parquet page too large");
        Bytes header;Compact p{header};p.integer(1,dictionary?2:0);p.integer(2,raw.size());p.integer(3,size);
        if(dictionary) {
            p.object(7,[&](Compact& h){h.integer(1,count);h.integer(2,0);});chunk.dict_offset=output.position;
        } else {
            p.object(5,[&](Compact& h){h.integer(1,count);h.integer(2,chunk.encoding);h.integer(3,3);h.integer(4,3);});chunk.data_offset=output.position;
        }
        p.end();chunk.uncompressed+=header.size()+raw.size();chunk.compressed+=header.size()+size;
        output.write(header);output.write(compressed.data(),size);
    }
    template<class T,class F> void numeric(Chunk& chunk,const Field& field,int64_t first,int64_t n,F get) {
        std::vector<T> values;values.reserve(n);Bytes levels;
        if(field.nullable) {
            varint(levels,uint64_t((n+7)/8)*2+1);size_t prefix=levels.size();levels.resize(prefix+size_t((n+7)/8),0);
            for(int64_t i=0;i<n;++i) {T value=T(get(first+i));if(value>=0) {values.push_back(value);levels[prefix+size_t(i/8)]|=uint8_t(1<<(i%8));}}
        } else {varint(levels,uint64_t(n)*2);levels.push_back(1);for(int64_t i=0;i<n;++i) values.push_back(T(get(first+i)));}
        Bytes raw;little32(raw,levels.size());raw.insert(raw.end(),levels.begin(),levels.end());
        bool dictionary=false;
        if(field.categorical&&env("T3SIM_LITE_DICT","0")=="1"&&!values.empty()) {
            auto limits=std::minmax_element(values.begin(),values.end());T minimum=*limits.first,maximum=*limits.second;uint64_t range=uint64_t(maximum)-uint64_t(minimum);
            if(range<4096&&range+1<uint64_t(n)) {
                Bytes dict;dict.resize(size_t(range+1)*sizeof(T));for(uint64_t i=0;i<=range;++i) {T value=minimum+T(i);std::memcpy(dict.data()+i*sizeof(T),&value,sizeof(T));}
                page(chunk,dict,int(range+1),true);chunk.encoding=8;
                auto indices=dictionary_indices(n,std::max(1,bit_width(range)),[&](int64_t i){return uint64_t(values[i])-uint64_t(minimum);});
                raw.insert(raw.end(),indices.begin(),indices.end());dictionary=true;
            }
        }
        if(!dictionary) {
            auto encoded=delta(values);chunk.encoding=5;raw.insert(raw.end(),encoded.begin(),encoded.end());
        }
        page(chunk,raw,int(n),false);
    }
    template<class F> void strings(Chunk& chunk,const Field& field,int64_t first,int64_t n,F get) {
        Bytes dict;for(const auto& value:field.labels) {little32(dict,value.size());dict.insert(dict.end(),value.begin(),value.end());}
        page(chunk,dict,field.labels.size(),true);chunk.encoding=8;
        Bytes levels;varint(levels,uint64_t(n)*2);levels.push_back(1);Bytes raw;little32(raw,levels.size());raw.insert(raw.end(),levels.begin(),levels.end());
        auto indices=dictionary_indices(n,std::max(1,bit_width(field.labels.size()-1)),[&](int64_t i){return get(first+i);});
        raw.insert(raw.end(),indices.begin(),indices.end());page(chunk,raw,int(n),false);
    }
public:
    Writer(const fs::path& path,std::vector<Field> schema):output(path),fields(std::move(schema)) {output.write("PAR1",4);}
    template<class F> void append(int64_t n,F get) {
        if(n<=0) return;Group group{n};group.columns.reserve(fields.size());
        for(size_t col=0;col<fields.size();++col) {
            const auto& field=fields[col];Chunk chunk{field.physical,5,n};auto value=[&](int64_t i){return get(col,i);};
            if(field.physical==6) strings(chunk,field,0,n,value);
            else if(field.physical==1) numeric<int32_t>(chunk,field,0,n,value);
            else numeric<int64_t>(chunk,field,0,n,value);
            group.bytes+=chunk.uncompressed;group.compressed+=chunk.compressed;group.columns.push_back(chunk);
        }
        groups.push_back(std::move(group));rows+=n;
    }
    void append_trace(const std::vector<TraceRow>& trace,int64_t first,int64_t n) {
        if(n<=0) return;deferred_types=true;Group group{n};group.columns.reserve(fields.size());
        for(int64_t i=0;i<n;++i) {
            const auto& row=trace[first+i];int kind=row.kind;
            if(kind==EXEC) {
                if(row.order_id<0) throw std::runtime_error("execution has no order ID");
                if(size_t(row.order_id)>=last_execution.size()) last_execution.resize(size_t(row.order_id)+1,SIZE_MAX);
                size_t& previous=last_execution[size_t(row.order_id)];if(previous!=SIZE_MAX) type_codes[previous]=PARTIAL;
                previous=type_codes.size();kind=FILL;
            }
            type_codes.push_back(uint8_t(kind));
        }
        for(size_t col=0;col<fields.size();++col) {
            const auto& field=fields[col];Chunk chunk{field.physical,8,n};
            auto value=[&](int64_t i)->i64 {const auto& r=trace[first+i];switch(col) {case 0:return r.time;case 1:return r.agent;case 3:return r.side;case 4:return r.price;case 5:return r.size;default:return r.order_id;}};
            if(col!=2) {
                if(field.physical==6) strings(chunk,field,0,n,value);
                else if(field.physical==1) numeric<int32_t>(chunk,field,0,n,value);
                else numeric<int64_t>(chunk,field,0,n,value);
            }
            group.bytes+=chunk.uncompressed;group.compressed+=chunk.compressed;group.columns.push_back(chunk);
        }
        groups.push_back(std::move(group));rows+=n;
    }
    std::string close() {
        if(deferred_types) {
            int64_t first=0;
            for(auto& group:groups) {
                Chunk chunk{6,8,group.n};strings(chunk,fields[2],0,group.n,[&](int64_t i){return type_codes[first+i];});
                group.columns[2]=chunk;group.bytes+=chunk.uncompressed;group.compressed+=chunk.compressed;first+=group.n;
            }
        }
        auto bytes=footer(fields,groups,rows);output.write(bytes);Bytes end;little32(end,bytes.size());end.insert(end.end(),{'P','A','R','1'});output.write(end);return output.close();
    }
};
std::vector<Field> trace_schema() {
    return {{"t_ns",2},{"agent_id",1,false,true},{"msg_type",6,false,false,{"ORDER_SUBMITTED","ORDER_ACCEPTED","ORDER_EXECUTED","ORDER_CANCELLED","QUOTE_UPDATE","ORDER_FILLED","PARTIAL_FILL"}},
        {"side",6,false,false,{"BID","ASK"}},{"price",2,false,true},{"size",2,false,true},{"order_id",2}};
}
std::vector<Field> ledger_schema() {
    return {{"seq",2},{"t_recv_ns",2},{"t_send_ns",2,true},{"latency_ns",2},{"src_id",1,false,true},{"dst_id",1,false,true},{"message_id",2},
        {"msg_type",6,false,false,{"AGENT_WAKEUP","MarketClosePriceRequestMsg","MarketHoursRequestMsg","MarketHoursMsg","QuerySpreadMsg","QuerySpreadResponseMsg","LimitOrderMsg","CancelOrderMsg","OrderAcceptedMsg","OrderExecutedMsg","OrderCancelledMsg","MarketClosedMsg","MarketClosePriceMsg"}},
        {"order_id",2,true},{"causal_parent",2,true}};
}
std::string trace(const Core& core,const fs::path& path) {
    Writer writer(path,trace_schema());
    for(int64_t first=0;first<int64_t(core.trace.size());) {
        int64_t end=std::min(first+16384,int64_t(core.trace.size()));
        while(end<int64_t(core.trace.size())&&core.trace[end].time==core.trace[end-1].time) ++end;
        writer.append_trace(core.trace,first,end-first);first=end;
    }
    return writer.close();
}
std::string ledger(const Core& core,const fs::path& path) {
    Writer writer(path,ledger_schema());int64_t group=65536;
    if(group<1||group>1048576) throw std::invalid_argument("invalid row group size");
    for(int64_t first=0;first<int64_t(core.ledger.size());first+=group) {
        int64_t n=std::min(group,int64_t(core.ledger.size())-first);
        writer.append(n,[&](size_t col,int64_t i)->i64 {const auto& r=core.ledger[first+i];switch(col) {case 0:return first+i;case 1:return r.recv;case 2:return r.send;case 3:return r.latency;case 4:return r.src;case 5:return r.dst;case 6:return r.id;case 7:return r.kind;case 8:return r.order_id;default:return r.parent;}});
    }
    return writer.close();
}
void ledger_block(Writer& writer,const std::vector<LedgerRow>& rows,int64_t first) {
    writer.append(rows.size(),[&](size_t col,int64_t i)->i64 {const auto& r=rows[i];switch(col) {case 0:return first+i;case 1:return r.recv;case 2:return r.send;case 3:return r.latency;case 4:return r.src;case 5:return r.dst;case 6:return r.id;case 7:return r.kind;case 8:return r.order_id;default:return r.parent;}});
}
}
