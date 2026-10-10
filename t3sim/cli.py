"""Published simulate/simulate-batch entrypoints, with process-isolated markets."""
import argparse
from concurrent.futures import ProcessPoolExecutor
import json
import multiprocessing
import os
from pathlib import Path
import time


def run_single(config, out, seed=None):
    if os.environ.get("T3SIM_BASELINE") == "1":
        from abides_fork.simulate import simulate
    else:
        from t3sim.engine import simulate
    return simulate(config, out, seed)


def cpu_budget():
    budget = min(4, os.cpu_count() or 1)
    try:
        quota, period = Path("/sys/fs/cgroup/cpu.max").read_text().split()
        if quota != "max":
            budget = min(budget, max(1, int(quota) // int(period)))
    except (OSError, ValueError):
        pass
    return budget


def run_batch(batch_dir, out_dir, workers=None):
    if os.environ.get("T3SIM_BASELINE") == "1":
        from abides_fork.simulate_batch import simulate_batch
        return simulate_batch(batch_dir, out_dir)
    started = time.perf_counter()
    subs = sorted(Path(batch_dir).glob("*.json"))
    if not subs:
        raise ValueError(f"No scenario JSON files in {batch_dir}")
    out_dir = Path(out_dir)
    workers = min(len(subs), cpu_budget(), workers or 4)
    results = []
    if workers == 1:
        results = [run_single(p, out_dir / p.stem / "trace.parquet") for p in subs]
    else:
        # fork shares read-only imported code; each process has private global
        # counters, agents and RNG. Prime imports before fork to avoid repeated IO.
        from t3sim import engine  # noqa: F401
        context = multiprocessing.get_context("fork")
        with ProcessPoolExecutor(max_workers=workers, mp_context=context) as pool:
            futures = [pool.submit(run_single, p, out_dir / p.stem / "trace.parquet")
                       for p in subs]
            results = [future.result() for future in futures]
    elapsed = time.perf_counter() - started
    total = sum(r["n_events"] for r in results)
    aggregate = {
        "n_scenarios": len(subs), "total_events": total,
        "wall_clock_sec": elapsed, "events_per_sec": total / elapsed,
        "gpu_seconds": 0.0,
        "peak_memory_bytes": sum(sorted((r["peak_memory_bytes"] for r in results),
                                          reverse=True)[:workers]),
        "per_scenario": [{"sub": p.stem, "n_events": r["n_events"],
                          "trace_sha256": r["trace_sha256"]}
                         for p, r in zip(subs, results)],
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "batch_events.json").write_text(json.dumps(aggregate, indent=2) + "\n")
    return aggregate


def main(argv=None):
    parser = argparse.ArgumentParser(description="Agenthon Track 3 exact market simulator")
    commands = parser.add_subparsers(dest="verb", required=True)
    single = commands.add_parser("simulate")
    single.add_argument("--config", required=True)
    single.add_argument("--out", required=True)
    single.add_argument("--seed", type=int)
    batch = commands.add_parser("simulate-batch")
    batch.add_argument("--batch-dir", required=True)
    batch.add_argument("--out-dir", required=True)
    batch.add_argument("--workers", type=int, choices=range(1, 5))
    args = parser.parse_args(argv)
    if args.verb == "simulate":
        result = run_single(args.config, args.out, args.seed)
    else:
        result = run_batch(args.batch_dir, args.out_dir, args.workers)
    print(json.dumps(result), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
