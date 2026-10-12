import json
import pathlib
import subprocess
import time
import sys

UNITS = [
    "t3-ca-baseline-calm",
    "t3-as01-base-mix",
    "t3-gb-base-30agent-30s",
    "t3-gb-highfreq-40hz-60s",
    "t3-gb-mega-throughput",
    "t3-mr-deep-book-state-size",
    "t3-mp03-mm-heavy-newest",
    "t3-ra01-fundamental-shock-mid",
    "t3-sf-01-baseline-regime",
    "t3-gbatch-dense-3",
    "t3-gbatch-homog-8",
    "t3-gbatch-hetero-mix",
]

IMAGES = {
    "aadit": "ghcr.io/aadit-n/agenthon-t3@sha256:1de238980b306fe71adcfb43d4cfeb01390eeebfaf1478b85dc375b84e6769f2",
    "vv7": "agenthon-t3:vv7",
    "vv8": "agenthon-t3:vv8",
}

ROOT = pathlib.Path(__file__).resolve().parent.parent
TRACK_REPO = ROOT.parent / "track3-simulation-public"
OUTPUT_BASE = ROOT / "results" / "benchmark_3way"
OUTPUT_BASE.mkdir(parents=True, exist_ok=True)

def run_one(image_tag: str, unit_name: str, out_dir: pathlib.Path) -> dict:
    unit_dir = TRACK_REPO / "units" / unit_name
    is_batch = (unit_dir / "scenarios").is_dir()
    
    out_dir.mkdir(parents=True, exist_ok=True)
    for f in out_dir.iterdir():
        if f.is_file(): f.unlink()
        
    u_mount = str(unit_dir.resolve()).replace("\\", "/")
    o_mount = str(out_dir.resolve()).replace("\\", "/")
    
    if is_batch:
        cmd = [
            "docker", "run", "--rm",
            "--network", "none",
            "-v", f"{u_mount}:/input:ro",
            "-v", f"{o_mount}:/output",
            image_tag,
            "simulate-batch",
            "--batch-dir", "/input/scenarios",
            "--out-dir", "/output"
        ]
        result_file = out_dir / "batch_events.json"
    else:
        cmd = [
            "docker", "run", "--rm",
            "--network", "none",
            "-v", f"{u_mount}:/input:ro",
            "-v", f"{o_mount}:/output",
            image_tag,
            "simulate",
            "--config", "/input/scenario.json",
            "--out", "/output/trace.parquet"
        ]
        result_file = out_dir / "events.json"

    t0 = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    t1 = time.perf_counter()
    
    wall_docker = t1 - t0
    if proc.returncode != 0:
        return {"error": proc.stderr or proc.stdout, "returncode": proc.returncode}
        
    if not result_file.exists():
        return {"error": f"Missing {result_file.name}", "returncode": 1}
        
    data = json.loads(result_file.read_text())
    events = data.get("total_events") or data.get("n_events", 0)
    reported_eps = data.get("events_per_sec", 0.0)
    reported_wall = data.get("wall_clock_sec", 0.0)
    docker_eps = events / wall_docker if wall_docker > 0 else 0
    trace_sha = data.get("trace_sha256") or (data.get("per_scenario", [{}])[0].get("trace_sha256", ""))
    
    return {
        "events": events,
        "reported_eps": reported_eps,
        "reported_wall": reported_wall,
        "docker_eps": docker_eps,
        "docker_wall": wall_docker,
        "trace_sha": trace_sha,
    }

def main():
    print(f"Starting 3-Way Benchmark across {len(UNITS)} units: aadit vs vv7 vs vv8\n")
    results = {u: {} for u in UNITS}
    
    for unit in UNITS:
        print(f"--- Running {unit} ---")
        for tag_name, image_ref in IMAGES.items():
            out_d = OUTPUT_BASE / unit / tag_name
            res = run_one(image_ref, unit, out_d)
            results[unit][tag_name] = res
            if "error" in res:
                print(f"  {tag_name:6s}: ERROR - {res['error'][:100]}")
            else:
                print(f"  {tag_name:6s}: {res['events']:>8,} ev | reported {res['reported_eps']:>11,.0f} eps | container {res['docker_eps']:>10,.0f} eps | wall {res['docker_wall']:.3f}s | hash {res['trace_sha'][:8]}")
        print()

    # Summary table
    print("\n" + "=" * 90)
    print(f"{'Unit':<30} | {'Aadit Reported':>14} | {'vv7 Reported':>14} | {'vv8 Reported':>14} | {'vv8 vs Aadit':>12}")
    print("=" * 90)
    
    aadit_rep_list = []
    vv7_rep_list = []
    vv8_rep_list = []
    
    aadit_dock_list = []
    vv7_dock_list = []
    vv8_dock_list = []
    
    for unit in UNITS:
        a = results[unit]["aadit"]
        v7 = results[unit]["vv7"]
        v8 = results[unit]["vv8"]
        
        if "error" not in a and "error" not in v7 and "error" not in v8:
            a_eps = a["reported_eps"]
            v7_eps = v7["reported_eps"]
            v8_eps = v8["reported_eps"]
            
            aadit_rep_list.append(a_eps)
            vv7_rep_list.append(v7_eps)
            vv8_rep_list.append(v8_eps)
            
            aadit_dock_list.append(a["docker_eps"])
            vv7_dock_list.append(v7["docker_eps"])
            vv8_dock_list.append(v8["docker_eps"])
            
            speedup = v8_eps / a_eps if a_eps > 0 else 0
            print(f"{unit:<30} | {a_eps:>14,.0f} | {v7_eps:>14,.0f} | {v8_eps:>14,.0f} | {speedup:>11.2f}x")
        else:
            print(f"{unit:<30} | ERROR in one of the runs")

    print("-" * 90)
    if aadit_rep_list:
        a_mean = sum(aadit_rep_list) / len(aadit_rep_list)
        v7_mean = sum(vv7_rep_list) / len(vv7_rep_list)
        v8_mean = sum(vv8_rep_list) / len(vv8_rep_list)
        
        a_hmean = len(aadit_rep_list) / sum(1.0/x for x in aadit_rep_list)
        v7_hmean = len(vv7_rep_list) / sum(1.0/x for x in vv7_rep_list)
        v8_hmean = len(vv8_rep_list) / sum(1.0/x for x in vv8_rep_list)
        
        print(f"{'ARITHMETIC MEAN':<30} | {a_mean:>14,.0f} | {v7_mean:>14,.0f} | {v8_mean:>14,.0f} | {v8_mean/a_mean:>11.2f}x")
        print(f"{'HARMONIC MEAN (CodaBench)':<30} | {a_hmean:>14,.0f} | {v7_hmean:>14,.0f} | {v8_hmean:>14,.0f} | {v8_hmean/a_hmean:>11.2f}x")
    print("=" * 90)
    
    (OUTPUT_BASE / "summary.json").write_text(json.dumps(results, indent=2))
    print(f"\nWrote full benchmark results to {OUTPUT_BASE / 'summary.json'}")

if __name__ == "__main__":
    main()
