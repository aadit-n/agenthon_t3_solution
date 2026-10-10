"""Run the official Linux no-follow manifest and public-firewall checks."""
import json
from pathlib import Path
import sys
from qfbench2_common.manifest import verify_manifest, assert_public_safe

units = Path(sys.argv[1]) / "units"
result = {}
for unit in sorted(units.iterdir()):
    if unit.is_dir():
        result[unit.name] = {"manifest_errors": verify_manifest(unit),
                             "firewall_errors": assert_public_safe(unit)}
output = Path(sys.argv[2])
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(result, indent=2) + "\n")
failed = sum(bool(v["manifest_errors"] or v["firewall_errors"]) for v in result.values())
print(f"Official manifest/firewall audit: {len(result) - failed} passed, {failed} failed")
raise SystemExit(bool(failed))
