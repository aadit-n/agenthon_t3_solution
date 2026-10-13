#!/bin/sh
set -eu
mkdir -p /tmp/execution-native /tmp/execution-build /output/binaries
cp /src/* /tmp/execution-native/
cp /scripts/build_lite.sh /scripts/build_release.sh /scripts/pgo_scenarios.py /scripts/pgo_specialized.py /tmp/execution-build/
g++ -O1 -g -std=c++17 -pthread -fsanitize=address,undefined -DT3_COMPACT_MESSAGES -DT3_PACKED_ROUTE \
    -I/tmp/execution-native /tests/narrow_scheduler.cpp -o /tmp/narrow-tests
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/narrow-tests
export T3_NATIVE_SOURCE=/tmp/execution-native T3_PGO_SCENARIOS=/tmp/execution-build/pgo_specialized.py
for variant in ${T3_EXECUTION_VARIANTS:-narrow narrow-latency narrow-combined}; do
    case "$variant" in
        narrow) extra='-DT3_NARROW_SCHEDULED' ;;
        narrow-latency) extra='-DT3_NARROW_SCHEDULED -DT3_SPECIALIZE_LATENCY' ;;
        narrow-combined) extra='-DT3_NARROW_SCHEDULED -DT3_SPECIALIZE_LATENCY -DT3_SPECIALIZE_PROTOCOL' ;;
        *) exit 1 ;;
    esac
    T3_EXTRA_CXX_FLAGS="$extra" T3_BUILD_OUTPUT="/output/binaries/$variant" \
        T3_PROFILE_DIR="/tmp/execution-profile-$variant" T3_BUILD_PGO=1 sh /tmp/execution-build/build_release.sh
    sha256sum "/output/binaries/$variant"
done
