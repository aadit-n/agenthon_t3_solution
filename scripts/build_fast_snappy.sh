#!/bin/sh
set -eu
SOURCE="${1:-/opt/snappy}"
OUTPUT="${2:-/opt/libsnappy-fast.a}"
mkdir -p /tmp/snappy-fast-build
for file in snappy.cc snappy-sinksource.cc snappy-stubs-internal.cc; do
    g++ -O3 -DNDEBUG -std=c++17 -ffp-contract=off -mavx2 -mbmi2 -msse4.2 \
        -Dsnappy=snappy_fast -DHAVE_BUILTIN_CTZ=1 -DHAVE_BUILTIN_EXPECT=1 \
        -DHAVE_BUILTIN_PREFETCH=1 -DHAVE_ATTRIBUTE_ALWAYS_INLINE=1 \
        -DHAVE_SYS_UIO_H=1 -DHAVE_UNISTD_H=1 -DSNAPPY_HAVE_SSSE3=1 \
        -I"$SOURCE" -c "$SOURCE/$file" -o "/tmp/snappy-fast-build/$file.o"
done
ar rcs "$OUTPUT" /tmp/snappy-fast-build/*.o
