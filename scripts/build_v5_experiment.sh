#!/bin/sh
set -eu
export T3_NATIVE_SOURCE=/src
export T3_BUILD_OUTPUT=/output/binaries/t3sim-refined
export T3_PGO_SCENARIOS=/scripts/pgo_scenarios.py
export T3_PROFILE_DIR=/tmp/profile-refined
export T3_EXTRA_CXX_FLAGS='-DT3_RADIX_QUEUE -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND -DT3_PREPARED_RANDOM -DT3_DIRECT_HASH -DT3_NARROW_CODES -DT3_IMPLICIT_SEQUENCE -DT3_PRICE_CACHE -Wno-deprecated-declarations'
export T3_BUILD_PGO=1
sh /scripts/build_lite.sh > /output/profile-refined-build.log 2>&1
g++ -O3 -std=c++17 -ffp-contract=off -pthread -DT3_POOL_BOOK $T3_EXTRA_CXX_FLAGS -DT3_VECTOR_DELTA -I/src /src/main_lite.cpp -lcrypto -lsnappy -o /output/binaries/t3sim-vector
python /scripts/tune_v5.py --variant v4=/output/binaries/t3sim-v4 --variant next=/output/binaries/t3sim-next --variant refined=/output/binaries/t3sim-refined --variant vector=/output/binaries/t3sim-vector --runs 9 --out /output/refined/comparison.json
