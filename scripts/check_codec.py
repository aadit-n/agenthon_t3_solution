"""Independent Arrow decoder checks for custom Parquet boundary/overflow cases."""
import argparse
import os
os.environ.setdefault('OPENBLAS_NUM_THREADS','1')
os.environ.setdefault('OMP_NUM_THREADS','1')
import hashlib
import json
from pathlib import Path
import numpy as np
import pyarrow as pa
import pyarrow.parquet as pq

ap=argparse.ArgumentParser();ap.add_argument('directory',type=Path);args=ap.parse_args()
names=['x64','x32','t_send_ns','msg_type','order_id','causal_parent'];labels=['A','BB','CCC','Δ','quote']
cases=json.loads((args.directory/'codec.json').read_text());rows=0
for case in cases:
    path=args.directory/case['file'];raw=np.fromfile(path.with_suffix('.bin'),dtype='<i8').reshape(-1,6)
    assert hashlib.sha256(path.read_bytes()).hexdigest()==case['sha256']
    table=pq.read_table(path);assert table.num_rows==case['rows']
    for col,name in enumerate(names):
        if col==3:reference=pa.array([labels[int(code)] for code in raw[:,col]],type=pa.string())
        else:
            reference=pa.array(raw[:,col].astype('int32' if col==1 else 'int64'),mask=raw[:,col]<0 if col in [2,4,5] else None)
        assert table[name].combine_chunks().equals(reference),(case,name)
    frame=table.to_pandas()
    for name in ['t_send_ns','order_id','causal_parent']:
        assert str(frame[name].dtype)=='Int64',(case,name)
        assert pa.array(frame[name],type=pa.int64()).equals(table[name].combine_chunks()),(case,name,'rounded')
    rows+=case['rows']
print(f'{len(cases)} codec cases, {rows:,} rows: exact full-width integers, UTF8, nulls, pandas and hashes')
(args.directory/'validation.json').write_text(json.dumps({'cases':len(cases),'rows':rows,'all_exact':True},indent=2)+'\n')
