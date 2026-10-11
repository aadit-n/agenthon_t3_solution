#!/bin/sh
set -eu
g++ -O3 -std=c++17 -I/tmp/snappy -c /src/cached_snappy.cpp -o /output/binaries/snappy-cache.o
g++ -O3 -std=c++17 -mavx2 -mbmi2 -msse4.2 -Dsnappy=snappy_fast -DSNAPPY_CACHE_NAME=cached_snappy_fast -I/tmp/snappy -c /src/cached_snappy.cpp -o /output/binaries/snappy-cache-fast.o
FLAGS='-O3 -std=c++17 -ffp-contract=off -pthread -DT3_POOL_BOOK -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND -DT3_PREPARED_RANDOM -DT3_DIRECT_HASH -DT3_NARROW_CODES -DT3_IMPLICIT_SEQUENCE -DT3_PRICE_CACHE -DT3_RADIX_QUEUE -Wno-deprecated-declarations'
g++ $FLAGS -DT3_FAST_SNAPPY -DT3_CACHED_SNAPPY -I/src /src/main_lite.cpp /output/binaries/snappy-cache.o /output/binaries/snappy-cache-fast.o /output/binaries/libsnappy-fast.a -lcrypto -lsnappy -o /output/binaries/t3sim-cached-snappy
python /scripts/tune_v5.py --variant v4=/output/binaries/t3sim-v4 --variant refined=/output/binaries/t3sim-refined-control --variant cached=/output/binaries/t3sim-cached-snappy --variant raw=/output/binaries/t3sim-cached-snappy --env raw:T3SIM_CACHE_SNAPPY=0 --variant portable=/output/binaries/t3sim-cached-snappy --env portable:T3SIM_FAST_SNAPPY=off --runs 9 --out /output/cached/comparison.json
