"""Bind final v4 correctness, repeated timings and native experiments to images."""
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

root=Path(__file__).resolve().parents[1]
def read(relative):return json.loads((root/relative).read_text(encoding='utf-8-sig'))
def info(tag):return json.loads(subprocess.check_output(['docker','image','inspect',tag]))[0]

release=info('agenthon-t3:v4');previous=info('agenthon-t3:v3')
validation=read('results/v4-final-public/validation.json');exact=read('results/v4-final-public/exact.json')
variants=read('results/v4-final-variants/validation.json');variant_binding=read('results/v4-final-variants/image.json')
random=read('results/v4-final-random/validation.json');comparison=read('results/v4-final-vs-v3/benchmark.json')
performance=read('results/v4-final-performance/performance.json');native=read('results/v4-balanced-comparison/ablation.json')
assert validation['image_id']==exact['image_id']==variant_binding['candidate']==random['image_id']==performance['image_id']==comparison['images']['candidate']==release['Id']
assert comparison['images']['baseline']==previous['Id']
assert validation['passed']==71 and validation['failed']==0 and exact['all_exact'] and all(v['passed'] for v in variants)
binary=subprocess.check_output(['docker','run','--rm','--network','none','--entrypoint','python','agenthon-t3:v4','-c',
    "import hashlib;print(hashlib.sha256(open('/opt/t3sim-cpp','rb').read()).hexdigest())"],text=True).strip()
assert binary==native['binaries']['balanced']

cases={c['case']:c['summary'] for c in performance['cases']}
calibration=[r for r in validation['units'] if r['stylized_facts']]
assert all(all(value==0 for value in r['stylized_facts'].values()) for r in calibration)
summary={'rankable':False,'image_label':'agenthon-t3:v4','image_id':release['Id'],'binary_sha256':binary,
    'v3_image_id':previous['Id'],'image_bytes':release['Size'],'v3_image_bytes':previous['Size'],
    'public_units_passed':71,'markets_exact':exact['markets'],'events_exact':exact['events'],'messages_exact':exact['messages'],
    'calibration_units_with_zero_divergences':len(calibration),'unseen_variants_exact':len(variants),'exact_random_draws':random['draws'],
    'codec':read('results/v4-codec/validation.json'),'comparison':comparison['units'],'capacity':cases,'native_comparison':[],
    'limitations':'Local, non-rankable timings. Windows bind-mount/container overhead and Linux process timings are separate protocols. No Codabench score or rank prediction.'}
for row in native['units']:
    baseline=row['modes']['v3'];candidate=row['modes']['balanced']
    summary['native_comparison'].append({'unit':row['unit'],'v3_events_per_sec':baseline['events_per_sec'],
        'v4_events_per_sec':candidate['events_per_sec'],'speedup':candidate['events_per_sec']/baseline['events_per_sec']})

out=root/'results/v4-optimization';out.mkdir(parents=True,exist_ok=True)
files=['Dockerfile','native/core.hpp','native/main_lite.cpp','native/parquet_lite.hpp','native/random.hpp','native/simulation.hpp',
       'native/latency_stream.hpp','native/pooled_book.hpp','scripts/build_lite.sh','scripts/pgo_scenarios.py','scripts/entrypoint_cpp.sh',
       'SOURCE_PROVENANCE.json','THIRD_PARTY_NOTICES.md']
(out/'source-files.json').write_text(json.dumps({f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in files},indent=2)+'\n')
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');(root/'results/SUMMARY.json').write_text(json.dumps(summary,indent=2)+'\n')

lines=[
    'V4 completes the market simulation and required output faster while reproducing every public reference exactly. It replaces general Arrow conversion with a focused C++ Snappy Parquet writer, overlaps independent output work with the causal simulation, and removes per-order allocation. Measured gains depend on workload and filesystem; a new Codabench evaluation is needed to establish a score.\n',
    f"Release: `agenthon-t3:v4` and `agenthon-t3:latest`, local image identity `{release['Id']}`. Executable SHA256: `{binary}`.\n",
    f"Local CPU: {performance['environment']['cpu_model']}; four CPUs, 16 GiB cap, no network, no GPU attached. Runtime size: {release['Size']/2**20:.1f} MiB versus {previous['Size']/2**20:.1f} MiB for v3. These local IDs are not substitutes for a registry manifest digest.\n",
    f"Correctness: **71/71 actual public developer-verifier units**, **{exact['markets']} exact markets**, **{exact['events']:,} exchange rows**, **{exact['messages']:,} ledger rows**. All four realism divergences are zero on all {len(calibration)} calibration units. **{len(variants)} unseen scenarios** pass with trace streaming and latency prefetch forced on. **{random['draws']:,} mixed random draws** match pinned Linux NumPy 1.26.4. Nullable pandas Int64 roundtrips are exact.\n",
    'Codec diagnostics independently decode 18 boundary cases containing 721,782 rows: full-width signed values and wraparound deltas, all-null/mixed-null columns, UTF8, partial blocks, multiple row groups and hashes. Address/undefined-behavior sanitizers check the generator. The pooled book and experimental heap passed 300,000 randomized differential book actions and 1,000,000 tied heap operations with sanitizers.\n',
    'Seven measured alternating complete-container trials after one warmup per workload. Docker StartedAt-to-FinishedAt includes process startup, simulation, all output/hashing and shutdown, using Windows output bind mounts. Every repeated Parquet file is byte stable.\n',
    '| Workload | v3 events/s | v4 events/s | Speedup |','|---|---:|---:|---:|']
