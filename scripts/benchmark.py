"""Sequential, same-machine container benchmarks; local evidence, not Final timing."""
import argparse
import json
from pathlib import Path
import statistics
import sys

from validate import launch


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--candidate", default="agenthon-t3:latest")
    parser.add_argument("--baseline", default="agenthon-t3-baseline:local")
    parser.add_argument("--track-repo", type=Path, default=Path(__file__).resolve().parents[2] / "track3-simulation-public")
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).resolve().parents[1] / "results/benchmark")
    parser.add_argument("--unit", action="append")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--candidate-env", action="append", default=[], help="optional KEY=VALUE for ablation experiments")
    args = parser.parse_args()
    units = args.unit or ["t3-as01-base-mix", "t3-gb-base-30agent-30s", "t3-gbatch-homog-8"]
    environment = dict(value.split("=", 1) for value in args.candidate_env)
    report = {"rankable": False, "protocol": "Docker StartedAt-to-FinishedAt window; one warmup, then alternating sequential images; fixed scenario seeds",
              "runs": args.runs, "units": [], "images": {}}
    import subprocess
    for label, image in (("baseline", args.baseline), ("candidate", args.candidate)):
        report["images"][label] = json.loads(subprocess.check_output(["docker", "image", "inspect", image]))[0]["Id"]
    for slug in units:
        unit = args.track_repo / "units" / slug
        batch = (unit / "batch.json").is_file()
        record = {"unit": slug, "baseline": [], "candidate": []}
        stable = {}
        for repeat in range(args.runs + 1):
            labels = [("baseline", args.baseline), ("candidate", args.candidate)]
            if repeat % 2:
                labels.reverse()
            for label, image in labels:
                out = args.output_dir / slug / label / str(repeat)
                elapsed = launch(image, unit / "scenarios" if batch else unit / "scenario.json", out,
                                 "simulate-batch" if batch else "simulate",
                                 ["--batch-dir", "/input", "--out-dir", "/output"] if batch else
                                 ["--config", "/input/scenario.json", "--out", "/output/trace.parquet"],
                                 environment if label == "candidate" else None)
                data = json.loads((out / ("batch_events.json" if batch else "events.json")).read_text())
                n = data["total_events" if batch else "n_events"]
                import hashlib
                binding = tuple((str(p.relative_to(out)), hashlib.sha256(p.read_bytes()).hexdigest())
                                for p in sorted(out.rglob("*.parquet")))
                if label in stable and binding != stable[label]:
                    raise RuntimeError(f"Non-stable repeat output: {slug} {label}")
                stable[label] = binding
                if repeat > 0:
                    record[label].append({"elapsed_sec": elapsed, "events": n,
                                          "container_events_per_sec": n / elapsed,
                                          "reported_events_per_sec": data["events_per_sec"]})
                print(f"{slug} {label} repeat {repeat}: {n/elapsed:,.0f} events/s over {elapsed:.3f}s", flush=True)
        record["baseline_median"] = statistics.median(r["container_events_per_sec"] for r in record["baseline"])
        record["candidate_median"] = statistics.median(r["container_events_per_sec"] for r in record["candidate"])
        record["speedup"] = record["candidate_median"] / record["baseline_median"]
        report["units"].append(record)
        args.output_dir.mkdir(parents=True, exist_ok=True)
        (args.output_dir / "benchmark.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"{slug}: {record['speedup']:.2f}x container-throughput speedup", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
