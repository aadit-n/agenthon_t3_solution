"""Local CPU diagnosis; profiling overhead is never used as submission timing.

--source loads the prepared Python equivalents of compiled simulator modules so
cProfile can attribute their work. Native-library calls remain native.
"""
import argparse
import cProfile
import importlib.abc
import importlib.util
import io
import json
from pathlib import Path
import pstats
import sys


class SourceModules(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname.split('.')[0] not in {'abides_core', 'abides_markets', 'abides_fork', 't3sim'}:
            return None
        base = Path('/opt').joinpath(*fullname.split('.'))
        if base.with_suffix('.py').exists():
            return importlib.util.spec_from_file_location(fullname, base.with_suffix('.py'))
        if (base / '__init__.py').exists():
            return importlib.util.spec_from_file_location(fullname, base / '__init__.py')
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--config', required=True)
    ap.add_argument('--out-dir', required=True)
    ap.add_argument('--source', action='store_true')
    args = ap.parse_args()
    if args.source:
        sys.meta_path.insert(0, SourceModules())
    from t3sim.engine import simulate
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    profiler = cProfile.Profile()
    profiler.runcall(simulate, args.config, out / 'trace.parquet')
    print('Heavy modules imported:',[name for name in ('pandas','scipy','abides_core','abides_markets') if name in sys.modules])
    profiler.dump_stats(str(out / 'cpu.prof'))
    stream = io.StringIO()
    stats = pstats.Stats(profiler, stream=stream)
    for sort in ('cumulative', 'tottime'):
        stats.sort_stats(sort).print_stats(50)
    (out / 'cpu.txt').write_text(stream.getvalue())
    rows = [{'file': k[0], 'line': k[1], 'function': k[2], 'calls': v[1],
             'self_sec': v[2], 'cumulative_sec': v[3]}
            for k, v in stats.stats.items()]
    (out / 'cpu.json').write_text(json.dumps(sorted(rows, key=lambda r: -r['cumulative_sec']), indent=2))
    print(stream.getvalue())


if __name__ == '__main__':
    main()
