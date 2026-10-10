"""Run all public units offline and check the organizer's published semantics.

References are read by this host-side validator only. They are never mounted in
the participant container or included in its build context.
"""
import argparse
from datetime import datetime
import gc
import hashlib
import importlib.metadata
import json
from pathlib import Path
import subprocess
import sys
import time
import tomllib
import uuid

import numpy as np
import pandas as pd
import pyarrow.parquet as pq


def digest(path):
    with path.open("rb") as fh:
        return hashlib.file_digest(fh, "sha256").hexdigest()


def launch(image, input_dir, output_dir, verb, args, environment=None):
    output_dir.mkdir(parents=True, exist_ok=True)
    name = "t3-validation-" + uuid.uuid4().hex
    command = ["docker", "create", "--name", name, "--network", "none",
               "--cpus", "4", "--memory", "16g", "--memory-swap", "16g",
               "-v", str(input_dir.resolve()) + (":/input/scenario.json:ro" if input_dir.is_file() else ":/input:ro"),
               "-v", str(output_dir.resolve()) + ":/output", image, verb, *args]
    if environment:
        command[2:2] = [part for key, value in environment.items() for part in ("-e", f"{key}={value}")]
    try:
        subprocess.run(command, capture_output=True, check=True, timeout=120)
        with (output_dir.parent / (output_dir.name + ".log")).open("w") as log:
            proc = subprocess.run(["docker", "start", "--attach", name],
                                  stdout=log, stderr=subprocess.STDOUT, timeout=300)
        if proc.returncode:
            raise RuntimeError(f"container exit {proc.returncode}: {output_dir.name}.log")
        state = json.loads(subprocess.check_output(["docker", "inspect", name]))[0]["State"]
        if state["ExitCode"]:
            raise RuntimeError(f"container exit {state['ExitCode']}")
        elapsed = (datetime.fromisoformat(state["FinishedAt"].replace("Z", "+00:00"))
                   - datetime.fromisoformat(state["StartedAt"].replace("Z", "+00:00"))).total_seconds()
    finally:
        subprocess.run(["docker", "rm", "-f", name], capture_output=True)
    return elapsed


def compare_run(candidate, reference, params, scenario, semantics):
    problems = []
    n_events = pq.ParquetFile(candidate / "trace.parquet").metadata.num_rows
    n_messages = pq.ParquetFile(candidate / "message_trace.parquet").metadata.num_rows
    exact_trace = pq.read_table(candidate / "trace.parquet").equals(
        pq.read_table(reference / "trace.parquet"), check_metadata=False)
    exact_ledger = pq.read_table(candidate / "message_trace.parquet").equals(
        pq.read_table(reference / "message_trace.parquet"), check_metadata=False)
    if not exact_trace:
        problems.append("Trace differs from reference (strict exact comparison)")
    if not exact_ledger:
        problems.append("Ledger differs from reference (strict exact comparison)")
    ev = json.loads((candidate / "events.json").read_text())
    for key, expected in (("n_events", n_events), ("scenario_id", scenario["scenario_id"]),
                          ("seed", scenario["seed"])):
        if ev.get(key) != expected:
            problems.append(f"Invalid {key}: {ev.get(key)!r} versus {expected!r}")
    if ev.get("trace_sha256") != digest(candidate / "trace.parquet"):
        problems.append("Wrong trace hash")
    if ev.get("message_trace_sha256") != digest(candidate / "message_trace.parquet"):
        problems.append("Wrong message ledger hash")
    elapsed, rate = ev["wall_clock_sec"], ev["events_per_sec"]
    if not (np.isfinite(elapsed) and elapsed > 0 and np.isfinite(rate)
            and abs(rate - n_events / elapsed) <= .05 * n_events / elapsed):
        problems.append("Inconsistent or nonfinite timing")
    return {"passed": not problems, "problems": problems, "events": n_events,
            "messages": n_messages, "exact_trace": exact_trace, "exact_ledger": exact_ledger,
            "reported_events_per_sec": rate}


