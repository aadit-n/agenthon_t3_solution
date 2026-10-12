#!/bin/sh
set -eu
CXX_FLAGS="-O3 -std=c++17 -ffp-contract=off -pthread -DT3_POOL_BOOK ${T3_EXTRA_CXX_FLAGS:-}"
NATIVE_SOURCE="${T3_NATIVE_SOURCE:-/opt/native}"
BUILD_OUTPUT="${T3_BUILD_OUTPUT:-/opt/t3sim-cpp}"
PROFILE_DIR="${T3_PROFILE_DIR:-/tmp/compiler-profile}"
if [ "${T3_BUILD_PGO:-0}" = 1 ]; then
    mkdir -p "$PROFILE_DIR"
    g++ $CXX_FLAGS -flto=auto -fprofile-generate="$PROFILE_DIR" -fprofile-update=atomic \
        "$NATIVE_SOURCE/main_lite.cpp" -Wl,--as-needed -lcrypto -lsnappy -l:libsimdjson_static.a -o "$BUILD_OUTPUT"
    python "${T3_PGO_SCENARIOS:-/tmp/pgo_scenarios.py}" /tmp/compiler-input
    T3SIM_STREAM_LEDGER=1 T3SIM_STREAM_TRACE=auto T3SIM_LATENCY_PREFETCH=auto "$BUILD_OUTPUT" simulate-batch --batch-dir /tmp/compiler-input --out-dir /tmp/compiler-small --workers 1
    T3SIM_STREAM_LEDGER=1 T3SIM_STREAM_TRACE=1 T3SIM_LATENCY_PREFETCH=auto "$BUILD_OUTPUT" simulate-batch --batch-dir /tmp/compiler-input --out-dir /tmp/compiler-output --workers 1
    "$BUILD_OUTPUT" simulate-batch --batch-dir /tmp/compiler-input --out-dir /tmp/compiler-batch --workers 4
    "$BUILD_OUTPUT" simulate --config /tmp/compiler-input/00.json --out /tmp/compiler-single/trace.parquet
    "$BUILD_OUTPUT" simulate --config /tmp/compiler-input/10.json --out /tmp/compiler-single10/trace.parquet
    g++ $CXX_FLAGS -flto=auto -fprofile-use="$PROFILE_DIR" -fprofile-correction -Werror=missing-profile \
        "$NATIVE_SOURCE/main_lite.cpp" -Wl,--as-needed -lcrypto -lsnappy -l:libsimdjson_static.a -o "$BUILD_OUTPUT"
else
    g++ $CXX_FLAGS "$NATIVE_SOURCE/main_lite.cpp" -Wl,--as-needed -lcrypto -lsnappy -l:libsimdjson_static.a -o "$BUILD_OUTPUT"
fi
strip --strip-unneeded "$BUILD_OUTPUT"
