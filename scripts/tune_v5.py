"""Controlled sequential native-process comparisons on configuration-only inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--variant', action='append', required=True, help='NAME=EXECUTABLE')
    ap.add_argument('--runs', type=int, default=7)
    ap.add_argument('--unit', action='append')
    ap.add_argument('--out', default='/output/comparison.json')
    ap.add_argument('--env', action='append', default=[], help='NAME:KEY=VALUE')
    ap.add_argument('--all-units', action='store_true')
    args = ap.parse_args()
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    variants = dict(v.split('=', 1) for v in args.variant)
    environments = {n: {} for n in variants}
    for item in args.env:
        name, value = item.split(':', 1)
        key, value = value.split('=', 1)
        environments[name][key] = value
    report = {'protocol': 'rotated/reversed sequential fresh processes; one warmup; startup, simulation, file closure and hashing timed; artifact copying outside timing',
              'storage': 'Linux container filesystem', 'runs': args.runs,
              'cpu_affinity': sorted(os.sched_getaffinity(0)),
              'cpu_max': Path('/sys/fs/cgroup/cpu.max').read_text().strip(),
              'memory_max': Path('/sys/fs/cgroup/memory.max').read_text().strip(),
              'binaries': {n: hashlib.sha256(Path(p).read_bytes()).hexdigest() for n, p in variants.items()}, 'environments': environments, 'units': []}
    units = sorted(p.name for p in Path('/input').iterdir() if p.is_dir()) if args.all_units else args.unit or ['t3-as01-base-mix', 't3-gb-base-30agent-30s', 't3-gb-mega-throughput', 't3-gbatch-homog-8', 't3-mr-deep-book-state-size']
    for slug in units:
        unit = Path('/input') / slug
        batch = (unit / 'batch.json').exists()
        trials = {n: [] for n in variants}
        stable = {}
        for repeat in range(args.runs + 1):
            names = list(variants)
            names = names[repeat % len(names):] + names[:repeat % len(names)]
            if repeat % 2:
                names.reverse()
            for name in names:
                out = Path('/tmp/v5-trials') / slug / name
                out.mkdir(parents=True, exist_ok=True)
                cmd = [variants[name], 'simulate-batch', '--batch-dir', str(unit / 'scenarios'), '--out-dir', str(out)] if batch else [variants[name], 'simulate', '--config', str(unit / 'scenario.json'), '--out', str(out / 'trace.parquet')]
                start = time.perf_counter()
                process = subprocess.run(cmd, env={**os.environ, 'T3SIM_STREAM_LEDGER': '1', 'T3SIM_STREAM_TRACE': 'auto', 'T3SIM_LATENCY_PREFETCH': 'auto', **environments[name]}, capture_output=True, text=True, check=True, timeout=120)
                elapsed = time.perf_counter() - start
                events = json.loads(process.stdout)
                hashes = tuple((str(p.relative_to(out)), hashlib.sha256(p.read_bytes()).hexdigest()) for p in sorted(out.rglob('*.parquet')))
                if name in stable and stable[name] != hashes:
                    raise RuntimeError(f'Unstable output: {slug} {name}')
                stable[name] = hashes
                if repeat:
                    trials[name].append({'process_sec': elapsed, 'events': events})
        row = {'unit': slug, 'modes': {}, 'same_parquet_bytes': {n: stable[n] == stable[next(iter(variants))] for n in variants}}
        for name in variants:
            elapsed = statistics.median(t['process_sec'] for t in trials[name])
            count = trials[name][0]['events']['total_events' if batch else 'n_events']
            row['modes'][name] = {'median_process_sec': elapsed, 'events_per_sec': count / elapsed, 'median_reported_events_per_sec': statistics.median(t['events']['events_per_sec'] for t in trials[name]), 'trials': trials[name]}
            print(f'{slug} {name}: {count / elapsed:,.0f}/s; {elapsed:.6f}s', flush=True)
        report['units'].append(row)
        Path(args.out).write_text(json.dumps(report, indent=2) + '\n')
        for name in variants:
            shutil.copytree(Path('/tmp/v5-trials') / slug / name, Path(args.out).parent / slug / name, dirs_exist_ok=True)


if __name__ == '__main__':
    main()