def main():
    parser = argparse.ArgumentParser()
    project = Path(__file__).resolve().parent.parent
    parser.add_argument("--image", default="agenthon-t3:latest")
    parser.add_argument("--track-repo", type=Path, default=project.parent / "track3-simulation-public")
    parser.add_argument("--scoring-repo", type=Path, default=project.parent / "track3-simulation-public")
    parser.add_argument("--output-dir", type=Path, default=project / "results/public")
    parser.add_argument("--unit", action="append", help="limit to selected unit slugs")
    parser.add_argument("--reuse", action="store_true", help="check existing outputs without launching")
    parser.add_argument("--manifest-report", type=Path,
                        help="official Linux manifest/firewall audit (required on Windows)")
    args = parser.parse_args()
    sys.path.insert(0, str(args.scoring_repo.resolve()))
    from qfbench2_track_simulation import semantics
    from qfbench2_track_simulation.batch import check_aggregate
    from qfbench2_track_simulation.scoring import build_developer_verifier
    from qfbench2_common.manifest import assert_public_safe, verify_manifest
    version = importlib.metadata.version("qfbench2-common")
    if version != "2.5.1":
        raise RuntimeError(f"Validation requires pinned qfbench2-common 2.5.1; found {version}")
    report = {"image": args.image, "rankable": False, "toolkit_version": version, "units": []}
    audit = json.loads(args.manifest_report.read_text()) if args.manifest_report else None
    for unit in sorted((args.track_repo / "units").iterdir()):
        if not unit.is_dir() or (args.unit and unit.name not in args.unit):
            continue
        result = {"unit": unit.name}
        try:
            if audit is None and sys.platform == "win32":
                raise RuntimeError("Run scripts/audit_public.py under Linux, then pass --manifest-report")
            manifest = audit[unit.name]["manifest_errors"] if audit is not None else verify_manifest(unit)
            if manifest:
                raise RuntimeError(f"Invalid public reference manifest: {manifest}")
            firewall = audit[unit.name]["firewall_errors"] if audit is not None else assert_public_safe(unit)
            if firewall:
                raise RuntimeError(f"Public firewall violations: {firewall}")
            card = tomllib.loads((unit / "card.toml").read_text(encoding="utf8"))
            params = card["scoring"]["params"]
            out = args.output_dir / unit.name
            is_batch = (unit / "batch.json").is_file()
            if not args.reuse:
                verb, command = (("simulate-batch", ["--batch-dir", "/input/scenarios", "--out-dir", "/output"])
                                 if is_batch else
                                 ("simulate", ["--config", "/input/scenario.json", "--out", "/output/trace.parquet"]))
                result["container_elapsed_sec"] = launch(args.image, unit / "scenarios" if is_batch else unit / "scenario.json",
                                                          out, verb,
                                                          ["--batch-dir", "/input", "--out-dir", "/output"] if is_batch else command)
            if is_batch:
                subs = []
                for scenario_path in sorted((unit / "scenarios").glob("*.json")):
                    sub = scenario_path.stem
                    value = compare_run(out / sub, unit / "checks/reference_data" / sub, params,
                                        json.loads(scenario_path.read_text()), semantics)
                    value["sub"] = sub
                    subs.append(value)
                counts = {r["sub"]: r["events"] for r in subs}
                ok, issues = check_aggregate(out, json.loads((unit / "batch.json").read_text())["subs"])
                result.update(passed=all(r["passed"] for r in subs) and ok, subs=subs, problems=issues,
                              events=sum(counts.values()))
            else:
                result.update(compare_run(out, unit, params,
                                          json.loads((unit / "scenario.json").read_text()), semantics))
            context = {"unit_dir": str(unit), "output_dir": str(out)}
            verdict = build_developer_verifier(context).run(context)
            result["developer_verifier"] = {
                "admissible": verdict.admissible, "rankable": False,
                "gates": {k: v.passed for k, v in verdict.gate_results.items()},
                "labels": [str(label) for label in verdict.labels],
            }
            if not verdict.admissible:
                result["developer_verifier"]["detail"] = verdict.detail
            result["stylized_facts"] = verdict.detail.get("stylized_facts")
            result["passed"] = result["passed"] and verdict.admissible
        except Exception as exc:
            result.update(passed=False, error=f"{type(exc).__name__}: {exc}")
        report["units"].append(result)
        report["passed"] = sum(r["passed"] for r in report["units"])
        report["failed"] = len(report["units"]) - report["passed"]
        args.output_dir.mkdir(parents=True, exist_ok=True)
        (args.output_dir / "validation.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
        print(f"{'PASS' if result['passed'] else 'FAIL'} {unit.name} {result.get('error', result.get('problems', ''))}", flush=True)
        gc.collect()
        import pyarrow as pa
        pa.default_memory_pool().release_unused()
    print(f"{report['passed']} passed, {report['failed']} failed", flush=True)
    return bool(report["failed"])


if __name__ == "__main__":
    raise SystemExit(main())
