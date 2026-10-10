"""Published Arrow columns with nullable-int pandas metadata, without importing pandas."""
import json
import pyarrow as pa
import numpy as np


def string_array(values):
    encoded=[value.encode('utf8') for value in values]
    offsets=np.zeros(len(values)+1,dtype=np.int32)
    offsets[1:]=np.cumsum([len(value) for value in encoded])
    return pa.Array.from_buffers(pa.string(),len(values),
                                 [None,pa.py_buffer(offsets),pa.py_buffer(b''.join(encoded))])


def numeric_array(values, nullable=False):
    bitmap=pa.py_buffer(np.packbits(values>=0,bitorder='little')) if nullable else None
    return pa.Array.from_buffers(pa.from_numpy_dtype(values.dtype),len(values),
                                 [bitmap,pa.py_buffer(values)])


def schema(fields, nullable=()):
    result=pa.schema(fields)
    columns=[]
    for field in result:
        dtype='string' if pa.types.is_string(field.type) else str(field.type)
        columns.append({'name':field.name, 'field_name':field.name,
                        'pandas_type':'unicode' if dtype=='string' else dtype,
                        'numpy_type':'Int64' if field.name in nullable else dtype,
                        'metadata':None})
    metadata={'index_columns':[], 'column_indexes':[], 'columns':columns,
              'creator':{'library':'pyarrow', 'version':pa.__version__},
              'pandas_version':'1.5.3'}
    return result.with_metadata({b'pandas':json.dumps(metadata).encode()})


TRACE_SCHEMA=schema([
    ('t_ns',pa.int64()), ('agent_id',pa.int32()), ('msg_type',pa.string()),
    ('side',pa.string()), ('price',pa.int64()), ('size',pa.int64()), ('order_id',pa.int64())])
MESSAGE_SCHEMA=schema([
    ('seq',pa.int64()), ('t_recv_ns',pa.int64()), ('t_send_ns',pa.int64()),
    ('latency_ns',pa.int64()), ('src_id',pa.int32()), ('dst_id',pa.int32()),
    ('message_id',pa.int64()), ('msg_type',pa.string()), ('order_id',pa.int64()),
    ('causal_parent',pa.int64())], nullable=('t_send_ns','order_id','causal_parent'))
