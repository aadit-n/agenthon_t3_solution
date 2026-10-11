"""Bind v5 release correctness and repeated performance to immutable artifacts."""
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

root = Path(__file__).resolve().parents[1]
def read(path): return json.loads((root / path).read_text(encoding='utf-8-sig'))
def info(tag): return json.loads(subprocess.check_output(['docker', 'image', 'inspect', tag]))[0]

release = info('agenthon-t3:v5')
previous = info('agenthon-t3:v4')
validation = read('results/v5-final-public/validation.json')
exact = read('results/v5-final-public/exact.json')
variants = read('results/v5-final-variants/validation.json')
variant_binding = read('results/v5-final-variants/image.json')
random = read('results/v5-final-random/validation.json')
codec = read('results/v5-tuning/codec/validation.json')
comparison = read('results/v5-final-vs-v4/benchmark.json')
performance = read('results/v5-final-performance/performance.json')
native = read('results/v5-final-native/comparison.json')
audit = read('results/v5-optimization/runtime-audit.json')
assert validation['image_id'] == exact['image_id'] == variant_binding['candidate'] == random['image_id'] == performance['image_id'] == comparison['images']['candidate'] == release['Id']
assert comparison['images']['baseline'] == previous['Id']
assert validation['passed'] == 71 and validation['failed'] == 0 and exact['all_exact']
assert len(variants) == 18 and all(row['passed'] for row in variants)
assert codec['all_exact'] and codec['cases'] == 18
assert len(native['units']) == 71 and all(all(row['same_parquet_bytes'].values()) for row in native['units'])
binary = subprocess.check_output(['docker', 'run', '--rm', '--network', 'none', '--entrypoint', 'python', 'agenthon-t3:v5', '-c',
    "import hashlib;print(hashlib.sha256(open('/opt/t3sim-cpp','rb').read()).hexdigest())"], text=True).strip()
assert binary == native['binaries']['v5']
assert binary == audit['binary_sha256']
assert all(digest == hashlib.sha256((root / name).read_bytes()).hexdigest()
           for name, digest in audit['license_sha256'].items())
assert hashlib.sha256((root / 'results/v5-tuning/binaries/t3sim-release-v5').read_bytes()).hexdigest() == binary
assert native['binaries']['v4'] == 'd6dc218c174cfc1a0ac1672fb021e852d72342379b4276d2f6cba291f83f8adc'
assert 'passed' in (root / 'results/v5-tuning/structures-release.txt').read_text()
assert '12000' in (root / 'results/v5-tuning/streams-release.txt').read_text()
calibration = [row for row in validation['units'] if row['stylized_facts']]
assert all(all(value == 0 for value in row['stylized_facts'].values()) for row in calibration)
means = {}
for mode in ('v4', 'v5'):
    means[mode] = {key: statistics.mean(row['modes'][mode][key] for row in native['units'])
                  for key in ('events_per_sec', 'median_reported_events_per_sec')}
ratio = means['v5']['events_per_sec'] / means['v4']['events_per_sec']
reported_ratio = means['v5']['median_reported_events_per_sec'] / means['v4']['median_reported_events_per_sec']
cases = {row['case']: row['summary'] for row in performance['cases']}
summary = {'rankable': False, 'image_label': 'agenthon-t3:v5', 'image_id': release['Id'],
    'binary_sha256': binary, 'v4_image_id': previous['Id'], 'image_bytes': release['Size'],
    'public_units_passed': 71, 'markets_exact': exact['markets'], 'events_exact': exact['events'],
    'messages_exact': exact['messages'], 'calibration_units_with_zero_divergences': len(calibration),
    'unseen_variants_exact': len(variants), 'exact_random_draws': random['draws'], 'codec': codec,
    'worker_lifetimes_checked': 12000, 'runtime_audit': audit, 'native_roster_means': means, 'native_roster_speedup': ratio,
    'native_roster_reported_speedup': reported_ratio, 'comparison': comparison['units'], 'capacity': cases,
    'user_reported_v4_codabench_score': 178000, 'user_target': 231024.2407,
    'limitations': 'Local, non-rankable timings on public configurations. Different storage/timing protocols remain separate. No v5 Codabench score or rank established.'}
