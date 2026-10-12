#!/bin/sh
set -eu
root=/tmp/runtime-root
mkdir -p "$root/opt/licenses" "$root/work" "$root/tmp" "$root/etc" "$root/sbin" "$root/lib64" "$root/lib/x86_64-linux-gnu"
cp /opt/t3sim-cpp "$root/opt/"
cp /tmp/release-notices/* "$root/opt/licenses/"
cp -a /tmp/abides-licenses "$root/opt/licenses/abides"
cp /tmp/adapter-license "$root/opt/licenses/adapter-LICENSE"
for package in libc6 libstdc++6 libgcc-s1 libssl3t64 libsnappy1v5 nlohmann-json3-dev; do
    mkdir -p "$root/opt/licenses/$package"
    cp "/usr/share/doc/$package/copyright" "$root/opt/licenses/$package/"
done
# The engine is static. Keep a small glibc runtime, loader, ldconfig and OS
# metadata for the evaluation worker's optional injected driver utilities.
for library in libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1 libgcc_s.so.1 libstdc++.so.6; do
    cp -L "/lib/x86_64-linux-gnu/$library" "$root/lib/x86_64-linux-gnu/$library"
done
cp -L /lib64/ld-linux-x86-64.so.2 "$root/lib64/"
cp -L /usr/sbin/ldconfig "$root/sbin/ldconfig"
cp -L /etc/os-release "$root/etc/os-release"
printf '/lib/x86_64-linux-gnu\n/lib64\n' > "$root/etc/ld.so.conf"
ldconfig -r "$root"
chmod 1777 "$root/tmp"
# Include application sources/build recipe for rebuilding and relinking.
# No compiler, generated training inputs, profiles or reference outputs ship.
mkdir -p "$root/opt/relink/native" "$root/opt/relink/scripts"
cp /opt/native/* "$root/opt/relink/native/"
cp /tmp/build_lite.sh /tmp/build_release.sh /tmp/pgo_scenarios.py "$root/opt/relink/scripts/"
cp /tmp/release-Dockerfile /tmp/release-requirements.txt "$root/opt/relink/"
mv "$root/opt/relink/release-Dockerfile" "$root/opt/relink/Dockerfile"
mv "$root/opt/relink/release-requirements.txt" "$root/opt/relink/requirements.txt"
cp /tmp/prepare_runtime.sh "$root/opt/relink/scripts/"
cp /tmp/release-notices/* "$root/opt/relink/"
mkdir -p "$root/opt/relink/vendor/abides" "$root/opt/relink/vendor/abides_fork"
cp /tmp/abides-licenses/* "$root/opt/relink/vendor/abides/"
cp /tmp/adapter-license "$root/opt/relink/vendor/abides_fork/LICENSE"
