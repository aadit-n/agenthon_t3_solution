import json
import pathlib
import subprocess
import time
import sys
import statistics

UNITS = [
    "t3-ca-baseline-calm",
    "t3-as01-base-mix",
    "t3-as06-throughput-fast",
    "t3-s019-latency-jitter-kendall",
    "t3-gb-base-30agent-30s",
    "t3-gb-highfreq-40hz-60s",
    "t3-gb-mega-throughput",
    "t3-mr-deep-book-state-size",
    "t3-mp05-cancel-churn-newest",
    "t3-ra01-fundamental-shock-mid",
    "t3-sf-01-baseline-regime",
    "t3-gbatch-dense-3",
    "t3-gbatch-homog-8",
    "t3-gbatch-hetero-mix",
]

IMAGES = {
    "vv8": "agenthon-t3:vv8",
    "vv9": "agenthon-t3:vv9",
    "vv10": "agenthon-t3:vv10",
    "vv11": "agenthon-t3:vv11",
}

ROOT = pathlib.Path(__file__).resolve().parent.parent
TRACK_REPO = ROOT.parent / "track3-simulation-public"
OUTPUT_BASE = ROOT / "results" / "benchmark_5way"
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
            "--cpus", "4",
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
            "--cpus", "4",
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
    print("=" * 125)
    print(f"HEAD-TO-HEAD BENCHMARK WITH --cpus 4: vv8 vs vv9 vs vv10 vs vv11 across {len(UNITS)} units")
    print("=" * 125)
    results = {u: {} for u in UNITS}
    
    for idx, unit in enumerate(UNITS):
        print(f"\n[{idx+1}/{len(UNITS)}] Running {unit}...")
        for tag_name, image_ref in IMAGES.items():
            out_d = OUTPUT_BASE / unit / tag_name
            res = run_one(image_ref, unit, out_d)
            results[unit][tag_name] = res
            if "error" in res:
                print(f"  {tag_name:>5s}: ERROR - {res['error'][:80]}")
            else:
                print(f"  {tag_name:>5s}: {res['events']:>8,} ev | host {res['docker_eps']:>10,.0f} eps ({res['docker_wall']:.3f}s) | self {res['reported_eps']:>11,.0f} eps ({res['reported_wall']:.3f}s) | hash {res['trace_sha'][:8]}")

    print("\n" + "=" * 135)
    print("HOST-MEASURED EPS (--cpus 4 CONTAINER WALL CLOCK — EXACT CODABENCH METRIC)")
    print("=" * 135)
    print(f"{'Unit':<32} | {'Events':>8} | {'vv8 (Host)':>12} | {'vv9 (Host)':>12} | {'vv10 (Host)':>12} | {'vv11 (Host)':>13} | {'vv11/vv10':>9} | {'vv11/vv8':>9}")
    print("-" * 135)
    
    tags = ["vv8", "vv9", "vv10", "vv11"]
    host_lists = {t: [] for t in tags}
    
    for unit in UNITS:
        r = results[unit]
        ev = r["vv11"].get("events", 0) if "error" not in r["vv11"] else 0
        h = {t: r[t].get("docker_eps", 0) for t in tags}
        
        for t in tags:
            if "error" not in r[t]:
                host_lists[t].append(h[t])
                
        r_v10 = h["vv11"] / h["vv10"] if h["vv10"] > 0 else 1.0
        r_v8 = h["vv11"] / h["vv8"] if h["vv8"] > 0 else 1.0
        
        print(f"{unit:<32} | {ev:>8,} | {h['vv8']:>12,.0f} | {h['vv9']:>12,.0f} | {h['vv10']:>12,.0f} | {h['vv11']:>13,.0f} | {r_v10:>8.2f}x | {r_v8:>8.2f}x")

    print("-" * 135)
    print(f"{'ARITHMETIC MEAN HOST EPS':<32} | {'':>8} | {statistics.mean(host_lists['vv8']):>12,.0f} | {statistics.mean(host_lists['vv9']):>12,.0f} | {statistics.mean(host_lists['vv10']):>12,.0f} | {statistics.mean(host_lists['vv11']):>13,.0f} | {statistics.mean(host_lists['vv11'])/statistics.mean(host_lists['vv10']):>8.2f}x | {statistics.mean(host_lists['vv11'])/statistics.mean(host_lists['vv8']):>8.2f}x")
    print(f"{'HARMONIC MEAN HOST EPS':<32} | {'':>8} | {statistics.harmonic_mean(host_lists['vv8']):>12,.0f} | {statistics.harmonic_mean(host_lists['vv9']):>12,.0f} | {statistics.harmonic_mean(host_lists['vv10']):>12,.0f} | {statistics.harmonic_mean(host_lists['vv11']):>13,.0f} | {statistics.harmonic_mean(host_lists['vv11'])/statistics.harmonic_mean(host_lists['vv10']):>8.2f}x | {statistics.harmonic_mean(host_lists['vv11'])/statistics.harmonic_mean(host_lists['vv8']):>8.2f}x")
    print("=" * 135)

    bench_out = ROOT / "results" / "benchmark_5way_results.json"
    bench_out.write_text(json.dumps(results, indent=2))
    print(f"\nSaved raw benchmark data to: {bench_out}")

if __name__ == "__main__":
    main()
