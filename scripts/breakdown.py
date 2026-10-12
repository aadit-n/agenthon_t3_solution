import json
from pathlib import Path

base = Path("results/benchmark_3way")
for u in sorted(base.iterdir()):
    if not u.is_dir(): continue
    for ver in ["vv7", "vv8"]:
        f = u / ver / "events.json"
        if not f.exists():
            f = u / ver / "batch_events.json"
        if f.exists():
            d = json.loads(f.read_text())
            print(f"{u.name:<30} {ver}: wall={d.get('wall_clock_sec',0):.4f}s, sim={d.get('simulation_loop_sec',0):.4f}s, out={d.get('output_write_sec',0):.4f}s, cfg={d.get('configuration_sec',0):.4f}s, sort={d.get('sorting_sec',0):.4f}s, tr_str={d.get('streamed_trace',False)}, ld_str={d.get('streamed_ledger',False)}")
    print()
