"""Bind v7 release evidence to its immutable image and embedded source files."""
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
import tarfile

root = Path(__file__).resolve().parents[1]
results = root / 'results'
destination = results / 'v7-optimization'
destination.mkdir(exist_ok=True)


def read(path):
    return json.loads((results / path).read_text())


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


audit = read('v7-optimization/runtime-audit.json')
image = json.loads(subprocess.check_output(['docker', 'image', 'inspect', 'agenthon-t3:v7-candidate']))[0]
assert image['Id'] == audit['image_id']
with tarfile.open(destination / 'runtime.tar') as archive:
    for path in sorted((root / 'native').glob('*')):
        assert path.is_file()
        assert hashlib.sha256(archive.extractfile('opt/relink/native/' + path.name).read()).hexdigest() == sha(path), path
    for name in ('build_lite.sh', 'build_release.sh', 'pgo_scenarios.py', 'prepare_runtime.sh'):
        assert hashlib.sha256(archive.extractfile('opt/relink/scripts/' + name).read()).hexdigest() == sha(root / 'scripts' / name), name
    assert hashlib.sha256(archive.extractfile('opt/relink/Dockerfile').read()).hexdigest() == sha(root / 'Dockerfile')

public = read('v7-final-public/validation.json')
exact = read('v7-final-public/exact.json')
variants = read('v7-final-variants/validation.json')
random = read('v7-final-random/validation.json')
native = read('v7-final-native/comparison.json')
lifecycle = read('v7-final-lifecycle/benchmark.json')
capacity = read('v7-final-performance/performance.json')
assert public['image_id'] == exact['image_id'] == random['image_id'] == capacity['image_id'] == image['Id']
assert lifecycle['images']['v7']['Id'] == image['Id']
assert native['binaries']['v7'] == audit['binary_sha256']
assert native['binaries']['v5'] == '46bc0d1b8e83fb45580e32fb94e64eb4ca09b38dcefcf318ef6fa17c2edb3edd'
assert len(public['units']) == 71 and all(row['passed'] for row in public['units'])
assert exact['markets'] == 95 and exact['all_exact']
assert len(variants) == 18 and all(row['passed'] for row in variants)
assert random['draws'] == 630000 and all(row['exact'] for row in random['checks'])
assert len(native['units']) == 71 and all(all(row['same_parquet_bytes'].values()) for row in native['units'])
assert all(all(row['same_parquet_bytes'].values()) for row in lifecycle['units'])
assert len(capacity['cases']) == 6 and all(len(case['runs']) == 3 for case in capacity['cases'])
worker_hashes = [case['runs'][0]['parquet_hashes'] for case in capacity['cases'] if case['workers']]
assert all(binding == worker_hashes[0] for binding in worker_hashes)

means = {name: statistics.mean(row['modes'][name]['events_per_sec'] for row in native['units']) for name in ('v5', 'v7')}
life_means = lifecycle['mean_per_unit_median_rates']
summary = {'rankable': False, 'image_label': 'agenthon-t3:v7', 'image_id': image['Id'],
           'image_bytes': image['Size'], 'binary_sha256': audit['binary_sha256'],
           'public_units_passed': 71, 'markets_exact': exact['markets'],
           'events_exact': exact['events'], 'messages_exact': exact['messages'],
           'variants_exact': 18, 'numpy_distribution_values_exact': random['draws'],
           'v5_parquet_bytes_identical_on_all_public_units': True,
           'native_means': means, 'native_speedup': means['v7'] / means['v5'],
           'container_cli_subset_means': life_means,
           'container_cli_subset_speedup': life_means['v7'] / life_means['v5'],
           'container_subset_units': len(lifecycle['units']),
           'capacity': {case['case']: case['summary'] for case in capacity['cases']}}
