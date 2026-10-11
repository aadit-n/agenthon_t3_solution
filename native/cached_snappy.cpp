// Reuse scratch storage through the pinned Snappy 1.2.2 internal interface.
// The library still performs every actual compression operation.
#include <snappy-internal.h>
#include <algorithm>
#include <cstdint>
#ifndef SNAPPY_CACHE_NAME
#define SNAPPY_CACHE_NAME cached_snappy
#endif
void SNAPPY_CACHE_NAME(const char* input,size_t size,char* output,size_t* compressed_size) {
    thread_local snappy::internal::WorkingMemory scratch(65536);
    char* cursor=output;uint32_t length=uint32_t(size);
    while(length>=128) {*cursor++=char(uint8_t(length)|128);length>>=7;}
    *cursor++=char(length);
    for(size_t first=0;first<size;first+=65536) {
        size_t count=std::min(size_t(65536),size-first);int table_size;
        auto* table=scratch.GetHashTable(count,&table_size);
        cursor=snappy::internal::CompressFragment(input+first,count,cursor,table,table_size);
    }
    *compressed_size=size_t(cursor-output);
}
