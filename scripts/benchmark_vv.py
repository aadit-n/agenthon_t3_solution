import json
import pathlib
import subprocess
import time
import sys
import statistics

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

ROOT = pathlib.Path(__file__).resolve().parent.parent
TRACK_REPO = ROOT.parent / "track3-simulation-public"
OUTPUT_BASE = ROOT / "results" / "benchmark_vv"
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

def benchmark(images: dict, target_units=None):
    units = target_units or UNITS
    print(f"Comparing: {list(images.keys())} across {len(units)} units\n")
    results = {u: {} for u in units}
    
    for unit in units:
        print(f"--- Running {unit} ---")
        for tag_name, image_ref in images.items():
            out_d = OUTPUT_BASE / unit / tag_name
            res = run_one(image_ref, unit, out_d)
            results[unit][tag_name] = res
            if "error" in res:
                print(f"  {tag_name:6s}: ERROR - {res['error'][:100]}")
            else:
                print(f"  {tag_name:6s}: {res['events']:>8,} ev | reported {res['reported_eps']:>11,.0f} eps | container {res['docker_eps']:>10,.0f} eps | wall {res['reported_wall']:.4f}s | hash {res['trace_sha'][:8]}")
        print()

    # Compare first two if at least 2 tags
    tags = list(images.keys())
    if len(tags) >= 2:
        t0, t1 = tags[0], tags[1]
    
        print("\n" + "=" * 90)
        print(f"{'Unit':<30} | {t0 + ' (eps)':>14} | {t1 + ' (eps)':>14} | {t1 + '/' + t0:>12}")
        print("=" * 90)
    
        t0_list, t1_list = [], []
        for unit in units:
            r0 = results[unit][t0]
            r1 = results[unit][t1]
            if "error" not in r0 and "error" not in r1:
                e0 = r0["reported_eps"]
                e1 = r1["reported_eps"]
                t0_list.append(e0)
                t1_list.append(e1)
                ratio = e1 / e0 if e0 > 0 else 0
                print(f"{unit:<30} | {e0:>14,.0f} | {e1:>14,.0f} | {ratio:>11.2f}x")
            else:
                print(f"{unit:<30} | ERROR")

        print("-" * 90)
        if t0_list and t1_list:
            m0, m1 = statistics.mean(t0_list), statistics.mean(t1_list)
            h0, h1 = statistics.harmonic_mean(t0_list), statistics.harmonic_mean(t1_list)
            print(f"{'ARITHMETIC MEAN':<30} | {m0:>14,.0f} | {m1:>14,.0f} | {m1/m0:>11.2f}x")
            print(f"{'HARMONIC MEAN (CodaBench)':<30} | {h0:>14,.0f} | {h1:>14,.0f} | {h1/h0:>11.2f}x")
        print("=" * 90)
    return results

if __name__ == "__main__":
    tag = sys.argv[1] if len(sys.argv) > 1 else "agenthon-t3:vv9"
    benchmark({"vv8": "agenthon-t3:vv8", "vv9": tag})
