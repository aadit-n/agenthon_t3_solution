"""Compare complete experimental files with the independently executed v3."""
import argparse
import gc
import json
from pathlib import Path
import pyarrow.parquet as pq

ap=argparse.ArgumentParser();ap.add_argument('directory',type=Path);args=ap.parse_args()
report=[]
for unit in sorted(args.directory.iterdir()):
    if not unit.is_dir() or not (unit/'v3').is_dir():continue
    for mode in sorted(unit.iterdir()):
        if not mode.is_dir() or mode.name=='v3':continue
        for reference in sorted((unit/'v3').rglob('*.parquet')):
            candidate=mode/reference.relative_to(unit/'v3')
            a=pq.read_table(reference);b=pq.read_table(candidate)
            assert a.equals(b,check_metadata=False),(candidate,'Arrow values/schema differ')
            if candidate.name=='message_trace.parquet':
                frame=b.to_pandas()
                for name in ('t_send_ns','order_id','causal_parent'):
                    assert str(frame[name].dtype)=='Int64',(candidate,name,frame[name].dtype)
                del frame
            report.append({'file':str(candidate),'rows':b.num_rows,'exact':True})
            del a,b;gc.collect()
print(f'Exact values, types and nullable metadata: {len(report)} files')
(args.directory/'exact.json').write_text(json.dumps(report,indent=2)+'\n')
