#!/bin/sh
set -eu
export T3_NATIVE_SOURCE=/src
export T3_BUILD_OUTPUT=/output/binaries/t3sim-noise
export T3_PGO_SCENARIOS=/scripts/pgo_scenarios.py
export T3_PROFILE_DIR=/tmp/profile-noise
export T3_EXTRA_CXX_FLAGS='-DT3_RADIX_QUEUE -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND -DT3_PREPARED_RANDOM -DT3_DIRECT_HASH -DT3_NARROW_CODES -DT3_IMPLICIT_SEQUENCE -DT3_PRICE_CACHE -Wno-deprecated-declarations'
export T3_BUILD_PGO=1
sh /scripts/build_lite.sh > /output/profile-noise-build.log 2>&1
python /scripts/tune_v5.py --variant v4=/output/binaries/t3sim-v4 --variant refined=/output/binaries/t3sim-refined --variant noise=/output/binaries/t3sim-noise --env noise:T3SIM_NOISE_PREFETCH=auto --variant forced=/output/binaries/t3sim-noise --env forced:T3SIM_NOISE_PREFETCH=1 --runs 9 --out /output/noise/comparison.json
