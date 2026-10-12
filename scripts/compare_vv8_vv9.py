import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from benchmark_vv import run_one, UNITS
import statistics

print(f'Running 12-unit comparative benchmark between vv8 and vv9...')
vv8_res = {}
vv9_res = {}

for u in UNITS:
    out8 = Path(f'results/benchmark_vv/{u}/vv8_suite')
    r8 = run_one('agenthon-t3:vv8', u, out8)
    e8 = r8.get('reported_eps', 0)
    vv8_res[u] = e8
    
    out9 = Path(f'results/benchmark_vv/{u}/vv9_suite')
    r9 = run_one('agenthon-t3:vv9', u, out9)
    e9 = r9.get('reported_eps', 0)
    vv9_res[u] = e9
    
    speedup = (e9 / e8 - 1.0) * 100 if e8 > 0 else 0
    print(f'{u:<30}: vv8={e8:>10.0f} | vv9={e9:>10.0f} ({speedup:>+6.1f}%)')

v8_vals = [v for v in vv8_res.values() if v > 0]
v9_vals = [v for v in vv9_res.values() if v > 0]

print('=' * 70)
print(f'vv8 Arithmetic Mean: {statistics.mean(v8_vals):.0f} eps | Harmonic: {statistics.harmonic_mean(v8_vals):.0f} eps')
print(f'vv9 Arithmetic Mean: {statistics.mean(v9_vals):.0f} eps | Harmonic: {statistics.harmonic_mean(v9_vals):.0f} eps')
ari_speedup = (statistics.mean(v9_vals) / statistics.mean(v8_vals) - 1.0) * 100
har_speedup = (statistics.harmonic_mean(v9_vals) / statistics.harmonic_mean(v8_vals) - 1.0) * 100
print(f'Overall Speedup: Arithmetic {ari_speedup:+.1f}% | Harmonic {har_speedup:+.1f}%')
