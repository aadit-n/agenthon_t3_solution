"""Offline host-side audit of a native-only image; no runtime interpreter needed."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import uuid


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    image = json.loads(subprocess.check_output(['docker', 'image', 'inspect', args.image]))[0]
    assert image['Architecture'] == 'amd64' and image['Os'] == 'linux'
    assert image['Config']['Labels']['qfbench2.interface_version'] == '2.0'
    assert image['Config']['Entrypoint'] == ['/opt/t3sim-cpp']
    name = 't3-v7-audit-' + uuid.uuid4().hex
    archive = args.out.parent / 'runtime.tar'
    subprocess.run(['docker', 'create', '--name', name, '--network', 'none', image['Id'], '--help'], check=True, capture_output=True)
    try:
        subprocess.run(['docker', 'export', '--output', str(archive.resolve()), name], check=True)
    finally:
        subprocess.run(['docker', 'rm', name], check=True, capture_output=True)
    with tarfile.open(archive) as file:
        names = {member.name.removeprefix('./') for member in file.getmembers()}
        forbidden = ('opt/native', 'tmp/compiler-input', 'tmp/compiler-output', 'tmp/compiler-small', 'tmp/compiler-batch', 'tmp/compiler-profile')
        assert not any(name == path or name.startswith(path + '/') for name in names for path in forbidden)
        assert not any(name.endswith(('.gcda', '.gcno', '.parquet', '.pstats')) for name in names)
        assert not any('/site-packages/' in name or name.rsplit('/', 1)[-1] in ('g++', 'gcc', 'python', 'python3', 'python3.11') for name in names)
        binary = file.extractfile('opt/t3sim-cpp').read()
        assert binary[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', binary, 18)[0] == 62
        offset = struct.unpack_from('<Q', binary, 32)[0]
        entry_size, count = struct.unpack_from('<HH', binary, 54)
        segments = [struct.unpack_from('<I', binary, offset + i * entry_size)[0] for i in range(count)]
        assert 2 not in segments and 3 not in segments, 'Engine still has dynamic linkage'
        licenses = {name: hashlib.sha256(file.extractfile('opt/licenses/' + name).read()).hexdigest()
                    for name in ('SOURCE_PROVENANCE.json', 'THIRD_PARTY_NOTICES.md', 'LICENSE')}
        for path in ('etc/os-release', 'etc/ld.so.cache', 'sbin/ldconfig', 'lib64/ld-linux-x86-64.so.2'):
            assert path in names
        assert 'opt/relink/native/main_lite.cpp' in names
    result = {'image_label': args.image, 'image_id': image['Id'], 'image_bytes': image['Size'],
              'binary_sha256': hashlib.sha256(binary).hexdigest(), 'static_engine': True,
              'direct_entrypoint': True, 'python_and_compiler_absent': True,
              'generated_training_inputs_profiles_reference_outputs_absent': True,
              'license_sha256': licenses, 'runtime_paths': sorted(names)}
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k != 'runtime_paths'}, indent=2))


if __name__ == '__main__':
    main()
