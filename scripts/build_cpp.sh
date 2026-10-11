#!/bin/sh
set -eu
ARROW_DIR=/usr/local/lib/python3.11/site-packages/pyarrow
g++ -O3 -std=c++17 -ffp-contract=off -pthread \
    -I"$ARROW_DIR/include" /opt/native/main.cpp \
    -L"$ARROW_DIR" -Wl,-rpath,"$ARROW_DIR" \
    -Wl,--as-needed -lcrypto -l:libparquet.so.1500 -l:libarrow.so.1500 \
    -o /opt/t3sim-cpp
strip --strip-unneeded /opt/t3sim-cpp
