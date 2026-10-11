This folder contains the optimized Agenthon 2026 Track 3 simulator. V3 runs the entire submission as a standalone C++17 executable: configuration, exact random streams, oracle, agents, exchange, event dispatch and Parquet output. It starts no Python interpreter and needs no runtime compilation or network access. Read [the v3 report](results/v3-optimization/REPORT.md) for measured speedups, validation, phase timings and remaining costs.

[TRACK_REQUIREMENTS.md](TRACK_REQUIREMENTS.md) explains the contract. [RESEARCH.md](RESEARCH.md) records the alternatives reviewed and the architectural decisions. The user reported approximately 33k for v1 and 97k for v2 on Codabench; the older images and evidence remain available.

## Validation and measurements

Release evidence is bound to immutable image IDs:

- [Actual public developer-verifier results](results/v3-public/validation.json).
- [Complete exact trace/ledger comparisons](results/v3-public/exact.json): 95 markets, 7,728,572 exchange rows and 8,183,538 message rows.
- [630,000 exact mixed random draws](results/v3-random/validation.json) against pinned Linux NumPy 1.26.4.
- [18 unseen seed/parameter scenarios](results/v3-variants/validation.json), covering ties, busy exchange rescheduling, half rounding, geographic latency, frequent shocks and fractional/heavy-tail latency.
- [Repeated v3/v2 comparisons](results/v3-vs-v2/benchmark.json), including startup, output writing and hashing.
- [Capacity report](results/v3-performance/REPORT.md): event/message/order rates, cgroup CPU/memory, timing ranges and 1/2/4-worker scaling.

These local measurements are non-rankable. A new Codabench evaluation is required to establish whether v3 exceeds the user-provided 231,024.2407 target. This CLI processes simulation jobs; its order/message rates are not HTTP request capacity.

## Build and run

From this directory, with Docker Desktop running:

```powershell
docker build --platform linux/amd64 -t agenthon-t3:v3 .
```

The default target builds the standalone executable with generic amd64 instructions and `-O3 -std=c++17 -ffp-contract=off`, without fast-math. The final image contains the executable, native Arrow/Parquet libraries, licenses and base tools. Python remains a base diagnostic tool; NumPy/pandas and Python simulation modules are absent from the submission runtime.

Example single scenario:

```powershell
$ProjectPath = (Get-Location).Path
New-Item -ItemType Directory -Force out | Out-Null
docker run --rm --network none --cpus 4 --memory 16g `
  -v "$ProjectPath/../track3-simulation-public/units/t3-as01-base-mix/scenario.json:/input/scenario.json:ro" `
  -v "$ProjectPath/out:/output" `
  agenthon-t3:v3 simulate --config /input/scenario.json --out /output/trace.parquet
```

Batch input is a directory of scenario JSON files:

```text
simulate-batch --batch-dir /input/scenarios --out-dir /output [--workers 1..4]
```

Up to four C++ workers process independent markets, bounded by the CPU quota. Each market owns its IDs and random streams and processes causal events sequentially. Per-market Parquet bytes are identical across standalone/batch invocation and worker counts.

The old implementations can still be built explicitly:

```powershell
docker build --platform linux/amd64 --target cython -t agenthon-t3-cython:local .
docker build --platform linux/amd64 --target baseline -t agenthon-t3-baseline:local .
```

The saved `agenthon-t3:v1` and `agenthon-t3:v2` images retain the prior releases. `T3SIM_ENGINE=object` applies to the Cython image, not the standalone executable.

## Reproduce the checks

Python 3.13 and official `qfbench2-common[data]` v2.5.1 are required for the separate validator. A prepared `.venv-validation` is available in this workspace.

```powershell
.\scripts\validate.ps1 -Image agenthon-t3:v3 -OutputDirectory results\v3-public
.\.venv-validation\Scripts\python.exe scripts\check_exact_suite.py --output-dir results\v3-public
.\.venv-validation\Scripts\python.exe scripts\check_cpp_random.py --image agenthon-t3:v3 --output-dir results\v3-random
.\.venv-validation\Scripts\python.exe scripts\check_variants.py --candidate agenthon-t3:v3 --extended --output-dir results\v3-variants
```

Participant containers receive configuration inputs only. Public references are mounted in separate scoring containers. The official scoring library is imported without copying or rewriting it. Fresh bounded Linux scorers avoid the Windows paging failure encountered during earlier development.

To resume scoring existing image-bound outputs:

```powershell
.\.venv-validation\Scripts\python.exe scripts\run_public.py --image agenthon-t3:v3 `
  --output-dir results\v3-public --score-only --isolated-scoring --resume-scoring
```

Repeated comparisons and capacity testing:

