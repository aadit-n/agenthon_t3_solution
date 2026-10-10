Your task is to submit a Docker image containing a faster stock-exchange simulator. It receives a scenario describing agents and market conditions, runs the simulation offline, and writes the exchange events and message ledger. Speed earns a score only after the simulator passes every required correctness and realism check. The executable gates and current README take precedence over older prose or historical throughput claims.

## Deliverable and runtime

- Target platform: `linux/amd64`; image label `qfbench2.interface_version="2.0"`.
- `simulate --config /input/scenario.json --out /output/trace.parquet`.
- `simulate-batch --batch-dir /input/scenarios --out-dir /output`.
- Single output: `trace.parquet`, `message_trace.parquet`, and `events.json`.
- Batch output: the same files per sub-scenario, plus `batch_events.json`.
- Four CPU cores, 16 GiB RAM, offline network, one attached GPU. GPU use is optional. Runtime dependencies must already be in the image.
- Planned Final run ceiling: 300 seconds. Development has a distinct 1,800-second fallback.
- Hardware details are recorded by the organizers in [baselines/README.md](../track3-simulation-public/baselines/README.md); that table names the B200 GPU and Xeon Platinum 8570 CPU. Historical speed figures were not measured on that fleet.

`trace.parquet` has seven typed columns: `t_ns:int64`, `agent_id:int32`, `msg_type:string`, `side:string`, `price:int64`, `size:int64`, `order_id:int64`. Events include submissions, acceptances, fills/partial fills, cancellations, replacements, and quote updates. The message ledger additionally records send/receive times, latency, source/destination, message ID, processing sequence, order ID, and causal parent.

`events.json` must identify the scenario and seed, count actual trace rows, report real elapsed seconds and the consistent rate, and hash the emitted trace. The implementation also hashes the ledger and reports real peak RSS and zero GPU time. Batch totals must match all actual per-market row counts.

## What must pass

The public release contains **65 individual scenarios and six batch units**, with 30 sub-scenarios across those batches. A sealed set is also evaluated; its size and parameters are not supplied. Every scenario must pass.

| Tier | Families | Meaning |
|---|---|---|
| A | Matching engine, latency profile, throughput scale, exchange protocol, reactive agents | Exact event coverage and fill sequence, unchanged prices/sizes, Kendall tau at least 0.999 by default, timestamps within the card's tolerance (normally 1,000 ns). |
| B | Agent mix, market stress, calibration/stylized facts | Return-distribution KS at most 0.08 and spread difference within the declared limit, normally 10 bps. |

Stress scenarios in this release use the published `agent-mix` family identifier; dense matching variants use `matching-engine-semantics`. Read each card rather than classifying units by their slug.

The Family 5 realism checks are KS ≤ 0.08, absolute-return ACF RMS error ≤ 0.12, Hill tail-exponent error ≤ 1.5, and depth-histogram JS divergence ≤ 0.10. Message causality and reference-relative ledger checks apply wherever the card requires them: 59 of the 71 top-level cards do. Every batch sub-scenario requires a ledger and must preserve isolated behavior. Exchange-protocol scenarios also check execution-report fidelity, including self-trade prevention and response delays.

## How speed counts

Current Development scores use checked self-reported rates and are explicitly provisional (`rankable=false`). Planned Final rates use organizer timing and actual row counts, with the median over the measured repeats specified in the evaluation plan. Stable trace and ledger outputs must repeat byte for byte; timing JSON may vary.

The aggregate is the **arithmetic mean of the per-unit rates across the complete evaluation roster**, not just the largest throughput scenario. Startup, imports, output writing, and hashing therefore matter, especially on short scenarios. The historical ABIDES and vectorized-reference figures are not admission thresholds or performance guarantees.

## Submission steps

Build and test the image, push it to a registry permitting anonymous digest pulls, then create a sealed descriptor with `track="simulation"`, `category="simulator"`, `models=[]`, the correct phase/competition ID, and the registry digest. Use the organizer toolkit to pack `submission.zip` with the team claim. Do not submit a bare image name or guess a team alias. The README includes a descriptor-generation helper and exact build commands; registry publication and account submission are still user actions.

The published Development deadline is October 12, 2026 at 23:59 AoE, equivalent to **October 13 at 17:29 IST**. Joint Final + Verification closes October 25 at 23:59 AoE, equivalent to **October 26 at 17:29 IST**. Each team makes one Final submission per track. Source: [published submission contract](https://github.com/Agenthon-2026/track3-simulation-public/blob/main/SUBMISSION_CLI.md).

Primary references: the local [README](../track3-simulation-public/README.md), [CLI contract](../track3-simulation-public/SUBMISSION_CLI.md), [semantic checks](../track3-simulation-public/qfbench2_track_simulation/semantics.py), [scorer](../track3-simulation-public/qfbench2_track_simulation/scoring.py), and [batch gate](../track3-simulation-public/qfbench2_track_simulation/batch.py).
