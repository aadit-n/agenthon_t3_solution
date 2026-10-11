"""Streaming exact comparison of every public market and message ledger."""
import os
os.environ.setdefault('OPENBLAS_NUM_THREADS','1')
os.environ.setdefault('OMP_NUM_THREADS','1')
import argparse
import json
from pathlib import Path
import pyarrow.parquet as pq
import pyarrow as pa
from performance import exact_parquet

def exact_nullable_metadata(path):
    file=pq.ParquetFile(path)
    metadata=file.metadata.metadata or {}
    if b'pandas' not in metadata:return False
    columns={c['name']:c for c in json.loads(metadata[b'pandas'])['columns']}
    nullable=('t_send_ns','order_id','causal_parent')
    if any(columns[name]['numpy_type']!='Int64' for name in nullable):return False
    batch=next(file.iter_batches(batch_size=1024,columns=list(nullable)),None)
    if batch is None:return True
    frame=batch.to_pandas()
    return all(str(frame[name].dtype)=='Int64' and pa.array(frame[name],type=pa.int64()).equals(batch.column(name)) for name in nullable)

def main():
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser()
    parser.add_argument('--output-dir',type=Path)
    parser.add_argument('--pair',type=Path,nargs=2,metavar=('CANDIDATE','REFERENCE'))
    parser.add_argument('--fixture',type=Path,default=root/'results/reference-public')
    args=parser.parse_args();checks=[]
    if args.pair:
        result={name:exact_parquet(args.pair[0]/name,args.pair[1]/name) for name in ('trace.parquet','message_trace.parquet')}
        result['nullable_pandas_roundtrip_exact']=exact_nullable_metadata(args.pair[0]/'message_trace.parquet')
        print(json.dumps(result))
        return 0 if all(result.values()) else 1
    if args.output_dir is None:parser.error('--output-dir or --pair required')
    for unit in sorted((args.fixture/'units').iterdir()):
        if not unit.is_dir():continue
        batch=(unit/'batch.json').is_file()
        configs=sorted((unit/'scenarios').glob('*.json')) if batch else [unit/'scenario.json']
        for config in configs:
            reference=unit/'checks/reference_data'/config.stem if batch else unit
            actual=args.output_dir/unit.name/config.stem if batch else args.output_dir/unit.name
            row={'unit':unit.name,'sub':config.stem if batch else None}
            for filename,key in (('trace.parquet','trace_exact'),('message_trace.parquet','ledger_exact')):
                row[key]=exact_parquet(actual/filename,reference/filename)
            row['events']=pq.ParquetFile(actual/'trace.parquet').metadata.num_rows
            row['messages']=pq.ParquetFile(actual/'message_trace.parquet').metadata.num_rows
            row['nullable_pandas_roundtrip_exact']=exact_nullable_metadata(actual/'message_trace.parquet')
            checks.append(row)
            print(('EXACT' if row['trace_exact'] and row['ledger_exact'] and row['nullable_pandas_roundtrip_exact'] else 'DIFF')+' '+unit.name+' '+config.stem,flush=True)
    result={'image_id':json.loads((args.output_dir/'image.json').read_text())['image_id'],
            'markets':len(checks),'events':sum(r['events'] for r in checks),'messages':sum(r['messages'] for r in checks),
            'all_exact':all(r['trace_exact'] and r['ledger_exact'] and r['nullable_pandas_roundtrip_exact'] for r in checks),'checks':checks}
    (args.output_dir/'exact.json').write_text(json.dumps(result,indent=2)+'\n')
    return 0 if result['all_exact'] else 1

if __name__=='__main__':raise SystemExit(main())
