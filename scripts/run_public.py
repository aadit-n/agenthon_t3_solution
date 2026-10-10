"""Generate public outputs sequentially, then score in a separate Linux container.

This avoids Windows filesystem restrictions and limits validation memory. Only
the scoring container receives reference traces; the simulator receives configs.
"""
import argparse
import json
from pathlib import Path
import subprocess

from validate import launch


def main():
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--image", default="agenthon-t3:latest")
    parser.add_argument("--validator-image", default="agenthon-t3-validator:local")
    parser.add_argument("--track-repo", type=Path, default=root.parent / "track3-simulation-public")
    parser.add_argument("--fixture", type=Path, default=root / "results/reference-public")
    parser.add_argument("--output-dir", type=Path, default=root / "results/public")
    parser.add_argument("--unit", action="append")
    parser.add_argument("--score-only", action="store_true")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    runs = []
    if not args.score_only:
        for unit in sorted((args.fixture / "units").iterdir()):
            if not unit.is_dir() or (args.unit and unit.name not in args.unit):
                continue
            batch = (unit / "batch.json").is_file()
            elapsed = launch(args.image, unit / "scenarios" if batch else unit / "scenario.json",
                             output / unit.name, "simulate-batch" if batch else "simulate",
                             ["--batch-dir", "/input", "--out-dir", "/output"] if batch else
                             ["--config", "/input/scenario.json", "--out", "/output/trace.parquet"])
            runs.append({"unit": unit.name, "container_elapsed_sec": elapsed})
            (output / "run-times.json").write_text(json.dumps(runs, indent=2) + "\n")
            print(f"SIMULATED {unit.name} in {elapsed:.3f}s", flush=True)
    command = ["docker", "run", "--rm", "--network", "none", "--cpus", "4",
               "--memory", "2g", "--memory-swap", "4g",
               "-v", str(args.fixture.resolve()) + ":/references:ro",
               "-v", str(args.track_repo.resolve()) + ":/track:ro",
               "-v", str(root / "scripts") + ":/scripts:ro",
               "-v", str(output) + ":/reports", args.validator_image,
               "/scripts/validate.py", "--reuse", "--image", args.image,
               "--track-repo", "/references", "--scoring-repo", "/track", "--output-dir", "/reports"]
    for unit in args.unit or []:
        command.extend(["--unit", unit])
    return subprocess.run(command).returncode


if __name__ == "__main__":
    raise SystemExit(main())
