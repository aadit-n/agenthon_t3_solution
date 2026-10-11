#include "simulation.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
namespace fs=std::filesystem;
using namespace t3;
std::string env(const char* key,const char* fallback) {const char* value=std::getenv(key);return value?value:fallback;}
#include "parquet_lite.hpp"
int main(int argc,char** argv) {
    fs::path out=argv[1];fs::create_directories(out);std::mt19937_64 rng(151175);
    std::vector<int> counts={0,1,2,7,8,31,32,33,127,128,129,130,257,65535,65536,65537,262144,262145};
    std::vector<litepq::Field> fields={{"x64",2},{"x32",1},{"t_send_ns",2,true},
        {"msg_type",6,false,false,{"A","BB","CCC","\xCE\x94","quote"}},{"order_id",2,true},{"causal_parent",2,true}};
    Json manifest=Json::array();
    for(int n:counts) {
        std::vector<i64> values(size_t(n)*6);
        for(int i=0;i<n;++i) {
            uint64_t bits=rng();std::memcpy(&values[size_t(i)*6],&bits,8);
            if(i%11==0) values[size_t(i)*6]=i%22?INT64_MIN:INT64_MAX;
            values[size_t(i)*6+1]=int32_t(rng());
            values[size_t(i)*6+2]=i%3?1612483200000000000LL+i*131:-1;
            values[size_t(i)*6+3]=rng()%5;
            values[size_t(i)*6+4]=n==129||i%5==0?-1:i*255;
            values[size_t(i)*6+5]=i%4?-1:i/4;
        }
        std::string stem=std::to_string(n);litepq::Writer writer(out/(stem+".parquet"),fields);
        for(int first=0;first<n;first+=65536) {
            int count=std::min(65536,n-first);writer.append(count,[&](size_t col,int64_t i){return values[size_t(first+i)*6+col];});
        }
        auto hash=writer.close();std::ofstream binary(out/(stem+".bin"),std::ios::binary);binary.write(reinterpret_cast<const char*>(values.data()),values.size()*8);
        manifest.push_back({{"rows",n},{"sha256",hash},{"file",stem+".parquet"}});
    }
    std::ofstream manifest_file(out/"codec.json");manifest_file<<manifest.dump(2)<<'\n';
    std::cout<<counts.size()<<" encoding edge cases generated\n";
}
