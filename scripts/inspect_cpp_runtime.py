"""Read native linkage/provenance in the release without changing any files."""
import ctypes
import json
from pathlib import Path
import subprocess

directory=Path('/usr/local/lib/python3.11/site-packages/pyarrow')
crypto=ctypes.CDLL('libcrypto.so.3')
crypto.OpenSSL_version.restype=ctypes.c_char_p
print(json.dumps({'system_openssl':crypto.OpenSSL_version(0).decode(),
    'licenses':sorted(str(p) for p in Path('/opt/licenses').rglob('*') if p.is_file()),
    'linkage':subprocess.check_output(['ldd','/opt/t3sim-cpp'],text=True)}))
