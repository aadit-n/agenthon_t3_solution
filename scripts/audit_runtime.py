"""Inspect only submission-runtime files and dependency presence, offline."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

excluded = ['/opt/native', '/tmp/compiler-input', '/tmp/compiler-output', '/tmp/compiler-small',
            '/tmp/compiler-batch', '/tmp/compiler-profile']
excluded += ['/usr/local/lib/python3.11/site-packages/' + name for name in ('numpy', 'pandas', 'pyarrow')]
assert not any(Path(path).exists() for path in excluded)
assert shutil.which('g++') is None
result = {'binary_sha256': hashlib.sha256(Path('/opt/t3sim-cpp').read_bytes()).hexdigest(),
          'excluded_paths_absent': excluded,
          'compiler_absent': True,
          'license_sha256': {name: hashlib.sha256((Path('/opt/licenses') / name).read_bytes()).hexdigest()
                             for name in ('SOURCE_PROVENANCE.json', 'THIRD_PARTY_NOTICES.md')},
          'native_libraries': subprocess.check_output(['ldd', '/opt/t3sim-cpp'], text=True).splitlines()}
print(json.dumps(result, indent=2))
