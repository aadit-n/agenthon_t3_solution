This solution includes the public ABIDES simulator and the organizer's Track 3 adapter, with their original licenses retained. The new acceleration code is MIT licensed. Reference scenarios and traces are validation material and are excluded from the submission image.

- ABIDES: `jpmorganchase/abides-jpmc-public`, commit `f9cbe51342b7dedd9587e4e069040d68a5c6477f`. Original notices: `vendor/abides/LICENSE` and `vendor/abides/LICENSE.txt` (BSD-3-Clause notice naming Georgia Tech Research Corporation).
- Track 3 adapter and four build overlays: `Agenthon-2026/track3-simulation-public`, commit `e9e42cc25fb840462f4cd97b23107b87619ddb85`. MIT license retained in `vendor/abides_fork/LICENSE`.
- NumPy, pandas, SciPy, PyArrow, coloredlogs and supporting runtime libraries: installed at pinned versions listed in `requirements.txt`; their distribution licenses remain in the container's Python installation.
- Cython and GCC are build tools. Native modules are generated ahead of time; the compiler stage is not the runtime base.
- `qfbench2-common` v2.5.1 is installed as a dependency in a separate validator. No scoring implementation is forked or reimplemented in the simulator.

`SOURCE_PROVENANCE.json` records the upstream pins, patch order, and hashes of the vendored Python source. Changes to the runtime copies are applied by `scripts/prepare_build.py`; the vendored originals remain available for inspection.
