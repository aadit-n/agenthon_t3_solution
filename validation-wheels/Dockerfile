FROM python:3.13-slim@sha256:70729b46c69b4f1e97c4822c1af3df53a1476cf5ddc6c087c0c10bc3a5678c2f
COPY qfbench2_common-2.5.1-py3-none-any.whl /tmp/toolkit.whl
RUN mv /tmp/toolkit.whl /tmp/qfbench2_common-2.5.1-py3-none-any.whl \
    && pip install --no-cache-dir '/tmp/qfbench2_common-2.5.1-py3-none-any.whl[data]' \
        numpy==2.5.3 scipy==1.18.1 pandas==3.0.6 pyarrow==26.0.0 jsonschema==4.26.0
ENTRYPOINT ["python"]
