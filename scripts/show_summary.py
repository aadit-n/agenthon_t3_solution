import json
from pathlib import Path

base = Path("results/benchmark_vv")
for u in sorted(base.iterdir()):
    if not u.is_dir(): continue
    for ver in ["vv8_suite", "vv9_suite"]:
        f = u / ver / "events.json"
        if not f.exists():
            f = u / ver / "batch_events.json"
        if f.exists():
            d = json.loads(f.read_text())
            w = d.get('wall_clock_sec', 0)
            s = d.get('simulation_loop_sec', 0)
            o = d.get('output_write_sec', 0)
            c = d.get('configuration_sec', 0)
            srt = d.get('sorting_sec', 0)
            eps = d.get('events_per_sec', 0)
            print(f"{u.name:<30} {ver:<10}: wall={w:.4f}s, sim={s:.4f}s, out={o:.4f}s, cfg={c:.4f}s, sort={srt:.4f}s, eps={eps:.0f}")
    print()
