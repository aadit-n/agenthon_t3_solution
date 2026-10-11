#!/bin/sh
set -eu
# Test sources are copied to /tmp before this script runs.
FLAGS='-std=c++17 -O0 -g0 -ffp-contract=off -fno-math-errno -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -DT3_POOL_BOOK -DT3_RADIX_QUEUE -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND -DT3_PREPARED_RANDOM -DT3_DIRECT_HASH -DT3_NARROW_CODES -DT3_IMPLICIT_SEQUENCE -DT3_PRICE_CACHE -DT3_PACKED_ROUTE -DT3_UNINITIALIZED_BYTES -DT3_FUSED_DELTA -Wno-deprecated-declarations'
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
g++ $FLAGS -I/src /tmp/native_structures.cpp -o /tmp/structures-v5
/tmp/structures-v5 > /output/structures-release.txt
cat /output/structures-release.txt
g++ $FLAGS -I/src /tmp/native_streams.cpp -o /tmp/streams-v5
timeout 120 /tmp/streams-v5 > /output/streams-release.txt
cat /output/streams-release.txt
g++ $FLAGS -I/src /tmp/parquet_codec.cpp -lcrypto -lsnappy -o /tmp/codec-v5
/tmp/codec-v5 /output/codec > /output/codec-sanitizers.txt
cat /output/codec-sanitizers.txt
