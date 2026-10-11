#!/bin/sh
set -eu
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
# -O0 keeps the sanitizer compiler's peak memory low; release flags select the
# same encodings, buffers and checksum paths as the optimized submission.
g++ -std=c++17 -O0 -g0 -ffp-contract=off -pthread -fsanitize=address,undefined -fno-omit-frame-pointer -DT3_POOL_BOOK -DT3_RADIX_QUEUE -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND -DT3_PREPARED_RANDOM -DT3_DIRECT_HASH -DT3_NARROW_CODES -DT3_IMPLICIT_SEQUENCE -DT3_PRICE_CACHE -DT3_PACKED_ROUTE -DT3_UNINITIALIZED_BYTES -DT3_FUSED_DELTA -Wno-deprecated-declarations -I/src /tmp/parquet_codec.cpp -lcrypto -lsnappy -o /tmp/codec-v5
/tmp/codec-v5 /output/codec > /output/codec-sanitizers.txt
cat /output/codec-sanitizers.txt
