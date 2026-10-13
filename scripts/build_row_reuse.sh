#!/bin/sh
set -eu
mkdir -p /tmp/execution-native /tmp/execution-build /output/binaries
cp /src/* /tmp/execution-native/
cp /scripts/build_lite.sh /scripts/build_release.sh /scripts/pgo_scenarios.py /scripts/pgo_specialized.py /tmp/execution-build/
export T3_NATIVE_SOURCE=/tmp/execution-native T3_PGO_SCENARIOS=/tmp/execution-build/pgo_specialized.py
for variant in ${T3_EXECUTION_VARIANTS:-reuse reuse-combined}; do
    case "$variant" in
        reuse) extra='-DT3_REUSE_ROW_BUFFERS' ;;
        reuse-combined) extra='-DT3_REUSE_ROW_BUFFERS -DT3_SPECIALIZE_LATENCY -DT3_SPECIALIZE_PROTOCOL' ;;
        *) exit 1 ;;
    esac
    T3_EXTRA_CXX_FLAGS="$extra" T3_BUILD_OUTPUT="/output/binaries/$variant" \
        T3_PROFILE_DIR="/tmp/execution-profile-$variant" T3_BUILD_PGO=1 sh /tmp/execution-build/build_release.sh
    sha256sum "/output/binaries/$variant"
done
