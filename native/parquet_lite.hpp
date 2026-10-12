#pragma once
#include <snappy.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <cstdio>
#include <functional>
#include <limits>
#include <type_traits>
#include <future>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#ifdef T3_FAST_SNAPPY
namespace snappy_fast {void RawCompress(const char*,size_t,char*,size_t*);}
#endif
#ifdef T3_CACHED_SNAPPY
void cached_snappy(const char*,size_t,char*,size_t*);
void cached_snappy_fast(const char*,size_t,char*,size_t*);
#endif

// Independently authored implementation of the flat subset required by T3:
// optional INT32/INT64/UTF8, Snappy, delta integers, categorical dictionaries,
// data pages v1, compact-protocol footer, and pandas nullable-integer metadata.
// No Arrow tables, object conversion, reflection, or general nested schemas.
namespace litepq {
template<class T> struct UninitializedAllocator:std::allocator<T> {
    using value_type=T;
    template<class U> struct rebind {using other=UninitializedAllocator<U>;};
    UninitializedAllocator()=default;
    template<class U> UninitializedAllocator(const UninitializedAllocator<U>&) {}
    template<class U,class... Args> void construct(U* pointer,Args&&... args) {
        if constexpr(sizeof...(Args)==0) ::new(static_cast<void*>(pointer)) U;
        else ::new(static_cast<void*>(pointer)) U(std::forward<Args>(args)...);
    }
};
#ifdef T3_UNINITIALIZED_BYTES
template<class T> using Buffer=std::vector<T,UninitializedAllocator<T>>;
#else
template<class T> using Buffer=std::vector<T>;
#endif
using Compress=void(*)(const char*,size_t,char*,size_t*);
Compress compressor() {
#ifdef T3_CACHED_SNAPPY
    bool cache=env("T3SIM_CACHE_SNAPPY","1")!="0";
#endif
#ifdef T3_FAST_SNAPPY
    if(env("T3SIM_FAST_SNAPPY","auto")!="off"&&__builtin_cpu_supports("avx2")&&__builtin_cpu_supports("bmi2")&&__builtin_cpu_supports("sse4.2")&&__builtin_cpu_supports("ssse3")) {
#ifdef T3_CACHED_SNAPPY
        if(cache) return cached_snappy_fast;
#endif
        return snappy_fast::RawCompress;
    }
#endif
#ifdef T3_CACHED_SNAPPY
    if(cache) return cached_snappy;
#endif
    return snappy::RawCompress;
}
using Bytes=Buffer<uint8_t>;
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
    std::FILE* file=nullptr;int64_t position=0;Buffer<char> buffering;
#ifdef T3_DIRECT_HASH
    SHA256_CTX hash;
#else
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> hash{EVP_MD_CTX_new(),EVP_MD_CTX_free};
#endif
    explicit Output(const fs::path& path):buffering(1048576) {
        file=std::fopen(path.c_str(),"wb");if(!file) throw std::runtime_error("cannot open "+path.string());
        std::setvbuf(file,buffering.data(),_IOFBF,buffering.size());
#ifdef T3_DIRECT_HASH
        if(SHA256_Init(&hash)!=1) {std::fclose(file);file=nullptr;throw std::runtime_error("hash init");}
#else
        if(!hash||EVP_DigestInit_ex(hash.get(),EVP_sha256(),nullptr)!=1) {std::fclose(file);file=nullptr;throw std::runtime_error("hash init");}
#endif
    }
    ~Output() {if(file) std::fclose(file);}
    void write(const void* data,size_t bytes) {
        if(std::fwrite(data,1,bytes,file)!=bytes) throw std::runtime_error("Parquet write failed");
#ifdef T3_DIRECT_HASH
        if(SHA256_Update(&hash,data,bytes)!=1) throw std::runtime_error("hash update");
#else
        if(EVP_DigestUpdate(hash.get(),data,bytes)!=1) throw std::runtime_error("hash update");
#endif
        position+=bytes;
    }
    void write(const Bytes& bytes) {write(bytes.data(),bytes.size());}
    std::string close() {
        int status=std::fclose(file);file=nullptr;if(status) throw std::runtime_error("Parquet close failed");
        unsigned char result[32];unsigned int size;
#ifdef T3_DIRECT_HASH
        if(SHA256_Final(result,&hash)!=1) throw std::runtime_error("hash finish");
#else
        if(EVP_DigestFinal_ex(hash.get(),result,&size)!=1) throw std::runtime_error("hash finish");
#endif
        static const char digits[]="0123456789abcdef";std::string out(64,'0');
        for(int i=0;i<32;++i) {out[i*2]=digits[result[i]>>4];out[i*2+1]=digits[result[i]&15];}return out;
    }
};
template<int WIDTH> void pack32(Bytes& out,const uint64_t* values) {
    if constexpr(WIDTH>0) {
        if constexpr(WIDTH==64) {
            size_t offset=out.size();out.resize(offset+256);std::memcpy(out.data()+offset,values,256);return;
        } else if constexpr(WIDTH==32) {
            size_t offset=out.size();out.resize(offset+128);
            uint32_t* dst=reinterpret_cast<uint32_t*>(out.data()+offset);
            #pragma GCC unroll 32
            for(int i=0;i<32;++i) dst[i]=uint32_t(values[i]);
            return;
        } else if constexpr(WIDTH==16) {
            size_t offset=out.size();out.resize(offset+64);
            uint16_t* dst=reinterpret_cast<uint16_t*>(out.data()+offset);
            #pragma GCC unroll 32
            for(int i=0;i<32;++i) dst[i]=uint16_t(values[i]);
            return;
        } else if constexpr(WIDTH==8) {
            size_t offset=out.size();out.resize(offset+32);
            uint8_t* dst=out.data()+offset;
            #pragma GCC unroll 32
            for(int i=0;i<32;++i) dst[i]=uint8_t(values[i]);
            return;
        }
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
template<class T,class Container>
#ifdef T3_VECTOR_DELTA
__attribute__((target_clones("avx2","default")))
#endif
void delta_into(Bytes& out,const Container& values) {
    out.reserve(out.size()+values.size()*sizeof(T)/2+64);
    varint(out,128);varint(out,4);varint(out,values.size());varint(out,zigzag(values.empty()?0:values[0]));
    using U=std::make_unsigned_t<T>;T previous=values.empty()?0:values[0];
    for(size_t first=1;first<values.size();first+=128) {
        size_t n=std::min(size_t(128),values.size()-first);T differences[128],minimum=std::numeric_limits<T>::max();
        for(size_t i=0;i<n;++i) {
            T curr=values[first+i];
            T difference=static_cast<T>(U(curr)-U(previous));
            differences[i]=difference;minimum=std::min(minimum,difference);previous=curr;
        }
        varint(out,zigzag(minimum));uint64_t normalized[128]={};int widths[4]={};
        for(size_t i=0;i<n;++i) normalized[i]=U(differences[i])-U(minimum);
        for(int mini=0;mini<4;++mini) {
            uint64_t maximum=0;for(int i=mini*32;i<mini*32+32;++i) maximum|=normalized[i];
            widths[mini]=bit_width(maximum);out.push_back(uint8_t(widths[mini]));
        }
        for(int mini=0;mini<4;++mini) bitpack32(out,normalized+mini*32,widths[mini]);
    }
}
template<class T,class Container> Bytes delta(const Container& values) {Bytes out;delta_into<T>(out,values);return out;}
template<int WIDTH,class F> void narrow_indices_into(Bytes& out,int64_t n,F get) {
    out.reserve(out.size()+size_t((n+7)/8*WIDTH+16));out.push_back(uint8_t(WIDTH));varint(out,uint64_t((n+7)/8)*2+1);
    size_t prefix=out.size();out.resize(prefix+size_t((n+7)/8)*WIDTH);
    int64_t full=n/8*8;
    for(int64_t first=0;first<full;first+=8) {
        uint64_t packed=0;
        #pragma GCC unroll 8
        for(int i=0;i<8;++i) packed|=uint64_t(get(first+i))<<(i*WIDTH);
        std::memcpy(out.data()+prefix+size_t(first/8)*WIDTH,&packed,WIDTH);
    }
    if(full<n) {
        uint64_t packed=0;for(int i=0;full+i<n;++i) packed|=uint64_t(get(full+i))<<(i*WIDTH);
        std::memcpy(out.data()+prefix+size_t(full/8)*WIDTH,&packed,WIDTH);
    }
}
template<class F> void dictionary_indices_into(Bytes& out,int64_t n,int width,F get) {
#ifdef T3_NARROW_CODES
    switch(width) {
        case 1:narrow_indices_into<1>(out,n,get);return;case 2:narrow_indices_into<2>(out,n,get);return;
        case 3:narrow_indices_into<3>(out,n,get);return;case 4:narrow_indices_into<4>(out,n,get);return;
        case 5:narrow_indices_into<5>(out,n,get);return;case 6:narrow_indices_into<6>(out,n,get);return;
        case 7:narrow_indices_into<7>(out,n,get);return;case 8:narrow_indices_into<8>(out,n,get);return;
    }
#endif
    out.reserve(out.size()+size_t((n+7)/8*width+16));out.push_back(uint8_t(width));varint(out,uint64_t((n+7)/8)*2+1);
    for(int64_t first=0;first<n;first+=8) {
        __uint128_t packed=0;
        for(int i=0;i<8&&first+i<n;++i) packed|=__uint128_t(get(first+i))<<(i*width);
        for(int i=0;i<width;++i) out.push_back(uint8_t(packed>>(i*8)));
    }
}
struct Field {
    std::string name;int physical=0;bool nullable=false,categorical=false;std::vector<std::string> labels;
    Bytes dict_bytes;
    Field(std::string n,int p,bool null=false,bool cat=false,std::vector<std::string> l={}):
        name(std::move(n)),physical(p),nullable(null),categorical(cat),labels(std::move(l)) {
        if(!labels.empty()) {
            for(const auto& val:labels) {little32(dict_bytes,val.size());dict_bytes.insert(dict_bytes.end(),val.begin(),val.end());}
        }
    }
};
struct Chunk {int physical,encoding;int64_t n,data_offset=0,dict_offset=-1,compressed=0,uncompressed=0;};
struct Group {int64_t n,bytes=0,compressed=0;std::vector<Chunk> columns;};
std::string pandas(const std::vector<Field>& fields) {
    if(!fields.empty()&&fields[0].name=="t_ns") {
        return "{\"column_indexes\":[],\"columns\":[{\"field_name\":\"t_ns\",\"metadata\":null,\"name\":\"t_ns\",\"numpy_type\":\"int64\",\"pandas_type\":\"int64\"},{\"field_name\":\"agent_id\",\"metadata\":null,\"name\":\"agent_id\",\"numpy_type\":\"int32\",\"pandas_type\":\"int32\"},{\"field_name\":\"msg_type\",\"metadata\":null,\"name\":\"msg_type\",\"numpy_type\":\"string\",\"pandas_type\":\"unicode\"},{\"field_name\":\"side\",\"metadata\":null,\"name\":\"side\",\"numpy_type\":\"string\",\"pandas_type\":\"unicode\"},{\"field_name\":\"price\",\"metadata\":null,\"name\":\"price\",\"numpy_type\":\"int64\",\"pandas_type\":\"int64\"},{\"field_name\":\"size\",\"metadata\":null,\"name\":\"size\",\"numpy_type\":\"int64\",\"pandas_type\":\"int64\"},{\"field_name\":\"order_id\",\"metadata\":null,\"name\":\"order_id\",\"numpy_type\":\"int64\",\"pandas_type\":\"int64\"}],\"creator\":{\"library\":\"agenthon-t3\",\"version\":\"4.0\"},\"index_columns\":[],\"pandas_version\":\"1.5.3\"}";
    }
    if(!fields.empty()&&fields[0].name=="seq") {
        return "{\"column_indexes\":[],\"columns\":[{\"field_name\":\"seq\",\"metadata\":null,\"name\":\"seq\",\"numpy_type\":\"int64\",\"pandas_type\":\"int64\"},{\"field_name\":\"t_recv_ns\",\"metadata\":null,\"name\":\"t_recv_ns\",\"numpy_type\":\"int64\",\"pandas_type\":\"int64\"},{\"field_name\":\"t_send_ns\",\"metadata\":null,\"name\":\"t_send_ns\",\"numpy_type\":\"Int64\",\"pandas_type\":\"int64\"},{\"field_name\":\"latency_ns\",\"metadata\":null,\"name\":\"latency_ns\",\"numpy_type\":\"int64\",\"pandas_type\":\"int64\"},{\"field_name\":\"src_id\",\"metadata\":null,\"name\":\"src_id\",\"numpy_type\":\"int32\",\"pandas_type\":\"int32\"},{\"field_name\":\"dst_id\",\"metadata\":null,\"name\":\"dst_id\",\"numpy_type\":\"int32\",\"pandas_type\":\"int32\"},{\"field_name\":\"message_id\",\"metadata\":null,\"name\":\"message_id\",\"numpy_type\":\"int64\",\"pandas_type\":\"int64\"},{\"field_name\":\"msg_type\",\"metadata\":null,\"name\":\"msg_type\",\"numpy_type\":\"string\",\"pandas_type\":\"unicode\"},{\"field_name\":\"order_id\",\"metadata\":null,\"name\":\"order_id\",\"numpy_type\":\"Int64\",\"pandas_type\":\"int64\"},{\"field_name\":\"causal_parent\",\"metadata\":null,\"name\":\"causal_parent\",\"numpy_type\":\"Int64\",\"pandas_type\":\"int64\"}],\"creator\":{\"library\":\"agenthon-t3\",\"version\":\"4.0\"},\"index_columns\":[],\"pandas_version\":\"1.5.3\"}";
    }
    nlohmann::json columns=nlohmann::json::array();
    for(const auto& f:fields) {
        bool string=f.physical==6;std::string type=string?"unicode":f.physical==1?"int32":"int64";
        columns.push_back({{"name",f.name},{"field_name",f.name},{"pandas_type",type},{"numpy_type",f.nullable?"Int64":string?"string":type},{"metadata",nullptr}});
    }
    return nlohmann::json({{"index_columns",nlohmann::json::array()},{"column_indexes",nlohmann::json::array()},{"columns",columns},
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
struct ColumnPayload {
    Chunk chunk;
    Bytes bytes;
    int64_t rel_data_offset=0;
    int64_t rel_dict_offset=-1;
};
class Writer {
    Compress compress=compressor();
    Output output;std::vector<Field> fields;std::vector<Group> groups;Buffer<char> compressed;
    int64_t rows=0;
    bool deferred_types=false;
    std::vector<uint8_t> type_codes;
    std::vector<size_t> last_execution;
    bool plain_numeric=env("T3SIM_LITE_ENCODING","delta")=="plain";
    bool parallel=false;

    void page_into(ColumnPayload& cp,const Bytes& raw,int count,bool dictionary,Buffer<char>& scratch) {
        scratch.resize(snappy::MaxCompressedLength(raw.size()));size_t size=scratch.size();
        compress(reinterpret_cast<const char*>(raw.data()),raw.size(),scratch.data(),&size);scratch.resize(size);
        if(raw.size()>INT32_MAX||size>INT32_MAX) throw std::runtime_error("Parquet page too large");
        Bytes header;Compact p{header};p.integer(1,dictionary?2:0);p.integer(2,raw.size());p.integer(3,size);
        if(dictionary) {
            p.object(7,[&](Compact& h){h.integer(1,count);h.integer(2,0);});cp.rel_dict_offset=cp.bytes.size();
        } else {
            p.object(5,[&](Compact& h){h.integer(1,count);h.integer(2,cp.chunk.encoding);h.integer(3,3);h.integer(4,3);});cp.rel_data_offset=cp.bytes.size();
        }
        p.end();cp.chunk.uncompressed+=header.size()+raw.size();cp.chunk.compressed+=header.size()+size;
        cp.bytes.reserve(cp.bytes.size()+header.size()+size);
        cp.bytes.insert(cp.bytes.end(),header.begin(),header.end());
        cp.bytes.insert(cp.bytes.end(),reinterpret_cast<const uint8_t*>(scratch.data()),reinterpret_cast<const uint8_t*>(scratch.data())+size);
    }

    template<class T,class F> ColumnPayload encode_numeric(const Field& field,int64_t first,int64_t n,F get,Buffer<char>& scratch) {
        ColumnPayload cp{Chunk{field.physical,5,n},{}};
#ifdef T3_IMPLICIT_SEQUENCE
        if(field.name=="seq"&&!plain_numeric) {
            Bytes levels;varint(levels,uint64_t(n)*2);levels.push_back(1);
            Bytes raw;raw.reserve(size_t(n/128*5+32));little32(raw,levels.size());raw.insert(raw.end(),levels.begin(),levels.end());
            varint(raw,128);varint(raw,4);varint(raw,n);varint(raw,zigzag(get(first)));
            for(int64_t i=1;i<n;i+=128) {varint(raw,2);raw.insert(raw.end(),4,0);}
            cp.chunk.encoding=5;page_into(cp,raw,int(n),false,scratch);return cp;
        }
#endif
        if(field.nullable) {
            Buffer<T> values;values.resize(n);size_t valid=0;Bytes levels;
            varint(levels,uint64_t((n+7)/8)*2+1);size_t prefix=levels.size();levels.resize(prefix+size_t((n+7)/8),0);
            for(int64_t i=0;i<n;++i) {T value=T(get(first+i));if(value>=0) {
                values[valid++]=value;
                levels[prefix+size_t(i/8)]|=uint8_t(1<<(i%8));}}
            values.resize(valid);
            Bytes raw;little32(raw,levels.size());raw.insert(raw.end(),levels.begin(),levels.end());
            cp.chunk.encoding=5;delta_into<T>(raw,values);
            page_into(cp,raw,int(n),false,scratch);return cp;
        } else {
            Buffer<T> values;values.resize(n);
            for(int64_t i=0;i<n;++i) values[i]=T(get(first+i));
            Bytes levels;varint(levels,uint64_t(n)*2);levels.push_back(1);
            Bytes raw;little32(raw,levels.size());raw.insert(raw.end(),levels.begin(),levels.end());
            cp.chunk.encoding=5;delta_into<T>(raw,values);
            page_into(cp,raw,int(n),false,scratch);return cp;
        }
    }

    template<class F> ColumnPayload encode_strings(const Field& field,int64_t first,int64_t n,F get,Buffer<char>& scratch) {
        ColumnPayload cp{Chunk{field.physical,8,n},{}};
        page_into(cp,field.dict_bytes,field.labels.size(),true,scratch);cp.chunk.encoding=8;
        Bytes levels;varint(levels,uint64_t(n)*2);levels.push_back(1);Bytes raw;little32(raw,levels.size());raw.insert(raw.end(),levels.begin(),levels.end());
        int width=std::max(1,bit_width(field.labels.size()-1));
        dictionary_indices_into(raw,n,width,[&](int64_t i){return get(first+i);});
        page_into(cp,raw,int(n),false,scratch);return cp;
    }

    void write_payload(Group& group,ColumnPayload&& cp) {
        int64_t base_pos=output.position;
        if(cp.rel_dict_offset>=0) cp.chunk.dict_offset=base_pos+cp.rel_dict_offset;
        else cp.chunk.dict_offset=-1;
        cp.chunk.data_offset=base_pos+cp.rel_data_offset;
        output.write(cp.bytes.data(),cp.bytes.size());
        group.bytes+=cp.chunk.uncompressed;group.compressed+=cp.chunk.compressed;
        group.columns.push_back(cp.chunk);
    }

public:
    Writer(const fs::path& path,std::vector<Field> schema,bool parallel_mode=false):output(path),fields(std::move(schema)),parallel(parallel_mode) {
        output.write("PAR1",4);
        type_codes.reserve(16384);
        last_execution.resize(4096,SIZE_MAX);
    }

    template<class F> void append(int64_t n,F get) {
        if(n<=0) return;Group group{n};group.columns.reserve(fields.size());
        Buffer<char> scratch;
        for(size_t col=0;col<fields.size();++col) {
            const auto& field=fields[col];auto value=[&](int64_t i){return get(col,i);};
            ColumnPayload cp;
            if(field.physical==6) cp=encode_strings(field,0,n,value,scratch);
            else if(field.physical==1) cp=encode_numeric<int32_t>(field,0,n,value,scratch);
            else cp=encode_numeric<int64_t>(field,0,n,value,scratch);
            write_payload(group,std::move(cp));
        }
        groups.push_back(std::move(group));rows+=n;
    }

    void append_ledger(const LedgerRow* input,int64_t n,int64_t first) {
        if(n<=0) return;Group group{n};group.columns.reserve(10);
        std::vector<ColumnPayload> cols(10);
        auto encode_cols=[&](int start_c,int end_c) {
            Buffer<char> scratch;
            for(int c=start_c;c<end_c;++c) {
                switch(c) {
                    case 0: cols[0]=encode_numeric<int64_t>(fields[0],0,n,[&](int64_t i){return first+i;},scratch);break;
                    case 1: cols[1]=encode_numeric<int64_t>(fields[1],0,n,[&](int64_t i){return input[i].recv;},scratch);break;
                    case 2: cols[2]=encode_numeric<int64_t>(fields[2],0,n,[&](int64_t i){return input[i].send;},scratch);break;
                    case 3: cols[3]=encode_numeric<int64_t>(fields[3],0,n,[&](int64_t i){return input[i].latency;},scratch);break;
                    case 4: cols[4]=encode_numeric<int32_t>(fields[4],0,n,[&](int64_t i){return input[i].src;},scratch);break;
                    case 5: cols[5]=encode_numeric<int32_t>(fields[5],0,n,[&](int64_t i){return input[i].dst;},scratch);break;
                    case 6: cols[6]=encode_numeric<int64_t>(fields[6],0,n,[&](int64_t i){return input[i].id;},scratch);break;
                    case 7: cols[7]=encode_strings(fields[7],0,n,[&](int64_t i){return input[i].kind;},scratch);break;
                    case 8: cols[8]=encode_numeric<int64_t>(fields[8],0,n,[&](int64_t i){return input[i].order_id;},scratch);break;
                    case 9: cols[9]=encode_numeric<int64_t>(fields[9],0,n,[&](int64_t i){return input[i].parent;},scratch);break;
                }
            }
        };
        if(parallel&&n>=2048) {
            auto f1=std::async(std::launch::async,[&](){encode_cols(3,6);});
            auto f2=std::async(std::launch::async,[&](){encode_cols(6,8);});
            auto f3=std::async(std::launch::async,[&](){encode_cols(8,10);});
            encode_cols(0,3);
            f1.get();f2.get();f3.get();
        } else {
            encode_cols(0,10);
        }
        for(int c=0;c<10;++c) write_payload(group,std::move(cols[c]));
        groups.push_back(std::move(group));rows+=n;
    }

    void append_trace(const std::vector<TraceRow>& trace,int64_t first,int64_t n) {
        if(n<=0) return;deferred_types=true;Group group{n};group.columns.reserve(7);
        for(int64_t i=0;i<n;++i) {
            const auto& row=trace[first+i];int kind=row.kind;
            if(kind==EXEC) {
                if(row.order_id<0) throw std::runtime_error("execution has no order ID");
                if(__builtin_expect(size_t(row.order_id)>=last_execution.size(), 0))
                    last_execution.resize(std::max(size_t(row.order_id)*2+1, size_t(65536)),SIZE_MAX);
                size_t& previous=last_execution[size_t(row.order_id)];if(previous!=SIZE_MAX) type_codes[previous]=PARTIAL;
                previous=type_codes.size();kind=FILL;
            }
            type_codes.push_back(uint8_t(kind));
        }
        std::vector<ColumnPayload> cols(7);
        cols[2].chunk=Chunk{fields[2].physical,8,n};
        auto encode_trace_cols=[&](int start_c,int end_c) {
            Buffer<char> scratch;
            for(int c=start_c;c<end_c;++c) {
                switch(c) {
                    case 0: cols[0]=encode_numeric<int64_t>(fields[0],0,n,[&](int64_t i){return trace[first+i].time;},scratch);break;
                    case 1: cols[1]=encode_numeric<int32_t>(fields[1],0,n,[&](int64_t i){return trace[first+i].agent;},scratch);break;
                    case 3: cols[3]=encode_strings(fields[3],0,n,[&](int64_t i){return trace[first+i].side;},scratch);break;
                    case 4: cols[4]=encode_numeric<int64_t>(fields[4],0,n,[&](int64_t i){return trace[first+i].price;},scratch);break;
                    case 5: cols[5]=encode_numeric<int64_t>(fields[5],0,n,[&](int64_t i){return trace[first+i].size;},scratch);break;
                    case 6: cols[6]=encode_numeric<int64_t>(fields[6],0,n,[&](int64_t i){return trace[first+i].order_id;},scratch);break;
                }
            }
        };
        if(parallel&&n>=2048) {
            auto f1=std::async(std::launch::async,[&](){encode_trace_cols(3,5);});
            auto f2=std::async(std::launch::async,[&](){encode_trace_cols(5,7);});
            encode_trace_cols(0,2);
            f1.get();f2.get();
        } else {
            encode_trace_cols(0,2);
            encode_trace_cols(3,7);
        }
        for(int c=0;c<7;++c) {
            if(c==2) group.columns.push_back(cols[2].chunk);
            else write_payload(group,std::move(cols[c]));
        }
        groups.push_back(std::move(group));rows+=n;
    }

    std::string close() {
        if(deferred_types) {
            int64_t first=0;Buffer<char> scratch;
            for(auto& group:groups) {
                ColumnPayload cp=encode_strings(fields[2],0,group.n,[&](int64_t i){return type_codes[first+i];},scratch);
                int64_t base_pos=output.position;
                if(cp.rel_dict_offset>=0) cp.chunk.dict_offset=base_pos+cp.rel_dict_offset;
                else cp.chunk.dict_offset=-1;
                cp.chunk.data_offset=base_pos+cp.rel_data_offset;
                output.write(cp.bytes.data(),cp.bytes.size());
                group.columns[2]=cp.chunk;group.bytes+=cp.chunk.uncompressed;group.compressed+=cp.chunk.compressed;first+=group.n;
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
std::string trace(const Core& core,const fs::path& path,bool parallel=false) {
    Writer writer(path,trace_schema(),parallel);
    for(int64_t first=0;first<int64_t(core.trace.size());) {
        int64_t end=std::min(first+16384,int64_t(core.trace.size()));
        while(end<int64_t(core.trace.size())&&core.trace[end].time==core.trace[end-1].time) ++end;
        writer.append_trace(core.trace,first,end-first);first=end;
    }
    return writer.close();
}
void ledger_block(Writer& writer,const LedgerRow* input,int64_t n,int64_t first) {
    if(n<=0) return;
    writer.append_ledger(input,n,first);
}
std::string ledger(const Core& core,const fs::path& path,bool parallel=false) {
    Writer writer(path,ledger_schema(),parallel);int64_t group=65536;
    if(group<1||group>1048576) throw std::invalid_argument("invalid row group size");
    for(int64_t first=0;first<int64_t(core.ledger.size());first+=group) {
        int64_t n=std::min(group,int64_t(core.ledger.size())-first);
        ledger_block(writer,core.ledger.data()+first,n,first);
    }
    return writer.close();
}
void ledger_block(Writer& writer,const std::vector<LedgerRow>& rows,int64_t first) {
    ledger_block(writer,rows.data(),rows.size(),first);
}
}
