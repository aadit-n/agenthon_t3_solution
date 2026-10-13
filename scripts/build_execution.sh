#!/bin/sh
set -eu
mkdir -p /tmp/execution-native /tmp/execution-build /output/binaries
cp /src/* /tmp/execution-native/
cp /scripts/build_lite.sh /scripts/build_release.sh /scripts/pgo_scenarios.py /scripts/pgo_specialized.py /tmp/execution-build/
export T3_NATIVE_SOURCE=/tmp/execution-native T3_PGO_SCENARIOS=/tmp/execution-build/pgo_specialized.py
g++ -O0 -g0 -std=c++17 -ffp-contract=off -fno-math-errno -pthread -fsanitize=address,undefined \
    -DT3_POOL_BOOK -DT3_RADIX_QUEUE -DT3_PRICE_CACHE -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND \
    -DT3_PACKED_ROUTE -DT3_COMPACT_TRACE -DT3_PREPARED_RANDOM -DT3_SPECIALIZE_LATENCY -DT3_SPECIALIZE_PROTOCOL \
    -I/tmp/execution-native /tests/execution_modes.cpp -o /tmp/execution-tests
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/execution-tests
for variant in ${T3_EXECUTION_VARIANTS:-control lazy protocol latency combined}; do
    case "$variant" in
        control) extra='' ;;
        lazy) extra='-DT3_LAZY_STP_FRONT' ;;
        protocol) extra='-DT3_SPECIALIZE_PROTOCOL' ;;
        latency) extra='-DT3_SPECIALIZE_LATENCY' ;;
        combined) extra='-DT3_SPECIALIZE_LATENCY -DT3_SPECIALIZE_PROTOCOL' ;;
        *) echo "Unknown variant: $variant" >&2;exit 1 ;;
    esac
    T3_EXTRA_CXX_FLAGS="$extra" T3_BUILD_OUTPUT="/output/binaries/$variant" \
        T3_PROFILE_DIR="/tmp/execution-profile-$variant" T3_BUILD_PGO=1 sh /tmp/execution-build/build_release.sh
    sha256sum "/output/binaries/$variant"
done
