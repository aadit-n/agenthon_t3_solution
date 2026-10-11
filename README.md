This folder contains the optimized Agenthon 2026 Track 3 simulator. V4 runs the complete engine and its Snappy Parquet writer in C++17, with bounded CPU pipelines for large jobs. It preserves the exact reference market behavior. The user reported Codabench scores of approximately 33k, 97k and 161k for v1, v2 and v3; v4 needs a new Codabench upload to establish its score.

[The v4 report](results/v4-optimization/REPORT.md) records final image identity, controlled comparisons, capacity and limitations. [TRACK_REQUIREMENTS.md](TRACK_REQUIREMENTS.md) explains the contract; [RESEARCH.md](RESEARCH.md) records reviewed implementations, primary sources and architectural decisions. Local rates are not predictions of a leaderboard rank.

## Build and run

From this directory, with Docker Desktop running:

```powershell
docker build --platform linux/amd64 -t agenthon-t3:v4 .
```

The default build uses generic amd64, `-O3 -std=c++17 -ffp-contract=off`, LTO and profile-guided optimization. Compiler profiles come from generated synthetic scenarios covering small jobs, streamed jobs and four-worker batches. Profiles and generated outputs stay in the compiler stage. No fast-math, runtime compilation or downloads are used.

The executable links Snappy 1.2.2 and system OpenSSL. Simulation needs no Arrow, NumPy, pandas or Python interpreter. Python remains a base diagnostic tool. The older implementations remain available as explicit Docker targets `arrow`, `cython` and `baseline`; the saved v1/v2/v3 images preserve earlier releases.

Single scenario:

```powershell
$ProjectPath = (Get-Location).Path
New-Item -ItemType Directory -Force out | Out-Null
docker run --rm --network none --cpus 4 --memory 16g `
  -v "$ProjectPath/../track3-simulation-public/units/t3-as01-base-mix/scenario.json:/input/scenario.json:ro" `
  -v "$ProjectPath/out:/output" `
  agenthon-t3:v4 simulate --config /input/scenario.json --out /output/trace.parquet
```

Batch input is a directory containing scenario JSON files:

```text
simulate-batch --batch-dir /input/scenarios --out-dir /output [--workers 1..4]
```

The four-CPU budget can run independent markets concurrently. A single market processes causal callbacks sequentially; spare cores prepare independent latency draws and encode finalized output blocks. All workers finish, files close and hashes finish before the elapsed time is reported. Market IDs and random state remain isolated.

## Validation

The prepared `.venv-validation` uses Python 3.13 and official `qfbench2-common[data]` v2.5.1. Participant validation containers receive configurations; separate scoring containers read public references and import the unmodified developer verifier.

Final evidence:

- [Official public verifier](results/v4-final-public/validation.json): 71 units.
- [Exact comparisons](results/v4-final-public/exact.json): 95 markets, 7,728,572 exchange rows and 8,183,538 message rows, including pandas nullable-integer roundtrips.
- [Unseen parameters/seeds](results/v4-final-variants/validation.json): 18 cases with trace streaming and latency prefetch forced on, including busy exchanges, ties, geographic latency, heavy tails, shocks and half rounding.
- [Exact distributions](results/v4-final-random/validation.json): 630,000 draws against Linux NumPy 1.26.4.
- [Encoding boundaries](results/v4-codec/validation.json): 18 cases and 721,782 rows covering full-width integers, overflow, UTF8, nulls, hashes and multiple row groups; address/undefined-behavior sanitizers used for the diagnostic generator.
- `tests/native_structures.cpp`: 300,000 randomized differential book actions and 1,000,000 tied heap operations checked with address/undefined-behavior sanitizers.

Reproduce the checks:

```powershell
.\.venv-validation\Scripts\python.exe scripts\run_public.py --image agenthon-t3:v4 `
  --isolated-scoring --output-dir results\v4-final-public
.\.venv-validation\Scripts\python.exe scripts\check_exact_suite.py --output-dir results\v4-final-public
.\.venv-validation\Scripts\python.exe scripts\check_variants.py --candidate agenthon-t3:v4 `
  --extended --output-dir results\v4-final-variants `
  --candidate-env T3SIM_STREAM_TRACE=1 --candidate-env T3SIM_LATENCY_PREFETCH=1
.\.venv-validation\Scripts\python.exe scripts\check_cpp_random.py --image agenthon-t3:v4 `
  --output-dir results\v4-final-random
```

Use fresh result directories for a changed image. `run_public.py --score-only --resume-scoring --isolated-scoring` can resume scoring generated outputs when the immutable image binding matches.

## Performance

Run timing tests sequentially on an idle machine. The report distinguishes Docker start-to-exit timing with Windows output mounts from separate executable trials on Linux container storage. Both include real simulation, encoding, file closure and hashing. Neither is an official competition measurement.

```powershell
.\.venv-validation\Scripts\python.exe scripts\benchmark.py `
  --candidate agenthon-t3:v4 --baseline agenthon-t3:v3 --runs 7 `
  --unit t3-as01-base-mix --unit t3-gb-base-30agent-30s `
  --unit t3-gb-mega-throughput --unit t3-gbatch-homog-8 `
  --unit t3-mr-deep-book-state-size --output-dir results\v4-final-vs-v3
