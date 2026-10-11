#pragma once
#include <parquet/column_writer.h>
#include <parquet/column_page.h>
#include <parquet/encoding.h>
#include <parquet/metadata.h>

// Flat public columns only. Arrow's pinned encoders, Snappy compressor, page
// serializer and Thrift metadata builder retain standard Parquet compatibility.
// The simulator already knows its categorical codes: avoid hashing the same
// strings and converting definition levels through the general nested writer.
namespace fastpq {
using Bytes=std::vector<uint8_t>;
void varint(Bytes& out,uint64_t v) {
    while(v>=128) {out.push_back(uint8_t(v)|128);v>>=7;}out.push_back(uint8_t(v));
}
void little32(Bytes& out,uint32_t v) {
    for(int b=0;b<4;++b) out.push_back(uint8_t(v>>(8*b)));
}
std::shared_ptr<arrow::Buffer> buffer(const Bytes& bytes) {
    auto out=checked(arrow::AllocateBuffer(bytes.size()));
    std::memcpy(out->mutable_data(),bytes.data(),bytes.size());return out;
}
Bytes definitions(const arrow::Array& array,int64_t first,int64_t n) {
    Bytes out;out.reserve(size_t(n/8+16));
    if(!array.null_count()) {varint(out,uint64_t(n)*2);out.push_back(1);}
    else {
        int64_t groups=(n+7)/8;varint(out,uint64_t(groups)*2+1);
        int64_t bit=array.offset()+first;
        const uint8_t* bitmap=array.null_bitmap_data();
        if(bit%8==0) out.insert(out.end(),bitmap+bit/8,bitmap+bit/8+groups);
        else for(int64_t i=0;i<groups;++i) {
            uint8_t byte=0;for(int j=0;j<8&&i*8+j<n;++j) if(array.IsValid(first+i*8+j)) byte|=uint8_t(1<<j);
            out.push_back(byte);
        }
        // Readers stop at the real row count; clear padding for byte stability.
        if(n%8) out.back()&=uint8_t((1<<(n%8))-1);
    }
    return out;
}
std::shared_ptr<arrow::Buffer> page_buffer(const Bytes& levels,const std::shared_ptr<arrow::Buffer>& values) {
    auto out=checked(arrow::AllocateBuffer(4+levels.size()+values->size()));
    uint32_t length=levels.size();auto* dst=out->mutable_data();
    for(int b=0;b<4;++b) dst[b]=uint8_t(length>>(8*b));
    std::memcpy(dst+4,levels.data(),levels.size());
    std::memcpy(dst+4+levels.size(),values->data(),values->size());return out;
}
int64_t data_page(parquet::PageWriter& writer,const std::shared_ptr<arrow::Buffer>& raw,
                  int n,parquet::Encoding::type encoding) {
    std::shared_ptr<arrow::Buffer> data=raw;
    if(writer.has_compressor()) {
        auto compressed=checked(arrow::AllocateResizableBuffer(0));
        writer.Compress(*raw,compressed.get());data=std::move(compressed);
    }
    return writer.WriteDataPage(parquet::DataPageV1(data,n,encoding,
        parquet::Encoding::RLE,parquet::Encoding::RLE,raw->size()));
}
template<class F> Bytes packed_codes(int64_t n,int width,F get) {
    Bytes indices;indices.reserve(size_t((n+7)/8*width+16));indices.push_back(uint8_t(width));
    int64_t groups=(n+7)/8;varint(indices,uint64_t(groups)*2+1);
    for(int64_t group=0;group<groups;++group) {
        __uint128_t packed=0;
        for(int j=0;j<8;++j) {int64_t i=group*8+j;if(i<n) packed|=__uint128_t(get(i))<<(j*width);}
        for(int j=0;j<width;++j) indices.push_back(uint8_t(packed>>(j*8)));
    }
    return indices;
}
template<class DType,class AType>
std::pair<int64_t,bool> numeric(parquet::PageWriter& writer,const parquet::ColumnDescriptor* descriptor,
                const AType& array,int64_t first,int64_t n) {
    const auto& name=descriptor->name();
    bool categorical=name=="price"||name=="size"||name=="agent_id"||name=="src_id"||name=="dst_id";
    if(env("T3SIM_NUMERIC_DICT","0")=="1"&&categorical&&!array.null_count()&&n) {
        using T=typename DType::c_type;const T* values=array.raw_values()+first;
        T minimum=values[0],maximum=values[0];
        for(int64_t i=1;i<n;++i) {minimum=std::min(minimum,values[i]);maximum=std::max(maximum,values[i]);}
        uint64_t range=uint64_t(maximum)-uint64_t(minimum);
        if(range<4096&&range+1<uint64_t(n)) {
            Bytes dict;dict.resize((range+1)*sizeof(T));
            for(uint64_t i=0;i<=range;++i) {T value=minimum+T(i);std::memcpy(dict.data()+i*sizeof(T),&value,sizeof(T));}
            int64_t bytes=writer.WriteDictionaryPage(parquet::DictionaryPage(buffer(dict),int(range+1),parquet::Encoding::PLAIN));
            int width=1;while((uint64_t(1)<<width)<=range) ++width;
            auto indices=packed_codes(n,width,[&](int64_t i){return uint64_t(values[i])-uint64_t(minimum);});
            bytes+=data_page(writer,page_buffer(definitions(array,first,n),buffer(indices)),int(n),parquet::Encoding::RLE_DICTIONARY);
            return {bytes,true};
        }
    }
    auto encoding=env("T3SIM_PAGE_NUMERIC","delta")=="plain"?parquet::Encoding::PLAIN:parquet::Encoding::DELTA_BINARY_PACKED;
    auto encoder=parquet::MakeTypedEncoder<DType>(encoding,false,descriptor);
    if(!array.null_count()) encoder->Put(array.raw_values()+first,int(n));
    else {
        std::vector<typename DType::c_type> dense;dense.reserve(n);
        for(int64_t i=0;i<n;++i) if(array.IsValid(first+i)) dense.push_back(array.Value(first+i));
        encoder->Put(dense.data(),int(dense.size()));
    }
    auto values=encoder->FlushValues();
    return {data_page(writer,page_buffer(definitions(array,first,n),values),int(n),encoding),false};
}
int64_t category(parquet::PageWriter& writer,const arrow::Array& source,int64_t first,int64_t n) {
    const auto& array=static_cast<const arrow::DictionaryArray&>(source);
    const auto& names=static_cast<const arrow::StringArray&>(*array.dictionary());
    const auto& codes=static_cast<const arrow::Int8Array&>(*array.indices());
    Bytes dict;
    for(int64_t i=0;i<names.length();++i) {
        auto value=names.GetView(i);little32(dict,value.size());
        dict.insert(dict.end(),value.begin(),value.end());
    }
    int64_t bytes=writer.WriteDictionaryPage(parquet::DictionaryPage(buffer(dict),int(names.length()),parquet::Encoding::PLAIN));
    int width=1;while((uint64_t(1)<<width)<uint64_t(names.length())) ++width;
    // A single bit-packed run, padded only at the end of this page. Each eight
    // codes fit in <= 32 bits for this contract. Padding is not counted as rows.
    auto indices=packed_codes(n,width,[&](int64_t i){return codes.Value(first+i);});
    bytes+=data_page(writer,page_buffer(definitions(source,first,n),buffer(indices)),int(n),parquet::Encoding::RLE_DICTIONARY);
    return bytes;
}
std::string write(const std::shared_ptr<arrow::Table>& table,const fs::path& path) {
    parquet::WriterProperties::Builder props;
    props.version(parquet::ParquetVersion::PARQUET_2_6);props.compression(parquet::Compression::SNAPPY);props.disable_statistics();
    auto properties=props.build();auto arrow_options=parquet::ArrowWriterProperties::Builder().build();
    std::shared_ptr<parquet::SchemaDescriptor> descriptor;
    checked(parquet::arrow::ToParquetSchema(table->schema().get(),*properties,*arrow_options,&descriptor));
    auto metadata=parquet::FileMetaDataBuilder::Make(descriptor.get(),properties);
    auto output=std::make_shared<HashingOutput>(path);checked(output->Write("PAR1",4));
    int64_t group_size=std::stoll(env("T3SIM_PAGE_ROWS","262144"));
    if(group_size<1||group_size>1048576) throw std::invalid_argument("page rows must be 1..1048576");
    for(int64_t first=0;first<table->num_rows();first+=group_size) {
        auto* group=metadata->AppendRowGroup();int64_t n=std::min(group_size,table->num_rows()-first),bytes=0;
        group->set_num_rows(n);
        for(int col=0;col<table->num_columns();++col) {
            const auto& array=*table->column(col)->chunk(0);auto* chunk=group->NextColumnChunk();
            auto pager=parquet::PageWriter::Open(output,parquet::Compression::SNAPPY,chunk);
            bool dictionary=false;std::pair<int64_t,bool> result;
            if(array.type_id()==arrow::Type::INT64) {result=numeric<parquet::Int64Type>(*pager,descriptor->Column(col),static_cast<const arrow::Int64Array&>(array),first,n);bytes+=result.first;dictionary=result.second;}
            else if(array.type_id()==arrow::Type::INT32) {result=numeric<parquet::Int32Type>(*pager,descriptor->Column(col),static_cast<const arrow::Int32Array&>(array),first,n);bytes+=result.first;dictionary=result.second;}
            else if(array.type_id()==arrow::Type::DICTIONARY) {bytes+=category(*pager,array,first,n);dictionary=true;}
            else throw std::invalid_argument("direct pages require numeric/dictionary columns");
            pager->Close(dictionary,false);
        }
        group->Finish(bytes);
    }
    auto footer=metadata->Finish(table->schema()->metadata());std::string thrift=footer->SerializeToString();
    checked(output->Write(thrift.data(),thrift.size()));
    Bytes tail;little32(tail,uint32_t(thrift.size()));tail.insert(tail.end(),{'P','A','R','1'});
    checked(output->Write(tail.data(),tail.size()));checked(output->Close());return output->digest();
}
}
