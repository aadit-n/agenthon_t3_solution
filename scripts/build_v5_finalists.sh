#!/bin/sh
set -eu
export T3_NATIVE_SOURCE=/src T3_PGO_SCENARIOS=/scripts/pgo_scenarios.py T3_BUILD_PGO=1
COMMON='-DT3_RADIX_QUEUE -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND -DT3_PREPARED_RANDOM -DT3_DIRECT_HASH -DT3_NARROW_CODES -DT3_IMPLICIT_SEQUENCE -DT3_PRICE_CACHE -DT3_PACKED_ROUTE -fno-math-errno -Wno-deprecated-declarations'
for mode in fast buffers pool; do
    export T3_BUILD_OUTPUT="/output/binaries/t3sim-final-$mode"
    export T3_PROFILE_DIR="/tmp/profile-final-$mode"
    export T3_EXTRA_CXX_FLAGS="$COMMON"
    if [ "$mode" = buffers ]; then export T3_EXTRA_CXX_FLAGS="$COMMON -DT3_UNINITIALIZED_BYTES -DT3_FUSED_DELTA"; fi
    if [ "$mode" = pool ]; then export T3_EXTRA_CXX_FLAGS="$COMMON -DT3_REF_MESSAGES"; fi
    sh /scripts/build_lite.sh > "/output/profile-final-$mode.log" 2>&1
    echo "Built $mode"
done
python /scripts/tune_v5.py --variant v4=/output/binaries/t3sim-v4 --variant fast=/output/binaries/t3sim-final-fast --variant buffers=/output/binaries/t3sim-final-buffers --variant pool=/output/binaries/t3sim-final-pool --runs 5 --all-units --out /output/finalists/comparison.json
