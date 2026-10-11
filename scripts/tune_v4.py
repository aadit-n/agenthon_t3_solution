"""Sequential executable trials, including startup, output close and hashing."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--runs',type=int,default=5)
    ap.add_argument('--mode',action='append')
    ap.add_argument('--unit',action='append')
    ap.add_argument('--local-output',action='store_true',help='time actual writes to Linux container storage, then retain artifacts outside timing')
    args=ap.parse_args()
    modes={
        'v3':('/opt/t3sim-cpp',{}),
        'arrow':('/binaries/t3sim-v4',{}),
        'pages':('/binaries/t3sim-v4',{'T3SIM_WRITER':'pages'}),
        'pages-serial':('/binaries/t3sim-v4',{'T3SIM_WRITER':'pages','T3SIM_IO_THREADS':'1'}),
        'pages-plain':('/binaries/t3sim-v4',{'T3SIM_WRITER':'pages','T3SIM_PAGE_NUMERIC':'plain'}),
        'pool':('/binaries/t3sim-pool',{'T3SIM_WRITER':'pages'}),
        'heap':('/binaries/t3sim-heap',{'T3SIM_WRITER':'pages'}),
        'both':('/binaries/t3sim-both',{'T3SIM_WRITER':'pages'}),
        'refined':('/binaries/t3sim-v4-refined',{'T3SIM_WRITER':'pages'}),
        'numeric-dict':('/binaries/t3sim-v4-refined',{'T3SIM_WRITER':'pages','T3SIM_NUMERIC_DICT':'1'}),
        'pipeline':('/binaries/t3sim-v4-refined',{'T3SIM_WRITER':'pages','T3SIM_PIPELINE':'1'}),
        'pipeline-dict':('/binaries/t3sim-v4-refined',{'T3SIM_WRITER':'pages','T3SIM_PIPELINE':'1','T3SIM_NUMERIC_DICT':'1'}),
        'lite':('/binaries/t3sim-lite',{'LD_LIBRARY_PATH':'/binaries'}),
        'lite-dict':('/binaries/t3sim-lite',{'LD_LIBRARY_PATH':'/binaries','T3SIM_LITE_DICT':'1'}),
        'lite-serial':('/binaries/t3sim-lite',{'LD_LIBRARY_PATH':'/binaries','T3SIM_LITE_PIPELINE':'0'}),
        'lite-stream':('/binaries/t3sim-lite',{'LD_LIBRARY_PATH':'/binaries','T3SIM_STREAM_LEDGER':'1'}),
        'pgo':('/binaries/t3sim-pgo',{'LD_LIBRARY_PATH':'/binaries'}),
        'pgo-stream':('/binaries/t3sim-pgo',{'LD_LIBRARY_PATH':'/binaries','T3SIM_STREAM_LEDGER':'1'}),
        'latency-off':('/binaries/t3sim-latency',{'LD_LIBRARY_PATH':'/binaries','T3SIM_STREAM_LEDGER':'1','T3SIM_LATENCY_PREFETCH':'off'}),
        'latency-auto':('/binaries/t3sim-latency',{'LD_LIBRARY_PATH':'/binaries','T3SIM_STREAM_LEDGER':'1','T3SIM_LATENCY_PREFETCH':'auto'}),
        'dual-off':('/binaries/t3sim-dual-stream',{'LD_LIBRARY_PATH':'/binaries','T3SIM_STREAM_LEDGER':'1','T3SIM_LATENCY_PREFETCH':'auto'}),
        'dual-on':('/binaries/t3sim-dual-stream',{'LD_LIBRARY_PATH':'/binaries','T3SIM_STREAM_TRACE':'1','T3SIM_LATENCY_PREFETCH':'auto'}),
        'dual-nolat':('/binaries/t3sim-dual-stream',{'LD_LIBRARY_PATH':'/binaries','T3SIM_STREAM_TRACE':'1','T3SIM_LATENCY_PREFETCH':'off'}),
        'release':('/binaries/t3sim-release',{'LD_LIBRARY_PATH':'/binaries','T3SIM_STREAM_LEDGER':'1','T3SIM_STREAM_TRACE':'auto','T3SIM_LATENCY_PREFETCH':'auto'}),
        'balanced':('/binaries/t3sim-balanced',{'LD_LIBRARY_PATH':'/binaries','T3SIM_STREAM_LEDGER':'1','T3SIM_STREAM_TRACE':'auto','T3SIM_LATENCY_PREFETCH':'auto'}),
    }
    selected=args.mode or ['v3','arrow','pages','pages-serial','pages-plain']
    report={'protocol':'separate real executables; same container; sequential rotated/reversed trials; one warmup excluded; startup, both outputs and hashing included','output_storage':'Linux container filesystem' if args.local_output else 'Windows bind mount','runs':args.runs,'units':[],'binaries':{}}
    for name in selected:
        binary,_=modes[name];report['binaries'][name]=hashlib.sha256(Path(binary).read_bytes()).hexdigest()
    for slug in args.unit or ['t3-as01-base-mix','t3-gb-base-30agent-30s','t3-gb-mega-throughput','t3-gbatch-homog-8']:
        unit=Path('/input')/slug;batch=(unit/'batch.json').is_file()
        trials={name:[] for name in selected};stable={}
        for repeat in range(args.runs+1):
            order=selected[repeat%len(selected):]+selected[:repeat%len(selected)]
            if repeat%2:order.reverse()
            for name in order:
                binary,environment=modes[name];out=Path('/tmp/native-trials' if args.local_output else '/output')/slug/name;out.mkdir(parents=True,exist_ok=True)
                cmd=[binary,'simulate-batch','--batch-dir',str(unit/'scenarios'),'--out-dir',str(out)] if batch else [binary,'simulate','--config',str(unit/'scenario.json'),'--out',str(out/'trace.parquet')]
                begin=time.perf_counter();p=subprocess.run(cmd,env={**os.environ,**environment},capture_output=True,text=True,check=True);elapsed=time.perf_counter()-begin
                events=json.loads(p.stdout);binding=tuple((str(f.relative_to(out)),hashlib.sha256(f.read_bytes()).hexdigest()) for f in sorted(out.rglob('*.parquet')))
                if name in stable and stable[name]!=binding:raise RuntimeError(f'Unstable output: {slug} {name}')
                stable[name]=binding
                if repeat:trials[name].append({'process_sec':elapsed,'events':events})
        row={'unit':slug,'modes':{}}
        for name in selected:
            elapsed=statistics.median(t['process_sec'] for t in trials[name]);n=trials[name][0]['events']['total_events' if batch else 'n_events']
            row['modes'][name]={'median_process_sec':elapsed,'events_per_sec':n/elapsed,'trials':trials[name]}
            print(f'{slug} {name}: {n/elapsed:,.0f}/s; {elapsed:.6f}s',flush=True)
        report['units'].append(row);Path('/output/ablation.json').write_text(json.dumps(report,indent=2)+'\n')
        if args.local_output:
            import shutil
            shutil.copytree(Path('/tmp/native-trials')/slug,Path('/output')/slug,dirs_exist_ok=True)

if __name__=='__main__':main()
