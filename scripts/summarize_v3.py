"""Bind the C++ release report to completed exact checks and measured runs."""
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
from summarize_v2 import roster

def main():
    root=Path(__file__).resolve().parents[1]
    def read(name):return json.loads((root/'results'/name).read_text())
    validation=read('v3-public/validation.json');exact=read('v3-public/exact.json')
    perf=read('v3-performance/performance.json');comparison=read('v3-vs-v2/benchmark.json')
    variants=read('v3-variants/validation.json');variant_image=read('v3-variants/image.json');random=read('v3-random/validation.json')
    image=validation['image_id']
    assert validation['passed']==71 and validation['failed']==0
    assert exact['all_exact'] and exact['markets']==95 and exact['image_id']==image
    assert all(r['nullable_pandas_roundtrip_exact'] for r in exact['checks'])
    assert perf['image_id']==comparison['images']['candidate']==random['image_id']==variant_image['candidate']==image
    assert len(variants)==18 and all(r['passed'] for r in variants)
    assert random['draws']==630000
    facts=[r['stylized_facts'] for r in validation['units'] if r.get('stylized_facts')]
    assert len(facts)==12 and all(all(v==0 for v in r.values()) for r in facts)
    previous=roster(root/'results/v2-public');current=roster(root/'results/v3-public')
    capacity={c['case']:c['summary'] for c in perf['cases']}
    info=json.loads(subprocess.check_output(['docker','image','inspect',image]))[0]
    old_info=json.loads(subprocess.check_output(['docker','image','inspect','agenthon-t3:v2']))[0]
    runtime=json.loads(subprocess.check_output(['docker','run','--rm','--network','none','--entrypoint','python',
        '-v',str(root/'scripts')+':/bench:ro',image,'/bench/inspect_cpp_runtime.py']))
    assert 'libcrypto.so.3' in runtime['linkage'] and 'libpython' not in runtime['linkage']
    phase_names=('configuration_sec','simulation_loop_sec','sorting_sec','column_assembly_sec','output_write_sec','wall_clock_sec')
    phases={}
    for slug in ('t3-as01-base-mix','t3-gb-base-30agent-30s','t3-gb-mega-throughput'):
        rows=[json.loads(p.read_text()) for p in (root/'results/v3-vs-v2'/slug/'candidate').glob('*/events.json') if p.parent.name!='0']
        phases[slug]={name:statistics.median(r[name] for r in rows) for name in phase_names}
    summary={'rankable':False,'image_id':image,'image_label':'agenthon-t3:v3','image_bytes':info['Size'],'v2_image_bytes':old_info['Size'],
        'public_units_passed':71,'markets_exact':95,'events_exact':exact['events'],'messages_exact':exact['messages'],
        'calibration_units_with_zero_divergences':12,'unseen_variants_exact':18,'exact_random_draws':random['draws'],
        'v2_public':previous,'v3_public':current,'comparison':comparison['units'],'capacity':capacity,'phase_medians_seconds':phases,
        'container_roster_speedup':current['container_events_per_sec']/previous['container_events_per_sec'],
        'self_reported_roster_speedup':current['self_reported_events_per_sec']/previous['self_reported_events_per_sec']}
    out=root/'results/v3-optimization';out.mkdir(parents=True,exist_ok=True)
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    (out/'runtime-audit.json').write_text(json.dumps(runtime,indent=2)+'\n')
    (root/'results/SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n')
    sources=[root/'Dockerfile',root/'scripts/build_cpp.sh',root/'scripts/entrypoint_cpp.sh',root/'t3sim/native_types.hpp',*sorted((root/'native').glob('*'))]
    (out/'source-files.json').write_text(json.dumps({str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},indent=2)+'\n')
    lines=[
        'V3 runs the complete simulator as a standalone C++17 executable. It removes Python startup and callbacks, preserves the exact public market paths, and improves complete-job throughput over the submitted v2 image. Local measurements cannot establish a Codabench score.\n',
        f"Release: `agenthon-t3:v3`, `{image}`. CPU: {perf['environment']['cpu_model']}, four CPUs, 16-GiB container cap, offline, no GPU. Image size: {info['Size']/2**20:.1f} MiB versus {old_info['Size']/2**20:.1f} MiB for v2.\n",
        f"Correctness: **71/71 actual public developer-verifier units**, **95 exact markets**, **{exact['events']:,} exchange rows**, **{exact['messages']:,} ledger rows**, all four calibration divergences zero on all 12 calibration units, **18 unseen parameter/seed scenarios**, and **630,000 exact mixed random draws** against Linux NumPy 1.26.4.\n",
        'Five measured alternating v2/v3 trials per workload after one discarded warmup. Docker start-to-exit timings include executable startup, both Parquet files, hashing and shutdown. All repeated outputs were byte stable.\n',
        '| Workload | v2 events/s | v3 events/s | Speedup |','|---|---:|---:|---:|']
    for row in comparison['units']:lines.append(f"| {row['unit']} | {row['baseline_median']:,.0f} | {row['candidate_median']:,.0f} | {row['speedup']:.2f}x |")
    lines += ['\nSeparate capacity test: three measured trials after one warmup; each output is checked against its complete public reference.\n',
        '| Workload | Events/s | Messages/s | Order commands/s | Container seconds | Peak cgroup MiB |',
        '|---|---:|---:|---:|---:|---:|']
    for name,c in capacity.items():lines.append(f"| {name} | {c['events_per_sec']['median']:,.0f} | {c['messages_per_sec']['median']:,.0f} | {c['order_commands_per_sec']['median']:,.0f} | {c['container_sec']['median']:.3f} | {c['peak_cgroup_memory_bytes']/2**20:.1f} |")
    lines += [
        f"\nThe full 71-unit single pass averaged **{current['container_events_per_sec']:,.0f} container events/s**, versus **{previous['container_events_per_sec']:,.0f}** for v2 ({summary['container_roster_speedup']:.2f}x). Self-reported arithmetic means were **{current['self_reported_events_per_sec']:,.0f}** and **{previous['self_reported_events_per_sec']:,.0f}**, respectively. These clocks and rosters do not certify comparability with the user-reported 97k/231k standings.\n",
        'The CLI processes simulation jobs; message rates and order-command rates are diagnostics, not HTTP requests per second. Batch 1/2/4-worker outputs have identical bytes. Range and CPU-use details are in `../v3-performance/performance.json`.\n',
        'Changes: native JSON/configuration, an owned RandomState-compatible MT19937 implementation, legacy distributions, and exact sparse OU/shock oracle; 32-byte heap entries referencing recycled message slots; only decision-relevant open-order bookkeeping; sorting equal-time runs rather than the whole already chronological trace; dense last-execution marking; activity-based bounded buffer reservation; one-pass Arrow columns; release of row buffers before encoding; adaptive string/dictionary representation; buffered writes and streaming SHA256; independent C++ batch threads with one writer each; a smaller runtime containing only the executable, Arrow/Parquet libraries and base tools.\n',
        'Numerical compatibility requires scalar seed initialization, the same global/private draw order, Gaussian cache behavior, masked integer rejection, ties-to-even rounding, and the oracle’s original mixture of integer and floating timestamps. Builds target generic amd64 with `-O3 -ffp-contract=off`, without fast-math or CPU-specific instruction assumptions.\n',
        'Dictionary output explicitly attaches pandas metadata to the Parquet footer without embedding dictionary Arrow field types. All 95 ledgers pass an Int64 pandas roundtrip check. Raw Arrow value equality alone does not catch downstream float conversion and nanosecond rounding.\n',
        'Seven measured rotated/reversed diagnostic trials after one warmup in `../cpp-output-tuning/large/ablation.json` and `../cpp-output-tuning/batch/ablation.json` selected Snappy, parallel files for single jobs, and one writer per concurrent batch market. Large tables use dictionary codes from 262,144 rows; small tables retain strings. This threshold depends on output size, never scenario identity, seed or reference values. Omitting optional statistics and uncompressed output did not consistently improve the workload mix.\n',
        '| Phase (seconds) | Mixed | Medium | Largest |','|---|---:|---:|---:|']
    for phase in phase_names:lines.append('| '+phase+' | '+' | '.join(f"{phases[slug][phase]:.6f}" for slug in phases)+' |')
    lines += [
        '\nV3 phase measurements separate simulation, sorting, assembly and writing. V2’s `simulation_loop_sec` included sorting and column assembly, so those two loop counters should not be directly compared. Timing remains actual elapsed wall time through output close and hashing.\n',
        'A single market stays sequential because agent reactions, exchange state, private/global random draws and causal message ordering interact. Parallelizing those dependent events or dropping ledgers changes the task. Independent markets use the four-CPU budget. GPU initialization, synchronization and a separately verified numerical port would add cost to these workloads; a GPU backend has not been demonstrated faster here. Rust offers no measured advantage over this native implementation.\n',
        'Remaining major costs are event dispatch/matching, required Parquet encoding and container lifecycle overhead on small jobs. Hardware, filesystem and scheduler differences can change the gains; a new organizer evaluation is required to establish whether v3 beats 231,024.2407.\n',
        'To submit, publish `agenthon-t3:v3` under a new public GHCR tag and regenerate the descriptor/ZIP using its new registry digest. Older ZIPs still identify earlier images. No push or Codabench upload was performed by this work.\n']
    (out/'REPORT.md').write_text('\n'.join(lines)+'\n',encoding='utf8')
    print(out/'REPORT.md')

if __name__=='__main__':main()