.\.venv-validation\Scripts\python.exe scripts\performance.py --image agenthon-t3:v4 `
  --runs 3 --warmups 1 --public-results results\v4-final-public --output-dir results\v4-final-performance
```

Capacity measures exchange events, delivered messages and order commands. These are simulation jobs, not HTTP requests. The 1/2/4-worker test checks identical per-market Parquet bytes across worker counts.

## Why v4 is faster

The book uses recycled contiguous slots, intrusive FIFO links, a dense order-ID index and cached best levels. Ordered price maps support arbitrary int64 prices. A four-ary queue did not consistently improve the workload mix; the selected queue retains the exact binary-heap tie comparator.

The flat writer encodes known integer/categorical columns directly and preserves Snappy and pandas Int64 metadata. It avoids Arrow table construction, nested-schema conversion and repeated string lookup. An independent decoder checks its encodings.

Ledgers flush in 65,536-row blocks. Large traces flush completed timestamp groups after 16,384 rows, so no equal-time group or mutable quote is split. Numeric columns can be written during simulation; the type column is finalized after each order's last execution is known. Its late column offsets are recorded in the standard footer. Bounded queues limit retained buffers and apply backpressure.

Single-job defaults: `T3SIM_STREAM_LEDGER=1`, `T3SIM_STREAM_TRACE=auto`, `T3SIM_LATENCY_PREFETCH=auto`. Trace streaming starts from a configured activity estimate of at least 262,144 rows; latency prefetch uses a configured message estimate. Choices depend on workload size and supported latency parameters, never scenario identity, seed or reference values. Concurrent batch markets use one worker per market and retain the same output bytes.

Diagnostics: `T3SIM_STREAM_TRACE=0/1/auto`, `T3SIM_STREAM_LEDGER=0/1`, `T3SIM_LATENCY_PREFETCH=off/1/auto`, `T3SIM_LITE_PIPELINE=0/1`, `T3SIM_LITE_DICT=0/1`. Numeric dictionaries did not consistently beat delta integers and remain disabled. `--build-arg T3_BUILD_PGO=0` builds an unprofiled executable.

## Submission through GHCR

No push or Codabench upload was performed by this work. Publish the validated v4 image under a new public tag:

```powershell
docker tag agenthon-t3:v4 ghcr.io/YOUR_GITHUB_USERNAME/agenthon-t3:v4
docker push ghcr.io/YOUR_GITHUB_USERNAME/agenthon-t3:v4
```

Confirm the package is public and anonymously pullable. Use the new registry manifest digest from this push, then generate a fresh descriptor and ZIP. A local image ID is not a substitute for that digest; old ZIPs identify older releases.

```powershell
.\.venv-validation\Scripts\python.exe scripts\make_descriptor.py `
  --registry ghcr.io --repository YOUR_GITHUB_USERNAME/agenthon-t3 `
  --digest sha256:YOUR_NEW_REGISTRY_MANIFEST_DIGEST `
  --team-id YOUR_DERIVED_TEAM_ID --phase dev --out submission-v4.json
.\.venv-validation\Scripts\qfbench2.exe submission pack `
  --descriptor submission-v4.json --team-number YOUR_TEAM_NUMBER --out submission-v4.zip
```

The toolkit prompts for the Team Key without echoing it. Use `--phase final` for Final. Keep the ZIP and Team Key private; no credentials belong in source or a descriptor.

## Main files

- `native/main_lite.cpp`: CLI, batches, bounded file pipelines and truthful timing.
- `native/parquet_lite.hpp`: direct encodings, Snappy, compact footer, deferred fill labels and SHA256.
- `native/simulation.hpp`, `native/random.hpp`, `native/core.hpp`: exact agents, oracle, RNG, scheduling and canonical rows.
- `native/pooled_book.hpp`: resting-order operations without per-order allocation.
- `scripts/build_lite.sh`, `scripts/pgo_scenarios.py`: compilation and synthetic compiler training.
- `native/main.cpp`, `native/fast_parquet.hpp`: retained Arrow alternatives and experiments.
- `scripts/run_public.py`, `scripts/check_*.py`, `tests/`: independent correctness checks.
- `scripts/benchmark.py`, `scripts/performance.py`, `scripts/tune_v4.py`: timing, capacity and ablations.
- `SOURCE_PROVENANCE.json`, `THIRD_PARTY_NOTICES.md`: pins, licenses and provenance.

`native/core.hpp` was recovered from the tested v3 compiler snapshot and hash-verified; the older Cython header remains separate. This fixes the source/image mismatch discovered at the start of this work.
