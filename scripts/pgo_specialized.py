"""Extend synthetic compiler training with independently varied protocol modes."""
import json
from pathlib import Path
import runpy
import sys

runpy.run_path(str(Path(__file__).with_name('pgo_scenarios.py')), run_name='__main__')
output = Path(sys.argv[1])
for index in range(10):
    source = json.loads((output / f'{index:02}.json').read_text())
    source['scenario_id'] = f'compiler-plain-protocol-{index}'
    source['seed'] += 13457
    source['horizon_ns'] = 2_500_000_000
    source['exchange_config'].update(protocol_enforcement=index % 2 == 1,
                                     stp_policy=None, ack_delay_ns=0, compute_delay_ns=index % 4 * 125)
    (output / f'plain-{index:02}.json').write_text(json.dumps(source))
