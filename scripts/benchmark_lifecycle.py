"""Sequential host-side Docker CLI trials on persistent Linux output storage.

Report complete docker-run --rm CLI wall time and daemon start-to-die time.
The first includes local CLI/transport overhead; it is not an official score.
A bounded, unprivileged helper retains and hashes files outside both windows.
No container receives Docker credentials, the Docker socket, or public answers.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time
import uuid


def docker(*arguments):
    return subprocess.check_output(["docker", *map(str, arguments)], text=True, timeout=180)


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def inspect_files(out):
    events = json.loads((out / ("batch_events.json" if (out / "batch_events.json").exists() else "events.json")).read_text())
    files = {str(p.relative_to(out)): file_hash(p) for p in sorted(out.rglob("*.parquet"))}
    if not files:
        raise RuntimeError("No Parquet outputs")
    for event_path in out.rglob("events.json"):
        ev = json.loads(event_path.read_text())
        for filename, field in (("trace.parquet", "trace_sha256"), ("message_trace.parquet", "message_trace_sha256")):
            relative = str((event_path.parent / filename).relative_to(out))
            if ev[field] != files[relative]:
                raise RuntimeError(f"Incorrect participant hash: {event_path}: {field}")
        if not ev["wall_clock_sec"] > 0 or abs(ev["events_per_sec"] / (ev["n_events"] / ev["wall_clock_sec"]) - 1) > .05:
            raise RuntimeError(f"Incorrect participant timing: {event_path}")
    return {"events": events, "parquet_sha256": files}


def prepare_inputs():
    inputs = Path("/bench/input")
    inputs.mkdir(parents=True, exist_ok=True)
    for source in sorted(Path("/input").iterdir()):
        if not source.is_dir():
            continue
        target = inputs / source.name
        target.mkdir(exist_ok=True)
        if (source / "batch.json").exists():
            (target / "batch.json").write_text("{}\n")
            (target / "scenarios").mkdir(exist_ok=True)
            for config in (source / "scenarios").glob("*.json"):
                shutil.copyfile(config, target / "scenarios" / config.name)
        else:
            shutil.copyfile(source / "scenario.json", target / "scenario.json")


def trial(image, volume, slug, batch, label, environment):
    out = f"/bench/output/{slug}/{label}"
    command = (["simulate-batch", "--batch-dir", f"/bench/input/{slug}/scenarios", "--out-dir", out] if batch else
               ["simulate", "--config", f"/bench/input/{slug}/scenario.json", "--out", out + "/trace.parquet"])
    name = "t3-v7-trial-" + uuid.uuid4().hex
    arguments = ["run", "--rm", "--name", name, "--label", f"agenthon.trial={name}",
                 "--network", "none", "--cpus", "4", "--memory", "16g", "--memory-swap", "16g",
                 "-v", volume + ":/bench"]
    for key, value in environment.items():
        arguments.extend(["-e", f"{key}={value}"])
    wall_since = time.time() - 2
    since = time.perf_counter()
    try:
        stdout = docker(*arguments, image, *command)
        elapsed = time.perf_counter() - since
    except BaseException:
        subprocess.run(["docker", "rm", "-f", name], capture_output=True, timeout=30)
        raise
    # Queries, independent file hashing and copying are outside both clocks.
    event_lines = docker("events", "--since", str(wall_since), "--until", str(time.time()),
                         "--filter", f"label=agenthon.trial={name}", "--format", "{{json .}}")
    events = [json.loads(line) for line in event_lines.splitlines() if line.strip()]
    starts = [event["timeNano"] for event in events if event.get("Action") == "start"]
    exits = [event["timeNano"] for event in events if event.get("Action") == "die"]
    destroys = [event for event in events if event.get("Action") == "destroy"]
    if len(starts) != 1 or len(exits) != 1 or len(destroys) != 1:
        raise RuntimeError(f"Incomplete container lifecycle evidence: {events!r}")
    return {"cli_creation_to_removal_sec": elapsed, "daemon_start_to_die_sec": (exits[0] - starts[0]) / 1e9,
            "events": json.loads(stdout), "docker_events": events}


def main():
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[1]
    parser.add_argument("--image", action="append", required=True, help="NAME=IMAGE")
    parser.add_argument("--env", action="append", default=[], help="NAME:KEY=VALUE")
    parser.add_argument("--volume", default="agenthon-v7-bench")
    parser.add_argument("--helper-image", default="agenthon-t3-build-lite:v4")
    parser.add_argument("--inputs", type=Path, default=root / "results/v4-configs")
    parser.add_argument("--runs", type=int, default=9)
    parser.add_argument("--unit", action="append")
    parser.add_argument("--all-units", action="store_true")
    parser.add_argument("--out", type=Path, default=root / "results/v7-tuning/lifecycle/benchmark.json")
    args = parser.parse_args()
    if args.runs < 1:
        parser.error("--runs must be positive")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    images = dict(value.split("=", 1) for value in args.image)
    bindings = {name: json.loads(docker("image", "inspect", image))[0] for name, image in images.items()}
    environments = {name: {} for name in images}
    for item in args.env:
        name, setting = item.split(":", 1)
        key, value = setting.split("=", 1)
        environments[name][key] = value
    report = {"rankable": False, "runs": args.runs,
              "protocol": "Host Docker CLI run --rm wall clock (includes local CLI/transport); daemon start-to-die timestamps separately; rotated/reversed sequential containers; one excluded warmup; persistent Linux volume; artifact checks and retention after removal",
              "images": bindings, "environments": environments, "units": []}
    units = sorted(p.name for p in args.inputs.iterdir() if p.is_dir()) if args.all_units else args.unit or [
        "t3-as01-base-mix", "t3-as03-mm-liquidity-heavy", "t3-as04-value-heavy",
        "t3-gb-base-30agent-30s", "t3-gb-mega-throughput", "t3-gbatch-homog-8", "t3-mr-deep-book-state-size"]
    helper = "t3-v7-files-" + uuid.uuid4().hex
    docker("create", "--name", helper, "--network", "none", "--cpus", "1", "--memory", "128m", "--memory-swap", "128m",
           "--entrypoint", "python", "-v", args.volume + ":/bench", "-v", str(root / "scripts") + ":/scripts:ro",
           "-v", str(args.inputs.resolve()) + ":/input:ro", args.helper_image, "-c", "import time; time.sleep(3600)")
    try:
        docker("start", helper)
        docker("exec", helper, "python", "/scripts/benchmark_lifecycle.py", "--prepare-inputs")
        stable = {}
        for slug in units:
            trials = {name: [] for name in images}
            batch = (args.inputs / slug / "batch.json").exists()
            for repeat in range(args.runs + 1):
                names = list(images)
                names = names[repeat % len(names):] + names[:repeat % len(names)]
                if (repeat // len(names)) % 2:
                    names.reverse()
                for name in names:
                    measured = trial(bindings[name]["Id"], args.volume, slug, batch, name, environments[name])
                    audited = json.loads(docker("exec", helper, "python", "/scripts/benchmark_lifecycle.py",
                                                "--inspect-files", f"/bench/output/{slug}/{name}"))
                    if audited["events"] != measured["events"]:
                        raise RuntimeError("stdout and persisted events differ")
                    binding = (slug, name)
                    files = audited["parquet_sha256"]
                    if binding in stable and stable[binding] != files:
                        raise RuntimeError(f"Unstable repeat files: {slug} {name}")
                    stable[binding] = files
                    if repeat:
                        trials[name].append(measured)
            record = {"unit": slug, "modes": {}, "same_parquet_bytes": {
                name: stable[slug, name] == stable[slug, next(iter(images))] for name in images}}
            for name in images:
                count_key = "total_events" if batch else "n_events"
                count = trials[name][0]["events"][count_key]
                if any(t["events"][count_key] != count for t in trials[name]):
                    raise RuntimeError("Unstable event count")
                median = statistics.median(t["cli_creation_to_removal_sec"] for t in trials[name])
                record["modes"][name] = {"median_cli_creation_to_removal_sec": median,
                    "median_daemon_start_to_die_sec": statistics.median(t["daemon_start_to_die_sec"] for t in trials[name]),
                    "events_per_sec": count / median, "trials": trials[name], "parquet_sha256": stable[slug, name]}
                print(f"{slug} {name}: {count / median:,.0f}/s; CLI lifecycle {median:.6f}s", flush=True)
                retained = args.out.parent / slug / name
                retained.mkdir(parents=True, exist_ok=True)
                docker("cp", helper + f":/bench/output/{slug}/{name}/.", str(retained.resolve()))
            report["units"].append(record)
            args.out.write_text(json.dumps(report, indent=2) + "\n")
        report["mean_per_unit_median_rates"] = {name: statistics.mean(
            row["modes"][name]["events_per_sec"] for row in report["units"]) for name in images}
        args.out.write_text(json.dumps(report, indent=2) + "\n")
    finally:
        docker("rm", "-f", helper)


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--inspect-files":
        print(json.dumps(inspect_files(Path(sys.argv[2]))))
    elif sys.argv[1:] == ["--prepare-inputs"]:
        prepare_inputs()
    else:
        main()
