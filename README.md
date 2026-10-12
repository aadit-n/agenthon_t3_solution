V7 keeps the 194k v5 engine's market behavior and Parquet bytes, changes its trace-row memory layout, and packages a statically linked C++17 executable in a small direct-entry runtime. The user reported that the previous output experiment reduced Codabench from approximately 194k to 173k and reverted it. V7 keeps v5's row groups, codecs, compression, streaming settings and both file hashes.

The audited runtime is 13.5 MiB versus 179.2 MiB for v5. Seven repeated Linux process trials across all 71 public units measured a 12.1% improvement in the mean of per-unit median rates. Eleven complete Docker CLI lifecycle trials on seven workloads measured a 4.4% gain in that subset's mean; local CLI/transport overhead is included. These are separate protocols, not a prediction of Codabench or proof of beating 231,024.2407.

The [v7 report](results/v7-optimization/REPORT.md) records exact image identity, validation, measurements and limitations. [HOW_IT_WORKS.md](HOW_IT_WORKS.md) explains the original architecture; [RESEARCH.md](RESEARCH.md) records alternatives. [The archived v5 guide](results/v7-tuning/README-v5.md) remains available.

## Build and run

Use Docker Desktop in Linux-container mode and PowerShell:

```powershell
cd C:\Users\DEVELOPMENT\Desktop\coding_projects\agenthon\agenthon_t3_solution
docker build --platform linux/amd64 -t agenthon-t3:v7 .
```

The engine uses generic amd64, GCC/C++17, exact FP ordering, LTO and synthetic PGO. No fast-math, runtime compilation, downloads or Python interpreter are required. A small glibc compatibility set remains for injected driver utilities; the simulator itself is statically linked. Application sources/build recipes are included under `/opt/relink`, alongside third-party notices. Compiler profiles, generated training inputs and reference outputs do not ship.

```powershell
New-Item -ItemType Directory -Force output | Out-Null
docker run --rm --network none --cpus 4 --memory 16g --memory-swap 16g `
  -v "${PWD}\..\track3-simulation-public\units\t3-as01-base-mix\scenario.json:/input/scenario.json:ro" `
  -v "${PWD}\output:/output" `
  agenthon-t3:v7 simulate --config /input/scenario.json --out /output/trace.parquet
```

For batches, mount a scenario directory and pass `simulate-batch --batch-dir /input --out-dir /output`. `--workers 1..4` is optional; the default is four, capped by the actual CPU quota. Each market remains isolated and produces the same Parquet bytes as its single-market run.

## Selected changes

- Static linking removes dynamic-loader relocation work and calls through shared-library interfaces.
- Trace rows shrink from 56 to 48 bytes: event kind and side have fixed small domains; timestamps, prices, sizes, order IDs and stable ranks remain full-width integers.
- One assembled runtime filesystem layer starts the executable directly.
- Simulation, RNG, scheduling, order books, output encodings and hashes retain the v5 settings.

Alternative price indexes, tombstone open orders, SIMD/buffered RNG, larger price caches, oracle coefficient caching, faster tied sorting and momentum rings were measured separately. They remain disabled in the release; sample-case gains did not consistently justify enabling them across the complete roster.

## Verification and benchmarks

The validator and diagnostic images are separate from the submission image. Existing `.venv-validation` tools use pinned `qfbench2-common==2.5.1`.

```powershell
.\.venv-validation\Scripts\python.exe scripts\run_public.py --image agenthon-t3:v7 `
  --isolated-scoring --output-dir results\v7-final-public
.\.venv-validation\Scripts\python.exe scripts\check_exact_suite.py --output-dir results\v7-final-public
.\.venv-validation\Scripts\python.exe scripts\check_variants.py --candidate agenthon-t3:v7 `
  --extended --reference-dir results\v4-variants --output-dir results\v7-final-variants `
  --candidate-env T3SIM_STREAM_TRACE=1 --candidate-env T3SIM_LATENCY_PREFETCH=1
.\.venv-validation\Scripts\python.exe scripts\check_cpp_random.py --image agenthon-t3:v7 `
  --output-dir results\v7-final-random
.\.venv-validation\Scripts\python.exe scripts\audit_static_runtime.py --image agenthon-t3:v7 `
  --out results\v7-optimization\runtime-audit.json
```

Run timing trials sequentially on an idle machine. `benchmark_lifecycle.py` uses host-side Docker commands, configuration-only inputs and persistent Linux storage. It reports complete CLI run/removal time and daemon start-to-die timestamps separately, binds immutable images and hashes every Parquet file outside timing.

```powershell
docker volume create --label agenthon.benchmark=v7 agenthon-v7-bench
.\.venv-validation\Scripts\python.exe scripts\benchmark_lifecycle.py `
  --image v5=agenthon-t3:v5 --image v7=agenthon-t3:v7 --runs 11 `
  --out results\v7-final-lifecycle\benchmark.json
```

Capacity diagnostics measure actual event/message/order counts, CPU, RSS, cgroup memory and batch worker equivalence. On a fresh machine, build the separate diagnostic image with `docker build --target lite-compiler -t agenthon-t3-diagnostics:local .`, then pass `--diagnostics-image agenthon-t3-diagnostics:local` below. Existing local diagnostics default to `agenthon-t3-build-lite:v4`.

```powershell
.\.venv-validation\Scripts\python.exe scripts\performance.py --image agenthon-t3:v7 `
  --runs 3 --warmups 1 --public-results results\v7-final-public `
  --output-dir results\v7-final-performance
```

The diagnostic shell/cat are mounted readonly and do not ship in the submission. Wrapper/mount overhead remains included in its daemon start-to-exit clock. These rates count simulation events, not HTTP requests. Current Development ranks harness-verified events over container creation-to-removal time; self-reported rates are checked but not ranked.

## Submit through GitHub Container Registry

No push or Codabench upload is performed by building/testing locally. Publish the validated image under a new tag:

```powershell
docker login ghcr.io -u aadit-n
docker tag agenthon-t3:v7 ghcr.io/aadit-n/agenthon-t3:v7
docker push ghcr.io/aadit-n/agenthon-t3:v7
```

Keep `agenthon-t3` public and anonymously pullable. Copy the new registry manifest digest from this push, not the local image ID. Derive your alias with `qfbench2 submission alias --team-number YOUR_TEAM_NUMBER`; the toolkit prompts for the Team Key without echoing it.

```powershell
.\.venv-validation\Scripts\python.exe scripts\make_descriptor.py `
  --registry ghcr.io --repository aadit-n/agenthon-t3 `
  --digest sha256:YOUR_NEW_REGISTRY_MANIFEST_DIGEST `
  --team-id YOUR_DERIVED_TEAM_ID --phase dev --out submission-v7.json
.\.venv-validation\Scripts\qfbench2.exe submission pack `
  --descriptor submission-v7.json --team-number YOUR_TEAM_NUMBER --out submission-v7.zip
```

Use `--phase final` only for Final. Upload the toolkit-generated ZIP, which includes the authenticated team claim; keep it and the Team Key private.