out = root / 'results/v5-optimization'
out.mkdir(parents=True, exist_ok=True)
files = ['Dockerfile', '.dockerignore', 'SOURCE_PROVENANCE.json', 'THIRD_PARTY_NOTICES.md',
         'scripts/build_release.sh', 'scripts/build_lite.sh', 'scripts/pgo_scenarios.py', 'scripts/entrypoint_cpp.sh']
files += [str(path.relative_to(root)).replace('\\', '/') for path in sorted((root / 'native').glob('*')) if path.is_file()]
(out / 'source-files.json').write_text(json.dumps({path: hashlib.sha256((root / path).read_bytes()).hexdigest() for path in files}, indent=2) + '\n')
(out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
(root / 'results/SUMMARY.json').write_text(json.dumps(summary, indent=2) + '\n')

lines = [
    'V5 reduces native scheduling, state and output costs while preserving every public market event and causal message. It also fixes a rare worker-shutdown hang found during repeated trials. A new Codabench evaluation is required to establish its score.\n',
    f"Release: `agenthon-t3:v5` and `agenthon-t3:latest`. Local image identity: `{release['Id']}`. Binary SHA256: `{binary}`. Runtime size: {release['Size']/2**20:.1f} MiB. A local image identity is not a substitute for the registry manifest digest from a new GHCR push.\n",
    f"Local CPU: {performance['environment']['cpu_model']}; four-CPU quota, offline, no GPU attached. The requested runtime memory cap is 16 GiB; this Docker VM reports about 7.7 GiB total RAM. Resource limits and CPU affinity for native trials are recorded in their JSON.\n",
    'The offline runtime audit confirms the executable hash, matching source provenance/licenses, native linked libraries, and absence of compiler profiles, generated training workloads, runtime compiler, Arrow, NumPy and pandas. Python remains a base diagnostic tool; the simulator entrypoint runs the native executable.\n',
    f"Correctness: **71/71 actual public developer-verifier units**, **{exact['markets']} exact markets**, **{exact['events']:,} trace rows**, **{exact['messages']:,} message rows**. All four realism divergences are zero on {len(calibration)} calibration units. **18 unseen seeds/parameters** match with trace streaming, latency prefetch and experimental noise prefetch forced on. **{random['draws']:,} mixed draws** match pinned Linux NumPy 1.26.4.\n",
    'Independent Arrow decoding verifies 18 codec boundary cases and 721,782 rows, including full-width signed values, overflow deltas, UTF8, nulls, multiple row groups, hashes and nullable pandas Int64. Address/undefined-behavior sanitizers cover the diagnostic generator, structures and worker tests. Differential tests cover 300,000 book actions, 1,000,000 four-ary heap operations, 3,000,000 radix operations, 100,000 independently ordered signed routing entries, 300,000 open-order actions, 400,000 rounding cases, 100,000 prepared bounded draws, stable message addresses and randomized recipient-scheduler stop boundaries. The optional recipient queue is not selected.\n',
    'The stream regression exercises 12,000 worker lifetimes with exact buffer refill sequences, early shutdown and repeated stop. Two Docker restarts interrupted diagnostic compilation/scoring under low available host memory; completed evidence was retained. Scoring resumed in smaller isolated containers using the same immutable participant outputs. All final tests below completed.\n',
    f"The complete **71-unit** comparison runs the actual v4/v5 executables on Linux container storage, rotating/reversing order with one excluded warmup and {native['runs']} measured fresh processes per version per unit. Process startup, simulation, output closure, hashing and exit are timed. Artifact hashing/retention occurs afterward. Every repeated Parquet file is byte stable, and v5 bytes equal v4 for every unit.\n",
    f"Unweighted mean of per-unit median process rates: v4 **{means['v4']['events_per_sec']:,.0f}**, v5 **{means['v5']['events_per_sec']:,.0f} events/s**, **{(ratio-1)*100:.1f}% improvement**. The separate mean of self-reported rates improves {(reported_ratio-1)*100:.1f}%. These are local public-roster summaries, not an estimate of the Codabench aggregate.\n",
    '| Workload | v4 process events/s | v5 process events/s | Speedup |', '|---|---:|---:|---:|']
selected = {'t3-as01-base-mix', 't3-gb-base-30agent-30s', 't3-gb-mega-throughput', 't3-gbatch-homog-8', 't3-mr-deep-book-state-size'}
for row in native['units']:
    if row['unit'] in selected:
        a = row['modes']['v4']['events_per_sec']; b = row['modes']['v5']['events_per_sec']
        lines.append(f"| {row['unit']} | {a:,.0f} | {b:,.0f} | {b/a:.2f}x |")
lines += ['\nA separate Windows-output-mount protocol measures Docker StartedAt-to-FinishedAt over seven alternating runs after one warmup. It includes container startup and shutdown. Small jobs can be dominated by this lifecycle and filesystem variability.\n',
          '| Workload | v4 container events/s | v5 container events/s | Speedup |', '|---|---:|---:|---:|']
for row in comparison['units']:
    lines.append(f"| {row['unit']} | {row['baseline_median']:,.0f} | {row['candidate_median']:,.0f} | {row['speedup']:.2f}x |")
lines += ['\nCapacity: three complete-container trials after one warmup, including a telemetry shell wrapper. Exact reference values and byte stability are required. Batch bytes are identical across 1/2/4 workers.\n',
          '| Workload | Events/s | Messages/s | Order commands/s | Seconds | Peak cgroup MiB |', '|---|---:|---:|---:|---:|---:|']
for name, c in cases.items():
    lines.append(f"| {name} | {c['events_per_sec']['median']:,.0f} | {c['messages_per_sec']['median']:,.0f} | {c['order_commands_per_sec']['median']:,.0f} | {c['container_sec']['median']:.3f} | {c['peak_cgroup_memory_bytes']/2**20:.1f} |")
lines += [
    '\nThese rates count simulation events, messages and order commands, not HTTP requests. Complete repeat ranges, CPU usage, counts, memory and simulated-time speed are retained in the JSON reports. Local timing noise limits precision, especially for short jobs; three capacity repeats do not establish tail-latency or reliability guarantees.\n',
    'Selected changes: exact radix scheduling, 88-byte messages, signed packed routes, contiguous MarketMaker open orders, direct price-level caches, prepared rejection masks, SSE2 rounding, a cached oracle coefficient, specialized categorical/sequence encoding, direct SHA256, fused delta buffers and uninitialized scratch storage whose bytes are fully written. The default remains generic amd64, Snappy, C++17, balanced synthetic PGO, LTO and exact floating-point ordering. Causal callbacks remain sequential; spare cores handle independent draws and finalized output.\n',
    'Alternatives tested but not selected include recipient-head scheduling, Clang 19, CPU-specific/reused-scratch Snappy, AVX2 delta dispatch, stable message references, precomputed noise actions, plain numeric pages, indexed value materialization and one-pass column gathering. Their workload-dependent gains did not consistently improve the mix. Source research and decisions are in ../../RESEARCH.md.\n',
    'The user reported v4 at approximately 178k and a target of 231,024.2407. Multiplying that score by any local speedup does not establish a new score. Shared Development load, worker hardware, timing boundaries and the evaluation roster differ. No v5 registry push or Codabench upload was performed.\n',
    'Publish the validated v5 image under a new public GHCR tag, use the new registry manifest digest, and regenerate submission-v5.json and submission-v5.zip with the official toolkit. README.md records the commands.\n']
(out / 'REPORT.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(json.dumps({key: summary[key] for key in ('image_id', 'binary_sha256', 'public_units_passed', 'native_roster_speedup', 'native_roster_reported_speedup')}, indent=2))
