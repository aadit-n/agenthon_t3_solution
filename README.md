This folder contains an optimized, offline market simulator for Agenthon 2026 Track 3. It uses the organizer's exact market logic and random streams, compiles hot modules to C, and runs independent batch markets in separate CPU processes. It includes a Docker submission image, public validation tools, repeated benchmarks, and submission-descriptor tooling.

Read [TRACK_REQUIREMENTS.md](TRACK_REQUIREMENTS.md) for exactly what the competition expects and [RESEARCH.md](RESEARCH.md) for the existing solutions reviewed and the reasons behind the design choices.

## Verified results

The final image passed **71/71 public units** through the actual developer verifier. Both Parquet outputs match all **65 single scenarios and 30 batch sub-scenarios exactly**, covering **7,728,572 exchange events**. All four calibration divergences are zero on all 12 calibration units. The five randomized/edge tests also pass.

Evidence: [public validation](results/final-public/validation.json), [container timings](results/final-public/run-times.json), and [source provenance](SOURCE_PROVENANCE.json). The largest public scenario completed in approximately 45 seconds and used approximately 1.6 GiB peak process RSS, within the published 300-second and 16 GiB limits. These measurements come from this machine, with no GPU attached locally.

Three measured repeats after one discarded warmup produced these median container rates:

| Public workload | Original baseline events/s | Solution events/s | Speedup |
|---|---:|---:|---:|
| Mixed agents, 20-second horizon | 6,852 | 17,178 | 2.51× |
| 30 agents, 30-second horizon | 7,906 | 25,949 | 3.28× |
| Eight independent batch markets | 9,132 | 39,415 | 4.32× |

All repeated trace and ledger files were byte stable. [Benchmark details](results/benchmark/benchmark.json) include the exact image identities and every measured run. [Additional variants](results/variants/validation.json) also match the baseline exactly for an unseen seed with zero-latency ties, a shock combined with STP/response delays, and the fallback geographic latency model. The largest public scenario was tested for correctness and runtime limits; it was not part of this repeated speedup comparison.

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

Checks include exact exchange and ledger values, scenario/seed/count/hash consistency, all four developer-verifier gates, calibration facts, and batch isolation. The Linux scorer uses a bounded RAM/swap allocation to avoid exhausting a 16 GiB Windows host; simulator runs retain the competition's four-CPU/16 GiB caps, disabled network and 300-second Final ceiling.

Repeated local benchmarks:

```powershell
.\.venv-validation\Scripts\python.exe scripts\benchmark.py --runs 3
```

Each image has one discarded warmup, followed by alternating sequential measured runs on fixed scenarios. Elapsed time is the Docker daemon's start-to-exit window, including startup and output writing. Trace and ledger hashes must be stable across repeats. The output is `results/benchmark/benchmark.json`.

For a detailed capacity test with event/message/order-command rates, measured cgroup CPU/memory, elapsed-time ranges, and 1/2/4-worker batch scaling:

```powershell
.\.venv-validation\Scripts\python.exe scripts\performance.py --runs 3 --warmups 1
```

The report is [results/performance/REPORT.md](results/performance/REPORT.md); every raw run is in [performance.json](results/performance/performance.json). Use `--case mixed --runs 1 --warmups 0` for a quick smoke test. The six default cases cover mixed agents, a medium scenario, the largest public scenario, and the same eight-market batch at three worker counts. Counts come from actual Parquet rows. Exact reference values, repeat byte stability and identical batch outputs across worker counts are enforced.

The detailed test produced median full-container rates of **14,313 events/s** (mixed), **23,101 events/s** (medium), **22,692 events/s** (largest), and **38,510 events/s** (eight-market batch, four workers). The batch handled **15,196 order commands/s** and **34,408 messages/s**. Four workers were **2.15 times** as fast as one on that batch. Maximum measured cgroup memory was approximately **1.51 GiB**. The report includes CPU utilization, job times and ranges across all 18 measured runs; six warmups were excluded. The local CPU was an Intel Core i7-10750H.

The simulator is a CLI job processor. Events/s is the leaderboard metric; messages/s and order commands/s are useful diagnostic capacities, and scenario jobs/s depends on the configured simulation horizon and population. These numbers do not measure HTTP request capacity. The earlier complete public pass averaged 42,521 self-reported events/s and 26,849 full-container events/s. The verified Development leader on October 10, 2026 was fuzz at 231,024.2407, numerically 5.43 times that local self-reported mean. [Leaderboard](https://www.agenthon.net/)

These are local, non-rankable measurements. The sealed suite, organizer instance and Final evaluation plan are unavailable here, so local success does not establish a Final score or a globally fastest implementation. The current version has not demonstrated leaderboard-leading throughput.

## Submission

Registry publication and CodaBench upload have not been performed. After choosing your registry repository, tag/push the tested image and record its **registry manifest digest**. A local Docker image/config ID is not the registry digest.

Use the toolkit's `submission alias` command to derive your team ID, then generate the descriptor from its published fixture:

```powershell
.\.venv-validation\Scripts\python.exe scripts\make_descriptor.py `
  --registry docker.io --repository YOUR_ACCOUNT/agenthon-t3 `
  --digest sha256:YOUR_REGISTRY_MANIFEST_DIGEST `
  --team-id YOUR_DERIVED_TEAM_ID --phase dev --out submission.json
.\.venv-validation\Scripts\qfbench2.exe submission pack `
  --descriptor submission.json --team-number YOUR_TEAM_NUMBER --out submission.zip
```

`pack` prompts for the Team Key without echoing it. For Final use `--phase final` and follow the organizer's phase-specific instructions. The descriptor declares `category=simulator`, `models=[]`, `image_access=public`, and MIT for the new code. Upstream BSD/MIT notices are retained in the image. Never put a Team Key into source files or the descriptor.

## Files

- `t3sim/`: direct trace collection, compiled event dispatch, latency buffering, batch CLI.
- `vendor/`: pinned ABIDES with the four organizer overlays and unchanged reference adapter.
- `scripts/prepare_build.py`, `setup_native.py`: build-time transformation and compilation.
- `scripts/run_public.py`, `scripts/validate.py`: isolated simulation and actual developer-scoring checks.
- `scripts/benchmark.py`: same-machine repeat timing and deterministic-output checks.
- `scripts/performance.py`: measured CPU/memory, events/messages/order rates and batch scaling.
- `tests/test_hot.py`: RNG, copying, heap ties, FIFO depth and hidden-order edge checks.
- `SOURCE_PROVENANCE.json`, `THIRD_PARTY_NOTICES.md`: source pins, hashes and licenses.

The optional experimental depth cache is disabled by default because its ablation did not show a consistent gain. The submission image contains no public reference data, validation toolkit, runtime network fetches, or JIT compilation.
