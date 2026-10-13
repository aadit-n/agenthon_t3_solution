"""Check candidate batch isolation against validated v7 at 1/2/4 workers."""
import hashlib
import json
from pathlib import Path
import subprocess
from validate import launch


def hashes(folder):
    result = {}
    for path in sorted(folder.rglob('*.parquet')):
        digest = hashlib.sha256()
        with path.open('rb') as file:
            for block in iter(lambda: file.read(1048576), b''):
                digest.update(block)
        result[str(path.relative_to(folder))] = digest.hexdigest()
    return result


def main():
    root = Path(__file__).resolve().parents[1]
    image_id = json.loads(subprocess.check_output(['docker', 'image', 'inspect', 'agenthon-t3:v8-candidate']))[0]['Id']
    output = root / 'results/execution-specialization/workers'
    output.mkdir(parents=True, exist_ok=True)
    reference = root / 'results/v7-final-public'
    baseline_id = json.loads((reference / 'image.json').read_text())['image_id']
    if baseline_id != 'sha256:af1468c9911f5440efddf34741eeeaf8a90ccd792c1653dee0928c232fad3924':
        raise RuntimeError('Worker reference is not bound to v7')
    checks = []
    for unit in ('t3-gbatch-homog-8', 't3-gbatch-hetero-mix'):
        expected = hashes(reference / unit)
        if not expected:
            raise RuntimeError('Missing v7 output')
        for workers in (1, 2, 4):
            target = output / f'w{workers}' / unit
            launch(image_id, root / 'results/v4-configs' / unit / 'scenarios', target, 'simulate-batch',
                   ['--batch-dir', '/input', '--out-dir', '/output', '--workers', str(workers)])
            actual = hashes(target)
            checks.append({'unit': unit, 'workers': workers, 'same_parquet_bytes': actual == expected})
            (output / 'validation.json').write_text(json.dumps({'image_id': image_id, 'baseline_image_id': baseline_id,
                'all_exact': all(c['same_parquet_bytes'] for c in checks), 'checks': checks}, indent=2) + '\n')
            if actual != expected:
                raise RuntimeError(f'Worker output mismatch: {unit} workers={workers}')
            print(f'EXACT {unit} workers={workers}', flush=True)


if __name__ == '__main__':
    main()
