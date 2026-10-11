"""Within-container diagnostic comparisons; each trial launches the real binary."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time

MODES={
    'default':{},
    'dictionary':{'T3SIM_STRINGS':'dictionary'},
    'uncompressed':{'T3SIM_COMPRESSION':'none'},
    'dictionary-uncompressed':{'T3SIM_STRINGS':'dictionary','T3SIM_COMPRESSION':'none'},
    'no-statistics':{'T3SIM_STATISTICS':'off'},
    'serial-writes':{'T3SIM_IO_THREADS':'1'},
}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--batch',action='store_true')
    parser.add_argument('--runs',type=int,default=7)
    args=parser.parse_args();results={mode:[] for mode in MODES};bindings={}
    for repeat in range(args.runs+1):
        modes=list(MODES)
        # Rotate and reverse mode ordering to reduce drift/position bias.
        modes=modes[repeat%len(modes):]+modes[:repeat%len(modes)]
        if repeat%2:modes.reverse()
        for mode in modes:
            output=Path('/output')/mode;output.mkdir(parents=True,exist_ok=True)
            command=['entrypoint','simulate-batch','--batch-dir','/input','--out-dir',str(output)] if args.batch else [
                'entrypoint','simulate','--config','/input/scenario.json','--out',str(output/'trace.parquet')]
            start=time.perf_counter()
            proc=subprocess.run(command,env={**os.environ,**MODES[mode]},check=True,capture_output=True,text=True)
            elapsed=time.perf_counter()-start;events=json.loads(proc.stdout)
            binding=events['per_scenario'] if args.batch else (events['trace_sha256'],events['message_trace_sha256'])
            if mode in bindings and bindings[mode]!=binding:raise ValueError('nondeterministic '+mode)
            bindings[mode]=binding
            if repeat:results[mode].append({'process_sec':elapsed,'events':events})
        print(f'Completed comparison round {repeat}',flush=True)
    report={'protocol':'one container; separate real executable per trial; one warmup; rotated/reversed order; all IO/hashing included',
        'runs':args.runs,'binary_sha256':hashlib.sha256(Path('/opt/t3sim-cpp').read_bytes()).hexdigest(),'modes':{}}
    for mode,runs in results.items():
        elapsed=statistics.median(r['process_sec'] for r in runs)
        count=runs[0]['events']['total_events' if args.batch else 'n_events']
        report['modes'][mode]={'environment':MODES[mode],'median_process_sec':elapsed,'events_per_sec':count/elapsed,'trials':runs}
        print(f'{mode}: {count/elapsed:,.0f} events/s, {elapsed:.6f}s',flush=True)
    Path('/output/ablation.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
