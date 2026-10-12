#!/bin/sh
set -eu
mkdir -p /tmp/v7-native /tmp/v7-build /tmp/v7-binaries
cp /workspace/native/* /tmp/v7-native/
cp /workspace/scripts/build_lite.sh /workspace/scripts/build_release.sh /workspace/scripts/pgo_scenarios.py /tmp/v7-build/
export T3_NATIVE_SOURCE=/tmp/v7-native T3_PGO_SCENARIOS=/tmp/v7-build/pgo_scenarios.py
g++ -O1 -g -std=c++17 -ffp-contract=off -pthread -fsanitize=address,undefined \
    -DT3_COMPACT_MESSAGES -DT3_PRICE_CACHE -DT3_PACKED_ROUTE -DT3_VECTOR_MT -DT3_BUFFERED_MT -DT3_DEQUE_PRICES \
    -I/tmp/v7-native /workspace/tests/price_book.cpp -o /tmp/v7-test
ASAN_OPTIONS=detect_leaks=1 /tmp/v7-test
for variant in ${T3_V7_VARIANTS:-static vector book open combined}; do
    case "$variant" in
        static) extra="-static -fno-pie -no-pie" ;;
        vector) extra="-DT3_VECTOR_MT" ;;
        book) extra="-DT3_FLAT_PRICE_BOOK" ;;
        open) extra="-DT3_TOMBSTONE_OPEN" ;;
        combined) extra="-static -fno-pie -no-pie -DT3_VECTOR_MT -DT3_FLAT_PRICE_BOOK -DT3_TOMBSTONE_OPEN" ;;
        buffered) extra="-static -fno-pie -no-pie -DT3_VECTOR_MT -DT3_BUFFERED_MT" ;;
        buffered-combined) extra="-static -fno-pie -no-pie -DT3_VECTOR_MT -DT3_BUFFERED_MT -DT3_FLAT_PRICE_BOOK -DT3_TOMBSTONE_OPEN" ;;
        cache10) extra="-static -fno-pie -no-pie -DT3_PRICE_CACHE_BITS=10" ;;
        cache12) extra="-static -fno-pie -no-pie -DT3_PRICE_CACHE_BITS=12" ;;
        compact) extra="-static -fno-pie -no-pie -DT3_COMPACT_TRACE" ;;
        compact-buffered) extra="-static -fno-pie -no-pie -DT3_COMPACT_TRACE -DT3_VECTOR_MT -DT3_BUFFERED_MT" ;;
        oracle) extra="-static -fno-pie -no-pie -DT3_ORACLE_CACHE" ;;
        oracle-compact) extra="-static -fno-pie -no-pie -DT3_ORACLE_CACHE -DT3_COMPACT_TRACE" ;;
        deque) extra="-static -fno-pie -no-pie -DT3_FLAT_PRICE_BOOK -DT3_DEQUE_PRICES" ;;
        deque-open) extra="-static -fno-pie -no-pie -DT3_FLAT_PRICE_BOOK -DT3_DEQUE_PRICES -DT3_TOMBSTONE_OPEN" ;;
        sort) extra="-static -fno-pie -no-pie -DT3_COMPACT_TRACE -DT3_FAST_TRACE_SORT" ;;
        sort-ring) extra="-static -fno-pie -no-pie -DT3_COMPACT_TRACE -DT3_FAST_TRACE_SORT -DT3_MOMENTUM_RING" ;;
        cpu-v3) extra="-march=x86-64-v3 -mtune=sapphirerapids" ;;
        cpu-v3-generic) extra="-march=x86-64-v3 -mtune=generic" ;;
    esac
    echo "Building $variant"
    T3_EXTRA_CXX_FLAGS="$extra" T3_BUILD_OUTPUT="/tmp/v7-binaries/$variant" \
        T3_PROFILE_DIR="/tmp/v7-profile-$variant" T3_BUILD_PGO=1 sh /tmp/v7-build/build_release.sh
    sha256sum "/tmp/v7-binaries/$variant"
done
