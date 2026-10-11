#!/bin/sh
set -eu
CXX_FLAGS="-O3 -std=c++17 -ffp-contract=off -pthread -DT3_POOL_BOOK"
if [ "${T3_BUILD_PGO:-0}" = 1 ]; then
    mkdir -p /tmp/compiler-profile
    g++ $CXX_FLAGS -flto -fprofile-generate=/tmp/compiler-profile -fprofile-update=atomic \
        /opt/native/main_lite.cpp -Wl,--as-needed -lcrypto -lsnappy -o /opt/t3sim-cpp
    python /tmp/pgo_scenarios.py /tmp/compiler-input
    T3SIM_STREAM_LEDGER=1 T3SIM_STREAM_TRACE=auto T3SIM_LATENCY_PREFETCH=auto /opt/t3sim-cpp simulate-batch --batch-dir /tmp/compiler-input --out-dir /tmp/compiler-small --workers 1
    T3SIM_STREAM_LEDGER=1 T3SIM_STREAM_TRACE=1 T3SIM_LATENCY_PREFETCH=auto /opt/t3sim-cpp simulate-batch --batch-dir /tmp/compiler-input --out-dir /tmp/compiler-output --workers 1
    /opt/t3sim-cpp simulate-batch --batch-dir /tmp/compiler-input --out-dir /tmp/compiler-batch --workers 4
    g++ $CXX_FLAGS -flto -fprofile-use=/tmp/compiler-profile -fprofile-correction -Werror=missing-profile \
        /opt/native/main_lite.cpp -Wl,--as-needed -lcrypto -lsnappy -o /opt/t3sim-cpp
else
    g++ $CXX_FLAGS /opt/native/main_lite.cpp -Wl,--as-needed -lcrypto -lsnappy -o /opt/t3sim-cpp
fi
strip --strip-unneeded /opt/t3sim-cpp
