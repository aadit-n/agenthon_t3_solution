#!/bin/sh
set -eu
# All selected optimizations retain the generic amd64 and exact FP contract.
export T3_EXTRA_CXX_FLAGS="-DT3_RADIX_QUEUE -DT3_COMPACT_MESSAGES -DT3_FLAT_OPEN -DT3_SSE_ROUND -DT3_PREPARED_RANDOM -DT3_DIRECT_HASH -DT3_NARROW_CODES -DT3_IMPLICIT_SEQUENCE -DT3_PRICE_CACHE -DT3_PACKED_ROUTE -DT3_UNINITIALIZED_BYTES -DT3_FUSED_DELTA -fno-math-errno -Wno-deprecated-declarations ${T3_EXTRA_CXX_FLAGS:-}"
sh "$(dirname "$0")/build_lite.sh"
