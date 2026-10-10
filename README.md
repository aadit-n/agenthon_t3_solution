This folder contains the optimized Agenthon 2026 Track 3 simulator. V2 uses C++17 for market state, the event queue, matching, cancellation and trace buffers, with typed Cython dispatch and exact NumPy random streams. Python handles the CLI and configuration; Arrow writes the outputs. It runs offline and compiles everything at build time. Read [the v2 optimization report](results/v2-optimization/REPORT.md) for measured results and design details.

Read [TRACK_REQUIREMENTS.md](TRACK_REQUIREMENTS.md) for exactly what the competition expects and [RESEARCH.md](RESEARCH.md) for the existing solutions reviewed and the reasons behind the design choices.

## Verified results

The v2 image passed **71/71 public units** through the actual developer verifier. Both Parquet outputs match all **65 single scenarios and 30 batch sub-scenarios exactly**, covering **7,728,572 exchange events**. All four calibration divergences are zero on all 12 calibration units. Three native tests and five retained edge tests pass; native RNG testing compares 180,000 draw triplets against pinned NumPy, including cached Gaussian values and wide integer bounds.

Evidence: [v2 public validation](results/v2-public/validation.json), [container timings](results/v2-public/run-times.json), [image binding](results/v2-public/image.json), and [source provenance](SOURCE_PROVENANCE.json). A complete public single pass averaged **428,466 self-reported events/s**, versus **42,521** for v1 (**10.08x**). Its full-container arithmetic mean was **126,433 events/s**. These local numbers do not establish a Codabench score.

Three measured repeats after one discarded warmup, alternating v1/v2 sequentially on the same i7-10750H under four-CPU/16-GiB caps, produced these median container rates:

| Public workload | Submitted v1 events/s | Native v2 events/s | Speedup |
|---|---:|---:|---:|
| Mixed agents, 20-second horizon | 15,786 | 41,091 | 2.60x |
| 30 agents, 30-second horizon | 24,502 | 100,005 | 4.08x |
| Largest public market | 23,195 | 618,854 | 26.68x |
| Eight independent batch markets | 34,560 | 78,920 | 2.28x |

All repeated trace and ledger files were byte stable. [Benchmark details](results/v2-vs-v1/benchmark.json) include image identities and every measured run. [Six extra variants](results/v2-variants/validation.json) match ABIDES exactly for unseen seeds, zero-latency ties, shock/STP delays, geographic latency, exchange congestion, half-rounding and frequent random shocks.

The tested release is `agenthon-t3:v2` and `agenthon-t3:latest`, image ID `sha256:1de238980b306fe71adcfb43d4cfeb01390eeebfaf1478b85dc375b84e6769f2`. `agenthon-t3:v1` retains the submitted image. This local ID is distinct from the registry digest required for submission.

## Build and run

From this folder on Windows with Docker Desktop running:

```powershell
.\scripts\build.ps1
```

The equivalent platform-independent build commands are:

```bash
docker build --platform linux/amd64 -t agenthon-t3:latest .
docker build --platform linux/amd64 --target baseline -t agenthon-t3-baseline:local .
```

The baseline target preserves the organizer's simulation and output code, including serial batch execution. Both images precompile imports to make startup comparisons fair. The default target is the compiled submission. CUDA, training, and model APIs are unnecessary for this CPU implementation.

Example single run from PowerShell:

```powershell
New-Item -ItemType Directory -Force out | Out-Null
docker run --rm --network none --cpus 4 --memory 16g `
  -v "${PWD}\..\track3-simulation-public\units\t3-as01-base-mix\scenario.json:/input/scenario.json:ro" `
  -v "${PWD}\out:/output" `
  agenthon-t3:latest simulate --config /input/scenario.json --out /output/trace.parquet
