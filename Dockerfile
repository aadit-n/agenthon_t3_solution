FROM python:3.11-slim@sha256:e88e9763f943ec1834f992a4b51e0f24500486803e8bc534e5767af9ea65f6ce AS base
LABEL qfbench2.interface_version="2.0"
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 PYTHONPATH=/opt \
    OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 MKL_NUM_THREADS=1 NUMEXPR_NUM_THREADS=1
FROM base AS dependencies
COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir -r /tmp/requirements.txt

# Install the toolchain before copying source so edits reuse this large layer.
FROM dependencies AS toolchain
RUN apt-get update && apt-get install -y --no-install-recommends gcc g++ libc6-dev \
    && pip install --no-cache-dir Cython==3.2.4 setuptools==82.0.0

FROM dependencies AS sources
COPY vendor/abides/abides-core/abides_core /opt/abides_core
COPY vendor/abides/abides-markets/abides_markets /opt/abides_markets
COPY vendor/abides_fork /opt/abides_fork
COPY vendor/abides/LICENSE* /opt/licenses/
COPY LICENSE THIRD_PARTY_NOTICES.md SOURCE_PROVENANCE.json /opt/licenses/
COPY t3sim /opt/t3sim
COPY scripts/entrypoint.sh /usr/local/bin/entrypoint
RUN chmod +x /usr/local/bin/entrypoint
WORKDIR /work
ENTRYPOINT ["entrypoint"]
CMD ["--help"]

# A separate baseline target keeps the exact organizer engine available for comparisons.
FROM sources AS baseline
ENV T3SIM_BASELINE=1
RUN python -m compileall -q /opt

FROM toolchain AS compiler
COPY --from=sources /opt /opt
COPY scripts/prepare_build.py setup_native.py /tmp/build-tools/
RUN python /tmp/build-tools/prepare_build.py /opt \
    && cd /opt && python /tmp/build-tools/setup_native.py build_ext --inplace -j 1 \
    && find /opt -name '*.c' -delete && rm -rf /opt/build \
    && find /opt -name '*.so' -exec strip --strip-unneeded {} \; \
    && python -m compileall -q /opt

FROM sources AS cython
COPY --from=compiler /opt /opt

# The submission path is a standalone C++ process: no Python startup or callbacks.
FROM toolchain AS cpp-toolchain
RUN apt-get update && apt-get install -y --no-install-recommends nlohmann-json3-dev libssl-dev

FROM cpp-toolchain AS cpp-compiler
COPY t3sim/native_types.hpp /opt/t3sim/native_types.hpp
COPY native /opt/native
COPY scripts/build_cpp.sh /tmp/build_cpp.sh
RUN sh /tmp/build_cpp.sh

FROM base AS arrow
COPY --from=cpp-compiler /opt/t3sim-cpp /opt/t3sim-cpp
COPY --from=dependencies /usr/local/lib/python3.11/site-packages/pyarrow/libarrow.so.1500 /usr/local/lib/python3.11/site-packages/pyarrow/libarrow.so.1500
COPY --from=dependencies /usr/local/lib/python3.11/site-packages/pyarrow/libparquet.so.1500 /usr/local/lib/python3.11/site-packages/pyarrow/libparquet.so.1500
COPY --from=dependencies /usr/local/lib/python3.11/site-packages/pyarrow-15.0.2.dist-info/LICENSE.txt /opt/licenses/arrow/LICENSE.txt
COPY --from=dependencies /usr/local/lib/python3.11/site-packages/pyarrow-15.0.2.dist-info/NOTICE.txt /opt/licenses/arrow/NOTICE.txt
COPY --from=cpp-compiler /usr/share/doc/nlohmann-json3-dev/copyright /opt/licenses/nlohmann-json-copyright
COPY scripts/entrypoint_cpp.sh /usr/local/bin/entrypoint
COPY LICENSE THIRD_PARTY_NOTICES.md SOURCE_PROVENANCE.json /opt/licenses/
COPY vendor/abides/LICENSE* /opt/licenses/abides/
COPY vendor/abides_fork/LICENSE /opt/licenses/adapter/LICENSE
RUN chmod +x /usr/local/bin/entrypoint
WORKDIR /work
ENTRYPOINT ["entrypoint"]
CMD ["--help"]

FROM cpp-toolchain AS lite-compiler
RUN apt-get update && apt-get install -y --no-install-recommends libsnappy-dev=1.2.2-1
COPY native /opt/native
COPY scripts/build_lite.sh scripts/build_release.sh scripts/pgo_scenarios.py scripts/pgo_specialized.py /tmp/
ARG T3_BUILD_PGO=1
ARG T3_EXTRA_CXX_FLAGS=""
ARG T3_SPECIALIZED_PGO=0
RUN if [ "$T3_SPECIALIZED_PGO" = 1 ]; then export T3_PGO_SCENARIOS=/tmp/pgo_specialized.py; fi; \
    T3_BUILD_PGO=$T3_BUILD_PGO T3_EXTRA_CXX_FLAGS="$T3_EXTRA_CXX_FLAGS" sh /tmp/build_release.sh

# Assemble one small runtime layer after compiling the static executable.
FROM lite-compiler AS runtime-root
COPY LICENSE THIRD_PARTY_NOTICES.md SOURCE_PROVENANCE.json /tmp/release-notices/
COPY vendor/abides/LICENSE* /tmp/abides-licenses/
COPY vendor/abides_fork/LICENSE /tmp/adapter-license
COPY Dockerfile /tmp/release-Dockerfile
COPY requirements.txt /tmp/release-requirements.txt
COPY scripts/prepare_runtime.sh /tmp/prepare_runtime.sh
RUN sh /tmp/prepare_runtime.sh

FROM scratch AS final
LABEL qfbench2.interface_version="2.0"
ENV T3SIM_STREAM_LEDGER=1 T3SIM_STREAM_TRACE=auto T3SIM_LATENCY_PREFETCH=auto
COPY --from=runtime-root /tmp/runtime-root/ /
WORKDIR /work
ENTRYPOINT ["/opt/t3sim-cpp"]
CMD ["--help"]
