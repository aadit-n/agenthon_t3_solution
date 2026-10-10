"""Produce a reviewable native-optimization report from completed test artifacts."""
import json
from pathlib import Path
import statistics


def roster(folder):
    runs=json.loads((folder/'run-times.json').read_text())
    declared=[]
    full=[]
    for run in runs:
        out=folder/run['unit']
        batch=(out/'batch_events.json').exists()
        events=json.loads((out/('batch_events.json' if batch else 'events.json')).read_text())
        declared.append(events['events_per_sec'])
        full.append(events.get('n_events',events.get('total_events'))/run['container_elapsed_sec'])
    return {'units':len(runs),'self_reported_events_per_sec':statistics.mean(declared),
            'container_events_per_sec':statistics.mean(full),'protocol':'single complete public pass, not repeat medians'}


def main():
    root=Path(__file__).resolve().parents[1]
    validation=json.loads((root/'results/v2-public/validation.json').read_text())
    assert validation['passed']==71 and validation['failed']==0
    perf=json.loads((root/'results/v2-performance/performance.json').read_text())
    comparison=json.loads((root/'results/v2-vs-v1/benchmark.json').read_text())
    assert len(comparison['units'])==4
    assert perf['image_id']==validation['image_id']==comparison['images']['candidate']
    variants=json.loads((root/'results/v2-variants/validation.json').read_text())
    assert len(variants)==6 and all(case['passed'] for case in variants)
    previous=roster(root/'results/final-public')
    current=roster(root/'results/v2-public')
    facts=[unit['stylized_facts'] for unit in validation['units'] if unit.get('stylized_facts')]
    assert len(facts)==12 and all(all(value==0 for value in row.values()) for row in facts)
    cases={case['case']:case['summary'] for case in perf['cases']}
    report={'image_id':perf['image_id'],'image_label':'agenthon-t3:v2','rankable':False,
            'public_units_passed':71,'markets_exact':sum(len(row['subs']) if 'subs' in row else 1 for row in validation['units']),
            'events_exact':sum(row['events'] for row in validation['units']),
            'v1_public':previous,'v2_public':current,'roster_rate_speedup':current['self_reported_events_per_sec']/previous['self_reported_events_per_sec'],
            'target_score':231024.2407,'comparison':comparison['units'],'capacity':cases,
            'calibration_units_with_four_zero_divergences':12,'unseen_variants_exact':6}
    out=root/'results/v2-optimization'
    out.mkdir(parents=True,exist_ok=True)
    (out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    lines=[
        'V2 replaces the Python-object event loop, exchange and order book with C++17 state and typed Cython dispatch. It passes the complete public suite and is substantially faster than the submitted v1 image on the same local machine. These are local measurements; a new Codabench upload is needed to establish its score.\n',
        f"Tested image: `agenthon-t3:v2`, `{perf['image_id']}`. CPU: {perf['environment']['cpu_model']}; four CPUs, 16 GiB RAM, offline, no GPU used.\n",
        f"Correctness: **71/71 public units**, **{report['markets_exact']} exact markets**, **{report['events_exact']:,} exact exchange events**, all message-ledger rows exact, and all four calibration divergences zero on all 12 calibration units. Six extra scenarios with unseen seeds/parameters match ABIDES exactly. Native RNG testing compares 180,000 draw triplets, including cached Gaussian states and wide integer bounds. Nullable nanoseconds and oracle/global-seed parity also pass.\n",
        f"The complete public single pass averaged **{current['self_reported_events_per_sec']:,.0f} self-reported events/s**, versus **{previous['self_reported_events_per_sec']:,.0f}** for v1 (**{report['roster_rate_speedup']:.2f}x**). Its full-container arithmetic mean was **{current['container_events_per_sec']:,.0f} events/s**. The self-reported figure is numerically **{current['self_reported_events_per_sec']/231024.2407:.2f}x** the user-provided 231,024.2407 target. The public roster, hardware and clocks are not certified equivalent to the organizer's run.\n",
        'Three measured repeats after one warmup, alternating v1/v2 sequentially; Docker daemon start-to-exit elapsed time includes startup and output writing:\n',
        '| Workload | v1 events/s | v2 events/s | Speedup |',
        '|---|---:|---:|---:|']
    for unit in comparison['units']:
        lines.append(f"| {unit['unit']} | {unit['baseline_median']:,.0f} | {unit['candidate_median']:,.0f} | {unit['speedup']:.2f}x |")
    lines += ['\nMeasured capacity in the separate diagnostic run:\n',
              '| Workload | Events/s | Messages/s | Order commands/s | Container seconds | Peak cgroup MiB |',
              '|---|---:|---:|---:|---:|---:|']
    for name,case in cases.items():
        lines.append(f"| {name} | {case['events_per_sec']['median']:,.0f} | {case['messages_per_sec']['median']:,.0f} | {case['order_commands_per_sec']['median']:,.0f} | {case['container_sec']['median']:.3f} | {case['peak_cgroup_memory_bytes']/2**20:.1f} |")
    lines += [
        '\nThe small batch showed overlapping timing ranges for two and four workers; four remains the default for general batch workloads. `--workers 1/2/4` are available and all produced identical bytes. Single-market capacity depends on horizon, population, and agent mix; these are simulation events, not HTTP requests.\n',
        'Changes: native min-heap with the original full tuple ordering; price-sorted C++ maps, FIFO lists, constant-time order lookup/cancellation and cached depth totals; scalar message/order snapshots; native mixed RandomState-compatible noise draws; buffered private latency draws; compact native ledger/trace vectors; direct Arrow buffers with nullable integer metadata; lean startup and compiled unmodified sparse-oracle methods; two parallel independent Parquet writes; string dictionaries and delta-packed integer columns.\n',
        'C++17 supplies the containers and scalar state; typed Cython integrates the exact oracle and NumPy RNG without a JIT or approximate numerical rewrite. Generic amd64 builds use `-O3` and disable floating-point contraction in the native RNG; fast-math is not enabled. The old object engine remains available through `T3SIM_ENGINE=object`. `T3SIM_IO_THREADS=1` and `T3SIM_PARQUET_ENCODING=legacy` reproduce output ablations.\n',
        'The largest-case ablations measured about 37% more full-container throughput from parallel writing and 41% more from the delta layout. Each decision has three measured alternating repeats in `../native-io-ablation/benchmark.json` and `../native-encoding-ablation/benchmark.json`.\n',
        'Validation uses fresh one-GiB Linux scorer containers with up to two GiB swap to avoid the local Windows paging failure encountered by the long-lived checker. Simulator performance tests retain the competition four-CPU/16-GiB/no-network caps. Completed results are bound to immutable image IDs, and isolated scoring can resume without rerunning already passing units.\n',
        'To submit v2, push `agenthon-t3:v2` to a new GHCR tag, copy the new registry digest, regenerate `submission.json`, and repack with the toolkit and your Team Key. Existing ZIPs identify the previous image.\n']
    (out/'REPORT.md').write_text('\n'.join(lines)+'\n',encoding='utf8')
    print(out/'REPORT.md')


if __name__=='__main__':
    main()