```

For batch inputs, mount the directory containing the sub-scenario JSON files and use:

```bash
simulate-batch --batch-dir /input/scenarios --out-dir /output
```

The batch command uses up to four workers, bounded by the CPU quota. Each market resets its random seed and order/message IDs; workers share no mutable simulation state. `--workers 1` is available for diagnostics. Only the required traces and timing sidecars go into the output mount.

## Validate and benchmark

Python 3.13 is required for the pinned official toolkit, separately from Python 3.11 inside the simulator image. A prepared `.venv-validation` is available in this workspace. To reproduce setup and run every public unit:

```powershell
.\scripts\validate.ps1
```

The validation script creates a separate public fixture under `results/reference-public`. It normalizes Windows checkout CRLF text to the original LF bytes and verifies the resulting manifests using the organizer's Linux functions. It never changes the track checkout. Simulator containers mount scenario configs only; reference traces are supplied to the separate scoring container.

Checks include exact exchange and ledger values, scenario/seed/count/hash consistency, all four developer-verifier gates, calibration facts, and batch isolation. Scoring uses one fresh Linux process per unit with one GiB RAM and up to two GiB swap to avoid the local Windows paging failure. Simulator runs retain four-CPU/16-GiB caps, disabled network and the 300-second Final ceiling. Reports are bound to immutable image IDs; `run_public.py --score-only --isolated-scoring --resume-scoring` can retain already passing units from the same image.

Repeated local benchmarks:

```powershell
.\.venv-validation\Scripts\python.exe scripts\benchmark.py --candidate agenthon-t3:v2 --baseline agenthon-t3:v1 --runs 3 --output-dir results\v2-vs-v1
```

Each image has one discarded warmup, followed by alternating sequential measured runs on fixed scenarios. Elapsed time is the Docker daemon's start-to-exit window, including startup and output writing. Trace and ledger hashes must be stable across repeats. Add `--unit t3-gb-mega-throughput` to include the largest workload; the saved four-workload comparison is `results/v2-vs-v1/benchmark.json`.

For a detailed capacity test with event/message/order-command rates, measured cgroup CPU/memory, elapsed-time ranges, and 1/2/4-worker batch scaling:

```powershell
.\.venv-validation\Scripts\python.exe scripts\performance.py --image agenthon-t3:v2 --runs 3 --warmups 1 --public-results results\v2-public --output-dir results\v2-performance
```

The report is [results/v2-performance/REPORT.md](results/v2-performance/REPORT.md); every raw run is in [performance.json](results/v2-performance/performance.json). Use `--case mixed --runs 1 --warmups 0` for a quick smoke test. The six default cases cover mixed agents, a medium scenario, the largest public scenario, and the same eight-market batch at three worker counts. Counts come from actual Parquet rows. Exact reference values, repeat byte stability and identical batch outputs across worker counts are enforced.

The separate capacity run produced median full-container rates of **37,798 events/s** (mixed), **101,962 events/s** (medium), **626,046 events/s** (largest), and **69,530 events/s** (eight-market batch, four workers). The largest case handled **761,345 messages/s** and **120,684 order commands/s** in **1.866 seconds**. Maximum cgroup memory was **374.7 MiB**, compared with 1,548.8 MiB in the earlier v1 capacity run. The small batch's two/four-worker timing ranges overlap. The report includes CPU utilization and ranges across 18 measured runs, with six discarded warmups.

The simulator is a CLI job processor. Events/s is the leaderboard metric; messages/s and order commands/s are diagnostic capacities, and scenario jobs/s depends on horizon and population. These numbers do not measure HTTP request capacity. The v2 complete public single-pass self-reported mean is numerically 1.85 times the user-provided 231,024.2407 target. [Leaderboard](https://www.agenthon.net/)

These are local, non-rankable measurements. The sealed suite, organizer instance and Final evaluation plan are unavailable here. A new Codabench evaluation is needed to establish the v2 score; local success does not establish rank or a globally fastest implementation.

## Submission

Publication/upload of v2 has not been performed. Tag/push `agenthon-t3:v2` to `ghcr.io/YOUR_GITHUB_USERNAME/agenthon-t3:v2`, make the package public, verify anonymous pullability, and record its **new registry manifest digest**. Regenerate the descriptor and ZIP; existing ZIPs identify v1. A local image ID is not the registry digest.

Use the toolkit's `submission alias` command to derive your team ID, then generate the descriptor from its published fixture:

```powershell
.\.venv-validation\Scripts\python.exe scripts\make_descriptor.py `
  --registry ghcr.io --repository YOUR_GITHUB_USERNAME/agenthon-t3 `
  --digest sha256:YOUR_REGISTRY_MANIFEST_DIGEST `
  --team-id YOUR_DERIVED_TEAM_ID --phase dev --out submission.json
.\.venv-validation\Scripts\qfbench2.exe submission pack `
  --descriptor submission.json --team-number YOUR_TEAM_NUMBER --out submission-v2.zip
```

`pack` prompts for the Team Key without echoing it. For Final use `--phase final` and follow the organizer's phase-specific instructions. The descriptor declares `category=simulator`, `models=[]`, `image_access=public`, and MIT for the new code. Upstream BSD/MIT notices are retained in the image. Never put a Team Key into source files or the descriptor.

## Files

- `t3sim/native_types.hpp`, `t3sim/native.pyx`: C++ state/heap/matching and typed dispatch, native noise RNG, compact ledger and Arrow buffers.
- `t3sim/config.py`, `t3sim/schemas.py`: lean configuration and exact nullable integer metadata.
- `t3sim/engine.py`, `t3sim/cli.py`: output writing, earlier object engine, batch CLI.
- `vendor/`: pinned ABIDES with the four organizer overlays and unchanged reference adapter.
- `scripts/prepare_build.py`, `setup_native.py`: build-time transformation and compilation.
- `scripts/run_public.py`, `scripts/validate.py`: isolated simulation and actual developer-scoring checks.
- `scripts/benchmark.py`: same-machine repeat timing and deterministic-output checks.
- `scripts/performance.py`: measured CPU/memory, events/messages/order rates and batch scaling.
- `tests/test_hot.py`: RNG, copying, heap ties, FIFO depth and hidden-order edge checks.
- `tests/test_native.py`: exact interleaved RNG streams, oracle/config seed parity, nullable nanoseconds.
- `SOURCE_PROVENANCE.json`, `THIRD_PARTY_NOTICES.md`: source pins, hashes and licenses.

The earlier object engine remains available through `T3SIM_ENGINE=object`; its experimental price-level cache stays disabled. The native engine caches depth totals directly. `T3SIM_IO_THREADS=1` and `T3SIM_PARQUET_ENCODING=legacy` reproduce output ablations. The submission image contains no reference data, validation toolkit, runtime downloads, or JIT compilation.