(destination / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
(results / 'SUMMARY.json').write_text(json.dumps(summary, indent=2) + '\n')
source_paths = [root / name for name in ('Dockerfile', '.dockerignore', 'requirements.txt', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'SOURCE_PROVENANCE.json')]
source_paths += sorted((root / 'native').glob('*'))
source_paths += [root / 'scripts' / name for name in ('build_lite.sh', 'build_release.sh', 'pgo_scenarios.py', 'prepare_runtime.sh')]
(destination / 'source-files.json').write_text(json.dumps({str(path.relative_to(root)).replace('\\', '/'): sha(path) for path in source_paths}, indent=2) + '\n')

text = [
    'V7 changes executable startup and trace-row storage while retaining v5 market behavior and Parquet bytes. The user reported v5 at approximately 194k and the reverted output experiment at approximately 173k. These local results do not establish a Codabench score or that 231,024.2407 has been beaten.\n',
    f"Release image: `{image['Id']}`. Static executable SHA256: `{audit['binary_sha256']}`. Runtime {image['Size'] / 2**20:.1f} MiB versus v5 179.2 MiB. Linux/amd64, interface 2.0. A registry manifest digest from a new GHCR push is still required for submission.\n",
    'Selected: GCC/C++17, static non-PIE linking, exact FP ordering, LTO/synthetic PGO, 48-byte trace rows (previously 56), direct executable entrypoint and a small assembled runtime. Trace timestamps/prices/sizes/order IDs/ranks remain full-width. The image contains a small glibc/loader/ldconfig set for injected utilities, notices and application rebuild sources; no runtime Python, compiler, generated training data, profiles or reference outputs. GPU-enabled worker launch cannot be tested on this local CPU-only machine.\n',
    'The event loop, scalar RNG stream, radix scheduler, pooled book, open-order container, v5 codecs/compression/page layout, 16,384-row trace streaming threshold, 65,536-row ledger blocks and both SHA256 fields are retained.\n',
    '## Validation\n',
    f"71/71 public developer verifier units; 95 exact markets with {exact['events']:,} exchange rows and {exact['messages']:,} message rows; exact nullable Int64 metadata/pandas roundtrips; 18 additional seeded/adversarial scenarios with streaming and latency preparation forced on; 630,000 distribution values against pinned Linux NumPy. The 1/2/4-worker batch outputs are byte identical.\n",
    'Every Parquet file across all 71 units also matches v5 byte for byte in every repeated native trial. Independent hashing includes the full message ledger. The exported runtime is audited and embedded application/build sources match the working source hashes.\n',
    'Experiments additionally passed ASAN/UBSAN tests: 643,332 book actions, 300,000 open-order actions and five million MT words; 1,400 tied-sort cases/156,400 rows, backward-time fallback and 1.2 million momentum observations. These cover alternatives; only selected flags are active.\n',
    '## Native process comparison\n',
    f"All 71 units, seven measured fresh processes/version after one warmup, balanced rotated/reversed ordering; executable/configuration/output on Linux storage. Startup, simulation, closure, hashing and exit are timed; hashing/retention occurs afterward. Mean per-unit median rates: v5 {means['v5']:,.0f}, v7 {means['v7']:,.0f} events/s: **{(summary['native_speedup'] - 1) * 100:.1f}% gain**. Some units regress; no uniform improvement is claimed.\n",
    '| Unit | v5 process events/s | v7 process events/s | Ratio |',
    '|---|---:|---:|---:|']
for row in native['units']:
    if row['unit'] in ('t3-as01-base-mix', 't3-as03-mm-liquidity-heavy', 't3-as04-value-heavy', 't3-gb-base-30agent-30s', 't3-gb-mega-throughput', 't3-gbatch-homog-8', 't3-mr-deep-book-state-size'):
        a, b = (row['modes'][name]['events_per_sec'] for name in ('v5', 'v7'))
        text.append(f"| {row['unit']} | {a:,.0f} | {b:,.0f} | {b/a:.3f} |")
text += ['\n## Complete container comparison\n',
         f"Eleven measured sequential repetitions/version after one warmup on seven workloads; persistent Linux volume; complete host `docker run --rm` wall clock, including local CLI/transport overhead. Artifact inspection/copy occurs after removal. Subset mean per-unit median rates: v5 {life_means['v5']:,.0f}, v7 {life_means['v7']:,.0f}: **{(summary['container_cli_subset_speedup'] - 1) * 100:.1f}% gain**. This clock is not the native clock or an official Development measurement. Daemon start-to-die timestamps are retained separately.\n",
         '| Unit | v5 CLI lifecycle events/s | v7 CLI lifecycle events/s | Ratio |',
         '|---|---:|---:|---:|']
for row in lifecycle['units']:
    a, b = (row['modes'][name]['events_per_sec'] for name in ('v5', 'v7'))
    text.append(f"| {row['unit']} | {a:,.0f} | {b:,.0f} | {b/a:.3f} |")
text += ['\n## Capacity and resources\n',
         'Three measured repeats plus a warmup; readonly-mounted diagnostic shell/cat collect cgroup CPU/memory, with wrapper/mount overhead included. Six cases, exact first-run references and stable hashes; detailed event/message/order rates and ranges in ../v7-final-performance/REPORT.md. Environment collection runs in a separate diagnostic image, not a Python interpreter in the submission.\n',
         '## Limits and alternatives\n',
         'Local CPU and Docker Desktop clocks differ from the documented Xeon/B200 fleet. Current Development ranks harness-verified event counts over container creation/removal on a shared host; self-reported throughput is not ranked. Multiplying the 194k score by native gains is not a valid prediction. No GHCR push, authenticated ZIP or Codabench upload was performed.\n',
         'Small native-only packaging using the unchanged dynamic v5 binary, alternate pooled price indexes/tree fallbacks, tombstone open orders, SIMD/buffered MT, larger bounded price caches, exact oracle coefficient caching, canonical tied-sort shortcuts and momentum rings were tested separately. Whole-roster selection chose the compact static engine: the most complex combination added only 0.2% over it. Alternative build macros remain disabled. Raw reports and source snapshots are retained under ../v7-tuning.\n',
         'Primary references: [current organizer timing](https://github.com/Agenthon-2026/track3-simulation-public/blob/main/README.md#how-throughput-is-measured), [minimal Docker images](https://docs.docker.com/build/building/base-images/), [GCC linking options](https://gcc.gnu.org/onlinedocs/gcc/Link-Options.html). See ../../RESEARCH.md for the research and decisions.\n']
(destination / 'REPORT.md').write_text('\n'.join(text) + '\n')
print(json.dumps({key: value for key, value in summary.items() if key != 'capacity'}, indent=2))