for row in comparison['units']:lines.append(f"| {row['unit']} | {row['baseline_median']:,.0f} | {row['candidate_median']:,.0f} | {row['speedup']:.2f}x |")
lines+=['\nA second seven-repeat protocol runs fresh real executables in one container on Linux storage, rotating/reversing order and excluding one warmup. Startup, simulation, file closure and hashes remain timed; artifact retention happens afterward. The final executable hash is checked against the actual release image. Public configurations alone are mounted for this comparison.\n',
        '| Workload | v3 events/s | v4 events/s | Speedup |','|---|---:|---:|---:|']
for row in summary['native_comparison']:lines.append(f"| {row['unit']} | {row['v3_events_per_sec']:,.0f} | {row['v4_events_per_sec']:,.0f} | {row['speedup']:.2f}x |")
lines+=['\nSeparate full-container capacity runs: three measurements after one warmup, all checked against complete public references.\n',
        '| Workload | Events/s | Messages/s | Order commands/s | Seconds | Peak cgroup MiB |','|---|---:|---:|---:|---:|---:|']
for name,c in cases.items():lines.append(f"| {name} | {c['events_per_sec']['median']:,.0f} | {c['messages_per_sec']['median']:,.0f} | {c['order_commands_per_sec']['median']:,.0f} | {c['container_sec']['median']:.3f} | {c['peak_cgroup_memory_bytes']/2**20:.1f} |")
lines+=[
    '\nThe prior v3 capacity test retained 223.6 MiB peak cgroup memory on the large workload; v4 retains about 31 MiB in this protocol. Cgroup peaks include charged file cache and all threads. Batch outputs are byte identical across 1/2/4 workers and preserve isolated behavior. These are simulation events/messages/order commands, not HTTP requests.\n',
    'Architecture: the binary heap preserves timestamp/sender/recipient/message-ID ordering. The pooled book preserves price-time FIFO and arbitrary int64 prices. Small jobs use the simpler output path. Larger jobs stream completed timestamp groups and ledger blocks through bounded queues; independent exact latency draws can occupy another core. Four CPUs handle useful independent work while causal market callbacks remain sequential. All workers are joined and output files close before elapsed time is reported.\n',
    'Trace streaming never splits a timestamp group or its mutable quote. The type column is deferred until each order\'s last execution is known; its dictionary/data pages and standard footer offsets are then written. Earlier partial fills and final fills reproduce the original canonical classification exactly. The other columns can be encoded while simulation continues.\n',
    'The custom writer implements the documented flat Parquet subset using Snappy 1.2.2, delta-packed integers, categorical dictionaries, correct null definitions, pandas metadata and incremental SHA256. It links no Arrow library. Runtime compilation, NumPy/pandas imports and external downloads are absent. Compiler LTO/PGO uses synthetic workloads covering small, streamed and concurrent batch paths, with generic amd64 and floating contraction disabled.\n',
    'Controlled trials retained Snappy and delta integers; numeric dictionaries and serial output did not consistently improve the mix. A four-ary queue did not consistently beat the exact binary heap. GPU and Rust alternatives require another compatibility implementation and have no demonstrated advantage on this measured mix. Primary sources and earlier reviews are in ../../RESEARCH.md.\n',
    'Small jobs are still dominated by process/container lifecycle and filesystem variability. The first compiler-profile comparison produced a noisy Windows batch regression; training was balanced across the selected small-job path, and the final startup-inclusive comparison improves all five measured workloads. Linux process results and Windows container results are separate protocols and cannot be substituted for one another. Correctness-only roster pass timings are not controlled repeat comparisons.\n',
    'The user reported approximately 161k for v3 and a 231,024.2407 leading score. Multiplying those scores by a local speedup does not establish a new score. The evaluation hardware, shared Development load, timing boundary and sealed roster differ. V4 must be published under a new public registry digest and evaluated on Codabench to establish rank. No push or upload was performed.\n',
    'Publish the validated `agenthon-t3:v4` image, use its new registry manifest digest, and regenerate `submission-v4.json` and `submission-v4.zip` with the official toolkit. The README contains the commands; old ZIPs identify previous versions.\n']
(out/'REPORT.md').write_text('\n'.join(lines)+'\n',encoding='utf8')
print(json.dumps({k:summary[k] for k in ['image_id','image_bytes','public_units_passed','markets_exact','unseen_variants_exact','exact_random_draws']},indent=2))