```powershell
.\.venv-validation\Scripts\python.exe scripts\benchmark.py `
  --candidate agenthon-t3:v3 --baseline agenthon-t3:v2 --runs 5 `
  --unit t3-as01-base-mix --unit t3-gb-base-30agent-30s `
  --unit t3-gb-mega-throughput --unit t3-gbatch-homog-8 --output-dir results\v3-vs-v2
.\.venv-validation\Scripts\python.exe scripts\performance.py --image agenthon-t3:v3 `
  --runs 3 --warmups 1 --public-results results\v3-public --output-dir results\v3-performance
.\.venv-validation\Scripts\python.exe scripts\summarize_v3.py
```

Run timing tests sequentially on an otherwise idle machine. Saved full-container rates include executable/library startup, Parquet encoding, hashing and shutdown. Sidecars separate configuration, simulation, sorting, assembly and writing. V2's loop counter included sorting and assembly, so its scope differs from v3's loop counter.

## Performance decisions

The engine recycles message slots and moves 32-byte heap entries; preserves the full timestamp/sender/recipient/message-ID comparator; keeps FIFO price-time matching; retains open-order bookkeeping where agent decisions use it; sorts equal-timestamp runs; uses dense execution markers; reserves bounded buffers from configured activity; assembles columns in one pass; releases row buffers before encoding; buffers disk writes and hashes bytes as they are written.

Large tables use dictionary codes from 262,144 rows; small tables use strings. This depends on row count, never scenario identity or reference data. Snappy and delta-packed numeric columns remain the defaults. Single jobs write two independent files concurrently; concurrent batch markets use one writer each. [Controlled output trials](results/cpp-output-tuning/large/ablation.json) and [batch trials](results/cpp-output-tuning/batch/ablation.json) retain every measurement.

Dictionary output explicitly preserves pandas nullable-integer metadata while keeping the public UTF8 column types. Regression checks roundtrip nanoseconds through pandas; raw Arrow value equality alone would miss float conversion by downstream readers.

Diagnostic options include `T3SIM_IO_THREADS=1/2`, `T3SIM_STRINGS=plain/dictionary/auto`, `T3SIM_COMPRESSION=snappy/none/lz4`, `T3SIM_PARQUET_ENCODING=legacy/delta`, `T3SIM_BUFFER_BYTES=0`, `T3SIM_STATISTICS=off`, `T3SIM_FULL_SORT=1`, `T3SIM_COLUMN_ASSEMBLY=legacy`, `T3SIM_TRACK_ALL_ORDERS=1` and `T3SIM_NO_RESERVE=1`. These are experiments, not required submission settings.

## Submission through GHCR

No push or Codabench upload was performed by this work. Publish the tested v3 image under a new tag:

```powershell
docker tag agenthon-t3:v3 ghcr.io/YOUR_GITHUB_USERNAME/agenthon-t3:v3
docker push ghcr.io/YOUR_GITHUB_USERNAME/agenthon-t3:v3
```

Make the GitHub package public and verify anonymous pullability. Copy the **new registry manifest digest** from the push; a local image ID is not a substitute. Regenerate the descriptor and ZIP because old ZIPs still identify earlier releases.

```powershell
.\.venv-validation\Scripts\python.exe scripts\make_descriptor.py `
  --registry ghcr.io --repository YOUR_GITHUB_USERNAME/agenthon-t3 `
  --digest sha256:YOUR_NEW_REGISTRY_MANIFEST_DIGEST `
  --team-id YOUR_DERIVED_TEAM_ID --phase dev --out submission-v3.json
.\.venv-validation\Scripts\qfbench2.exe submission pack `
  --descriptor submission-v3.json --team-number YOUR_TEAM_NUMBER --out submission-v3.zip
```

`pack` prompts for the Team Key without echoing it. For Final use `--phase final` and follow the organizer's phase instructions. Upstream notices are retained. Never put a Team Key into source files or a descriptor.

## Main files

- `native/main.cpp`: CLI, batch execution, Arrow/Parquet output, streaming hashes and timing.
- `native/simulation.hpp`: native configuration, sparse oracle, agents, exchange and dispatch.
- `native/random.hpp`: exact legacy MT19937 and distributions.
- `t3sim/native_types.hpp`: shared state, order book, queue and canonical trace processing.
- `Dockerfile`, `scripts/build_cpp.sh`: native build and runtime.
- `t3sim/*.py`, `t3sim/native.pyx`, `vendor/`: retained Cython/reference implementations.
- `scripts/run_public.py`, `scripts/validate.py`, `scripts/check_*.py`: separate correctness checks.
- `scripts/benchmark.py`, `scripts/performance.py`, `scripts/ablate_cpp_worker.py`: timing/capacity measurements.
- `SOURCE_PROVENANCE.json`, `THIRD_PARTY_NOTICES.md`: upstream pins, hashes and licenses.

The submission image contains no references, validation toolkit, runtime downloads or JIT compilation.
