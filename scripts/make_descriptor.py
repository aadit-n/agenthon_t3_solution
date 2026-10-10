"""Create a sealed descriptor from the organizer's published C5 fixture."""
import argparse
import json
from pathlib import Path
import re

import qfbench2_common
from qfbench2_common.contracts.descriptor import SubmissionDescriptor, seal_descriptor_digest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--registry", required=True)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--digest", required=True)
    parser.add_argument("--team-id", required=True, help="derived by qfbench2 submission alias/pack")
    parser.add_argument("--phase", choices=("dev", "final"), default="dev")
    parser.add_argument("--out", type=Path, default=Path("submission.json"))
    args = parser.parse_args()
    if not re.fullmatch(r"sha256:[0-9a-f]{64}", args.digest):
        parser.error("--digest must be a registry manifest digest, sha256: followed by 64 lowercase hex digits")
    fixture = Path(qfbench2_common.__file__).parent / "contracts/fixtures/c5" / f"simulation_{args.phase}.json"
    body = json.loads(fixture.read_text())
    body.update(team_id=args.team_id, license="MIT", models=[])
    body["image"] = {"registry": args.registry, "repository": args.repository, "digest": args.digest}
    body = seal_descriptor_digest(body)
    SubmissionDescriptor.from_mapping(body)
    args.out.write_text(json.dumps(body, indent=2) + "\n")
    print(args.out)


if __name__ == "__main__":
    main()
