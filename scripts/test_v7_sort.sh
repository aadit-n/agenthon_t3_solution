#!/bin/sh
set -eu
g++ -O1 -g -std=c++17 -pthread -ffp-contract=off -fsanitize=address,undefined \
    -I/workspace/native /workspace/tests/trace_sort.cpp -o /tmp/v7-sort-test
ASAN_OPTIONS=detect_leaks=1 /tmp/v7-sort-test
