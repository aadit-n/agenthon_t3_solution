This folder contains the optimized Agenthon 2026 Track 3 simulator. V5 runs the complete engine and its Snappy Parquet writer in C++17, with bounded CPU pipelines for large jobs. It preserves the exact reference market behavior. The user reported Codabench scores of approximately 33k, 97k, 161k and 178k for v1 through v4; v5 needs a new Codabench upload to establish its score.

[The v5 report](results/v5-optimization/REPORT.md) records final image identity, controlled comparisons, capacity and limitations. [TRACK_REQUIREMENTS.md](TRACK_REQUIREMENTS.md) explains the contract; [RESEARCH.md](RESEARCH.md) records reviewed implementations, primary sources and architectural decisions. Local rates are not predictions of a leaderboard rank.

Seven repeated Linux process trials per version across all 71 public units measured a 13.6% gain in the mean of median rates, including 41.6% on the largest workload, 20.3% on the eight-market batch and 22.1% on the deep book. The separate Docker lifecycle comparison improved those three cases by 30.6%, 5.5% and 16.3%. Some short process workloads are essentially unchanged. All public trace and ledger values match exactly; all repeated v5 Parquet bytes also match v4.

## Build and run

From this directory, with Docker Desktop running:

```powershell
docker build --platform linux/amd64 -t agenthon-t3:v5 .
```

The default build uses generic amd64, `-O3 -std=c++17 -ffp-contract=off -fno-math-errno`, LTO and profile-guided optimization. Compiler profiles come from generated synthetic scenarios covering small jobs, streamed jobs and four-worker batches. Profiles and generated outputs stay in the compiler stage. No fast-math, runtime compilation or downloads are used.

The executable links Snappy 1.2.2 and system OpenSSL. Simulation needs no Arrow, NumPy, pandas or Python interpreter. Python remains a base diagnostic tool. The older implementations remain available as explicit Docker targets `arrow`, `cython` and `baseline`; the saved v1/v2/v3/v4 images preserve earlier releases.

Single scenario:

```powershell
$ProjectPath = (Get-Location).Path
New-Item -ItemType Directory -Force out | Out-Null
docker run --rm --network none --cpus 4 --memory 16g `
  -v "$ProjectPath/../track3-simulation-public/units/t3-as01-base-mix/scenario.json:/input/scenario.json:ro" `
  -v "$ProjectPath/out:/output" `
  agenthon-t3:v5 simulate --config /input/scenario.json --out /output/trace.parquet
```

Batch input is a directory containing scenario JSON files:

```text
simulate-batch --batch-dir /input/scenarios --out-dir /output [--workers 1..4]
```

The four-CPU budget can run independent markets concurrently. Worker shutdown now changes its stop predicate under the condition-variable mutex, preventing a rare lost notification found during stress testing. A single market processes causal callbacks sequentially; spare cores prepare independent latency draws and encode finalized output blocks. All workers finish, files close and hashes finish before the elapsed time is reported. Market IDs and random state remain isolated.

## Validation

The prepared `.venv-validation` uses Python 3.13 and official `qfbench2-common[data]` v2.5.1. Participant validation containers receive configurations; separate scoring containers read public references and import the unmodified developer verifier.

Final evidence:

- [Official public verifier](results/v5-final-public/validation.json): 71 units.
- [Exact comparisons](results/v5-final-public/exact.json): 95 markets, 7,728,572 exchange rows and 8,183,538 message rows, including pandas nullable-integer roundtrips.
- [Unseen parameters/seeds](results/v5-final-variants/validation.json): 18 cases with trace streaming, latency prefetch and experimental noise prefetch forced on, including busy exchanges, ties, geographic latency, heavy tails, shocks and half rounding.
- [Exact distributions](results/v5-final-random/validation.json): 630,000 draws against Linux NumPy 1.26.4.
- [Encoding boundaries](results/v5-tuning/codec/validation.json): 18 cases and 721,782 rows covering full-width integers, overflow, UTF8, nulls, hashes and multiple row groups; address/undefined-behavior sanitizers used for the diagnostic generator.
- `tests/native_structures.cpp`: 300,000 differential book actions, 3,000,000 radix operations, signed routing extremes, 400,000 rounding cases and open-order/RNG checks with address/undefined-behavior sanitizers. `tests/native_streams.cpp` checks 12,000 worker lifetimes and exact refills.

Reproduce the checks:

```powershell
.\.venv-validation\Scripts\python.exe scripts\run_public.py --image agenthon-t3:v5 `
  --isolated-scoring --output-dir results\v5-final-public
.\.venv-validation\Scripts\python.exe scripts\check_exact_suite.py --output-dir results\v5-final-public
.\.venv-validation\Scripts\python.exe scripts\check_variants.py --candidate agenthon-t3:v5 `
  --extended --output-dir results\v5-final-variants `
  --candidate-env T3SIM_STREAM_TRACE=1 --candidate-env T3SIM_LATENCY_PREFETCH=1 `
  --candidate-env T3SIM_NOISE_PREFETCH=1
.\.venv-validation\Scripts\python.exe scripts\check_cpp_random.py --image agenthon-t3:v5 `
  --output-dir results\v5-final-random
```

Use fresh result directories for a changed image. `run_public.py --score-only --resume-scoring --isolated-scoring` can resume scoring generated outputs when the immutable image binding matches.

## Performance

Run timing tests sequentially on an idle machine. The report distinguishes Docker start-to-exit timing with Windows output mounts from separate executable trials on Linux container storage. Both include real simulation, encoding, file closure and hashing. Neither is an official competition measurement.

