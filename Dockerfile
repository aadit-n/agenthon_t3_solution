FROM python:3.11-slim@sha256:e88e9763f943ec1834f992a4b51e0f24500486803e8bc534e5767af9ea65f6ce AS dependencies
LABEL qfbench2.interface_version="2.0"
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 PYTHONPATH=/opt \
    OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 MKL_NUM_THREADS=1 NUMEXPR_NUM_THREADS=1
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

FROM sources AS final
COPY --from=compiler /opt /opt
