#!/bin/sh
set -eu
FLAGS='-O3 -std=c++17 -ffp-contract=off -pthread -DT3_POOL_BOOK -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND -DT3_PREPARED_RANDOM -DT3_DIRECT_HASH -DT3_NARROW_CODES -DT3_IMPLICIT_SEQUENCE -DT3_PRICE_CACHE -DT3_RADIX_QUEUE -Wno-deprecated-declarations'
g++ $FLAGS -I/src /src/main_lite.cpp -lcrypto -lsnappy -o /output/binaries/t3sim-buffer-control
g++ $FLAGS -DT3_UNINITIALIZED_BYTES -DT3_FUSED_DELTA -I/src /src/main_lite.cpp -lcrypto -lsnappy -o /output/binaries/t3sim-buffers
python /scripts/tune_v5.py --variant v4=/output/binaries/t3sim-v4 --variant control=/output/binaries/t3sim-buffer-control --variant buffers=/output/binaries/t3sim-buffers --runs 11 --out /output/buffers/comparison.json