```powershell
.\.venv-validation\Scripts\python.exe scripts\benchmark.py `
  --candidate agenthon-t3:v5 --baseline agenthon-t3:v4 --runs 7 `
  --unit t3-as01-base-mix --unit t3-gb-base-30agent-30s `
  --unit t3-gb-mega-throughput --unit t3-gbatch-homog-8 `
  --unit t3-mr-deep-book-state-size --output-dir results\v5-final-vs-v4
.\.venv-validation\Scripts\python.exe scripts\performance.py --image agenthon-t3:v5 `
  --runs 3 --warmups 1 --public-results results\v5-final-public --output-dir results\v5-final-performance
```

Capacity measures exchange events, delivered messages and order commands. These are simulation jobs, not HTTP requests. The 1/2/4-worker test checks identical per-market Parquet bytes across worker counts.

## Why v5 is faster

The book uses recycled contiguous slots, intrusive FIFO links, a dense order-ID index and cached best levels. Ordered price maps support arbitrary int64 prices. V5 adds a direct price-level cache and contiguous MarketMaker open-order lists. Its radix queue buckets monotone timestamps, preserves signed sender/recipient/message-ID ties, and falls back to a general heap for scheduling into the past. Messages shrink from 112 to 88 bytes without losing original arrival latency during postponement.

The flat writer encodes known integer/categorical columns directly and preserves Snappy and pandas Int64 metadata. It avoids Arrow table construction, nested-schema conversion and repeated string lookup. V5 specializes narrow category codes and consecutive ledger sequences, avoids redundant type finalization, fuses delta pages and avoids zeroing overwritten buffers. An independent decoder checks its encodings. The current writer format remains byte-compatible with v4.

Ledgers flush in 65,536-row blocks. Large traces flush completed timestamp groups after 16,384 rows, so no equal-time group or mutable quote is split. Numeric columns can be written during simulation; the type column is finalized after each order's last execution is known. Its late column offsets are recorded in the standard footer. Bounded queues limit retained buffers and apply backpressure.

Single-job defaults: `T3SIM_STREAM_LEDGER=1`, `T3SIM_STREAM_TRACE=auto`, `T3SIM_LATENCY_PREFETCH=auto`. Trace streaming starts from a configured activity estimate of at least 262,144 rows; latency prefetch uses a configured message estimate. Choices depend on workload size and supported latency parameters, never scenario identity, seed or reference values. Concurrent batch markets use one worker per market and retain the same output bytes.

Diagnostics: `T3SIM_NOISE_PREFETCH=off/1/auto` (off by default), `T3SIM_STREAM_TRACE=0/1/auto`, `T3SIM_STREAM_LEDGER=0/1`, `T3SIM_LATENCY_PREFETCH=off/1/auto`, `T3SIM_LITE_PIPELINE=0/1`, `T3SIM_LITE_DICT=0/1`. Numeric dictionaries did not consistently beat delta integers and remain disabled. `--build-arg T3_BUILD_PGO=0` builds an unprofiled executable.

## Submission through GHCR

No push or Codabench upload was performed by this work. Publish the validated v5 image under a new public tag:

```powershell
docker tag agenthon-t3:v5 ghcr.io/aadit-n/agenthon-t3:v5
docker push ghcr.io/aadit-n/agenthon-t3:v5
```

Confirm the package is public and anonymously pullable. Use the new registry manifest digest from this push, then generate a fresh descriptor and ZIP. A local image ID is not a substitute for that digest; old ZIPs identify older releases.

```powershell
.\.venv-validation\Scripts\python.exe scripts\make_descriptor.py `
  --registry ghcr.io --repository aadit-n/agenthon-t3 `
  --digest sha256:YOUR_NEW_REGISTRY_MANIFEST_DIGEST `
  --team-id YOUR_DERIVED_TEAM_ID --phase dev --out submission-v5.json
.\.venv-validation\Scripts\qfbench2.exe submission pack `
  --descriptor submission-v5.json --team-number YOUR_TEAM_NUMBER --out submission-v5.zip
```

The toolkit prompts for the Team Key without echoing it. Use `--phase final` for Final. Keep the ZIP and Team Key private; no credentials belong in source or a descriptor.

## Main files

- `native/main_lite.cpp`: CLI, batches, bounded file pipelines and truthful timing.
- `native/parquet_lite.hpp`: direct encodings, Snappy, compact footer, deferred fill labels and SHA256.
- `native/simulation.hpp`, `native/random.hpp`, `native/core.hpp`: exact agents, oracle, RNG, scheduling and canonical rows.
- `native/pooled_book.hpp`: resting-order operations without per-order allocation.
- `scripts/build_release.sh`, `scripts/build_lite.sh`, `scripts/pgo_scenarios.py`: compilation and synthetic compiler training.
- `native/main.cpp`, `native/fast_parquet.hpp`: retained Arrow alternatives and experiments.
- `scripts/run_public.py`, `scripts/check_*.py`, `tests/`: independent correctness checks.
- `scripts/benchmark.py`, `scripts/performance.py`, `scripts/tune_v5.py`: timing, capacity and ablations.
- `SOURCE_PROVENANCE.json`, `THIRD_PARTY_NOTICES.md`: pins, licenses and provenance.

`native/core.hpp` was recovered from the tested v3 compiler snapshot and hash-verified; the older Cython header remains separate. This fixes the source/image mismatch discovered at the start of this work.
