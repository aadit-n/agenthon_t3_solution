#!/bin/sh
set -eu
ARROW=/usr/local/lib/python3.11/site-packages/pyarrow
for mode in pool heap both; do
    flags=
    case "$mode" in
        pool) flags=-DT3_POOL_BOOK ;;
        heap) flags=-DT3_FOUR_HEAP ;;
        both) flags="-DT3_POOL_BOOK -DT3_FOUR_HEAP" ;;
    esac
    g++ -O3 -std=c++17 -ffp-contract=off -pthread $flags \
        -I"$ARROW/include" /opt/native/main.cpp \
        -L"$ARROW" -Wl,-rpath,"$ARROW" -Wl,--as-needed \
        -lcrypto -l:libparquet.so.1500 -l:libarrow.so.1500 -o "/output/t3sim-$mode"
done
