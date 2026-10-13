"""Verify candidate evidence and emit the execution optimization report."""
import hashlib
import json
from pathlib import Path
import statistics
import tarfile


def main():
    root = Path(__file__).resolve().parents[1]
    output = root / 'results/execution-specialization'
    read = lambda name: json.loads((output / name).read_text())
    audit = read('runtime-audit.json')
    roster = read('roster/comparison.json')
    second = read('narrow-roster/comparison.json')
    lifecycle = read('lifecycle/benchmark.json')
    public = read('public/validation.json')
    exact = read('public/exact.json')
    workers = read('workers/validation.json')
    image_id = audit['image_id']
    assert roster['binaries']['combined'] == second['binaries']['combined'] == audit['binary_sha256']
    assert lifecycle['images']['combined']['Id'] == public['image_id'] == exact['image_id'] == workers['image_id'] == image_id
    assert len(public['units']) == 71 and all(u['passed'] for u in public['units'])
    assert exact['all_exact'] and exact['markets'] == 95
    assert workers['all_exact'] and len(workers['checks']) == 6
    for folder in ('variants', 'generic-variants'):
        checks = read(folder + '/validation.json')
        assert read(folder + '/image.json')['candidate'] == image_id
        assert len(checks) == 18 and all(c['passed'] for c in checks)
    for name in ('screen', 'roster', 'narrow-screen', 'narrow-roster', 'reuse-screen'):
        report = read(name + '/comparison.json')
        assert all(all(u['same_parquet_bytes'].values()) for u in report['units'])
    assert all(all(u['same_parquet_bytes'].values()) for u in lifecycle['units'])
    # The measured executable was built before the disabled queue/reuse
    # experiments. Bind its rebuild sources to that immutable source snapshot.
    bindings = {'native/' + p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                for p in (output / 'snapshot-native').iterdir() if p.is_file()}
    for name in ('Dockerfile', 'Dockerfile.execution', 'scripts/build_lite.sh', 'scripts/build_release.sh',
                 'scripts/pgo_scenarios.py', 'scripts/pgo_specialized.py', 'scripts/prepare_runtime.sh'):
        bindings[name] = hashlib.sha256((root / name).read_bytes()).hexdigest()
    with tarfile.open(output / 'runtime.tar') as archive:
        for name, digest in bindings.items():
            assert hashlib.sha256(archive.extractfile('opt/relink/' + name).read()).hexdigest() == digest
    (output / 'candidate-source-bindings.json').write_text(json.dumps(bindings, indent=2) + '\n')
    rates = {name: statistics.mean(u['modes'][name]['events_per_sec'] for u in roster['units']) for name in roster['binaries']}
    second_rates = {name: statistics.mean(u['modes'][name]['events_per_sec'] for u in second['units']) for name in second['binaries']}
    cli = lifecycle['mean_per_unit_median_rates']
    summary = {'rankable': False, 'candidate_label': audit['image_label'], 'image_id': image_id,
        'binary_sha256': audit['binary_sha256'], 'image_bytes': audit['image_bytes'],
        'active_experiments': ['T3_SPECIALIZE_LATENCY', 'T3_SPECIALIZE_PROTOCOL'],
        'disabled_experiments': ['T3_LAZY_STP_FRONT', 'T3_NARROW_SCHEDULED', 'T3_REUSE_ROW_BUFFERS'],
        'default_submission': 'agenthon-t3:v7', 'reported_v7_codabench_score_approx': 199000,
        'candidate_codabench_score': None, 'native_mean_rates': rates,
        'native_ratio_to_v7': rates['combined'] / rates['v7'],
        'second_native_mean_rates': second_rates,
        'second_native_ratio_to_v7': second_rates['combined'] / second_rates['v7'],
        'cli_mean_rates': cli, 'cli_ratio_to_v7': cli['combined'] / cli['v7'],
        'public_units_passed': 71, 'exact_markets': 95, 'extra_case_checks': 36, 'batch_worker_checks': 6,
        'source_bindings_verified': len(bindings)}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    lines = ['# Execution optimization result', '',
        'Implemented configuration-based C++17 specialization. The separately validated `agenthon-t3:v8-candidate` enables latency specialization and the plain protocol path. V7 remains the default release because the complete-container gain is small and no Codabench improvement is established.', '',
        f"Candidate `{image_id}`; static executable SHA256 `{audit['binary_sha256']}`; runtime {audit['image_bytes']/1048576:.2f} MiB. The image retains v7 compatibility libraries, notices, direct entrypoint, output encodings, row groups, compression, both hashes and the seeded RNG contract. No runtime interpreter/compiler, generated training inputs, profiles or reference outputs ship.", '',
        '## Selected changes', '',
        'Choose immutable latency properties once before the event loop: fixed/geographic transport, scalar distribution sampling or prepared latency blocks. Same-sender bypass and exact distribution arithmetic/draw order are preserved. Action-stream latency uses the generic fallback. With STP and acknowledgement delay both zero, compile out acknowledgement-kind checks and the STP-only resting-order read. Computation delays and all callbacks remain. Compiler training adds independently varied synthetic protocol combinations to exercise that path, without reference data.', '',
        '## Verification', '',
        f"71/71 pinned public developer units. All 95 markets match exactly: {exact['events']:,} exchange rows and {exact['messages']:,} ledger rows, including nullable Int64 metadata/pandas roundtrips. Eighteen extra seeded/adversarial cases pass in automatic mode and 18 with the generic loop forced. Homogeneous and heterogeneous batches match v7 bytes at 1/2/4 workers.", '',
        'ASAN/UBSAN checks cover 60 generic-versus-specialized cases (981,690 exchange rows and 1,146,465 ledger rows), all seven latency paths, both protocol paths and action prefetch. Additional disabled experiments passed independent checks: 4,000,000 narrow-queue operations/2,002,051 exact pops with full-width fallback; and 972,416 ledger/409,600 trace rows for bounded reuse, backpressure, deferred labels, empty output and writer failure.', '',
        'All Parquet files match v7 byte for byte throughout every measured native/lifecycle repetition. Candidate rebuild sources are bound to the exact source snapshot compiled into the measured executable; the working source also retains later disabled experiments.', '',
        '## Performance', '',
        f"First all-71 comparison, seven measured repetitions plus warmup: v7 {rates['v7']:,.0f}, rebuilt generic {rates['control']:,.0f}, selected specialization {rates['combined']:,.0f} events/s, a {(rates['combined']/rates['v7']-1)*100:.2f}% mean per-unit median gain. A second all-71 comparison measured {(second_rates['combined']/second_rates['v7']-1)*100:.2f}%. These fresh-process clocks include startup, simulation, closure/hashing and exit on Linux storage.", '',
        f"Eleven measured complete-container repetitions plus warmup on seven workloads: v7 {cli['v7']:,.0f}, specialization {cli['combined']:,.0f}, compact-queue/latency {cli['narrow-latency']:,.0f} events/s. The selected candidate's mean per-unit median change is {(cli['combined']/cli['v7']-1)*100:+.2f}%. This includes local Windows CLI/transport overhead; daemon start-to-die timestamps are stored separately. It is not an official Development or Final score.", '',
        '| Unit | Native ratio to v7 | CLI lifecycle ratio to v7 |', '|---|---:|---:|']
    lookup = {u['unit']: u for u in roster['units']}
    for unit in lifecycle['units']:
        name = unit['unit']; n = lookup[name]['modes']; c = unit['modes']
        lines.append(f"| {name} | {n['combined']['events_per_sec']/n['v7']['events_per_sec']:.3f} | {c['combined']['events_per_sec']/c['v7']['events_per_sec']:.3f} |")
    lines += ['', '## Rejected/default-disabled changes and limits', '',
        'The lazy front-read guard did not beat specialization. Compact queue entries reduce 32-byte storage to 24 bytes and preserve full-width IDs using a checked heap fallback, but their native advantage did not improve the tested CLI mean. Bounded row-buffer reuse preserves every output byte but did not beat the selected specialization in its nine-repeat screening comparison; it stays disabled. These experiments are available behind explicit compile flags.', '',
        'The complete-container result is small enough that hardware/shared-host variation matters. It does not establish a substantial Codabench gain or a score above 231,024.2407. GPU-enabled launch cannot be tested on this CPU-only machine, and public developer checks do not include sealed Final cases. No registry push, authenticated ZIP or submission was performed.', '',
        'To explicitly build the selected candidate from the working source, use `--build-arg "T3_EXTRA_CXX_FLAGS=-DT3_SPECIALIZE_LATENCY -DT3_SPECIALIZE_PROTOCOL" --build-arg T3_SPECIALIZED_PGO=1`. Default build arguments and existing v7/latest image tags remain unchanged. See ../../EXECUTION_OPTIMIZATION.md for implementation and controls.', '']
    (output / 'REPORT.md').write_text('\n'.join(lines))
    print(json.dumps({key: summary[key] for key in ('image_id', 'public_units_passed', 'native_ratio_to_v7', 'cli_ratio_to_v7')}, indent=2))


if __name__ == '__main__':
    main()
