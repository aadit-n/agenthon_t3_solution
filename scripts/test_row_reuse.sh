#!/bin/sh
set -eu
g++ -O0 -g0 -std=c++17 -ffp-contract=off -pthread -fsanitize=address,undefined \
    -DT3_POOL_BOOK -DT3_RADIX_QUEUE -DT3_PRICE_CACHE -DT3_COMPACT_MESSAGES -DT3_PACKED_ROUTE \
    -DT3_COMPACT_TRACE -DT3_REUSE_ROW_BUFFERS -Wno-deprecated-declarations \
    -I/src /tests/row_reuse.cpp -lcrypto -lsnappy -o /tmp/row-reuse-tests
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 /tmp/row-reuse-tests /output/test-files
