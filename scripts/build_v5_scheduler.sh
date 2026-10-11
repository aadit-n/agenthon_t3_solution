#!/bin/sh
set -eu
FLAGS='-O3 -std=c++17 -ffp-contract=off -pthread -DT3_POOL_BOOK -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND -DT3_PREPARED_RANDOM -DT3_DIRECT_HASH -DT3_NARROW_CODES -DT3_IMPLICIT_SEQUENCE -DT3_PRICE_CACHE -Wno-deprecated-declarations'
g++ $FLAGS -DT3_RADIX_QUEUE -I/src /src/main_lite.cpp -lcrypto -lsnappy -o /output/binaries/t3sim-refined-control
g++ $FLAGS -DT3_RECIPIENT_QUEUE -I/src /src/main_lite.cpp -lcrypto -lsnappy -o /output/binaries/t3sim-recipient
python /scripts/tune_v5.py --variant v4=/output/binaries/t3sim-v4 --variant refined=/output/binaries/t3sim-refined-control --variant recipient=/output/binaries/t3sim-recipient --variant vector=/output/binaries/t3sim-vector --runs 9 --out /output/scheduler/comparison.json
