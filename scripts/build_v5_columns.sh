#!/bin/sh
set -eu
export T3_NATIVE_SOURCE=/src T3_PGO_SCENARIOS=/scripts/pgo_scenarios.py T3_BUILD_PGO=1
export T3_BUILD_OUTPUT=/output/binaries/t3sim-columns T3_PROFILE_DIR=/tmp/profile-columns
export T3_EXTRA_CXX_FLAGS='-DT3_RADIX_QUEUE -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND -DT3_PREPARED_RANDOM -DT3_DIRECT_HASH -DT3_NARROW_CODES -DT3_IMPLICIT_SEQUENCE -DT3_PRICE_CACHE -DT3_PACKED_ROUTE -DT3_COLUMNAR_WRITER -fno-math-errno -Wno-deprecated-declarations'
sh /scripts/build_lite.sh > /output/profile-columns.log 2>&1
python /scripts/tune_v5.py --variant v4=/output/binaries/t3sim-v4 --variant fast=/output/binaries/t3sim-final-fast --variant buffers=/output/binaries/t3sim-final-buffers --variant columns=/output/binaries/t3sim-columns --runs 9 --out /output/columns/comparison.json
