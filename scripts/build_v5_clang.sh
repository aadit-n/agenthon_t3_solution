#!/bin/sh
set -eu
FLAGS='-O3 -std=c++17 -ffp-contract=off -pthread -DT3_POOL_BOOK -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND -DT3_PREPARED_RANDOM -DT3_DIRECT_HASH -DT3_NARROW_CODES -DT3_IMPLICIT_SEQUENCE -DT3_PRICE_CACHE -DT3_RADIX_QUEUE -Wno-deprecated-declarations'
clang++ $FLAGS -I/src /src/main_lite.cpp -lcrypto -lsnappy -o /output/binaries/t3sim-clang
clang++ $FLAGS -DT3_REF_MESSAGES -DT3_PACKED_ROUTE -I/src /src/main_lite.cpp -lcrypto -lsnappy -o /output/binaries/t3sim-clang-pool
python /scripts/tune_v5.py --variant v4=/output/binaries/t3sim-v4 --variant gcc=/output/binaries/t3sim-pool-control --variant gcc-pool=/output/binaries/t3sim-ref-pool --variant clang=/output/binaries/t3sim-clang --variant clang-pool=/output/binaries/t3sim-clang-pool --runs 11 --out /output/clang/comparison.json
