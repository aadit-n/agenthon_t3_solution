#!/bin/sh
set -eu
sh /scripts/build_fast_snappy.sh /tmp/snappy /output/binaries/libsnappy-fast.a
FLAGS='-O3 -std=c++17 -ffp-contract=off -pthread -DT3_POOL_BOOK -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND -DT3_PREPARED_RANDOM -DT3_DIRECT_HASH -DT3_NARROW_CODES -DT3_IMPLICIT_SEQUENCE -DT3_PRICE_CACHE -DT3_RADIX_QUEUE -Wno-deprecated-declarations'
g++ $FLAGS -DT3_FAST_SNAPPY -I/src /src/main_lite.cpp /output/binaries/libsnappy-fast.a -lcrypto -lsnappy -o /output/binaries/t3sim-fast-snappy
python /scripts/tune_v5.py --variant v4=/output/binaries/t3sim-v4 --variant refined=/output/binaries/t3sim-refined-control --variant fast=/output/binaries/t3sim-fast-snappy --variant portable=/output/binaries/t3sim-fast-snappy --env portable:T3SIM_FAST_SNAPPY=off --runs 9 --out /output/snappy/comparison.json
