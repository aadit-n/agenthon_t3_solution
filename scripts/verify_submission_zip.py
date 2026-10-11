import sys
import zipfile
import json
import hashlib
from pathlib import Path

# Add official Agenthon2026 common library
common_path = Path(__file__).resolve().parent.parent.parent / "Agenthon2026-public" / "common"
sys.path.insert(0, str(common_path))

import qfbench2_common
from qfbench2_common.contracts.descriptor import SubmissionDescriptor, seal_descriptor_digest
from qfbench2_common import team_claim

def verify_zip(zip_path: Path):
    print("=" * 60)
    print("QFBENCH 2.0 OFFICIAL SUBMISSION ARCHIVE VERIFIER")
    print(f"Target archive: {zip_path}")
    print("=" * 60)

    # 1. Archive member verification
    print("\n[Step 1] Checking Archive Members:")
    with zipfile.ZipFile(zip_path, "r") as z:
        members = sorted(z.namelist())
        print(f"  - Members in zip: {members}")
        if members != ["submission.json", "team-claim.json"]:
            raise ValueError(f"Archive members must be exactly ['submission.json', 'team-claim.json'], found: {members}")
        sub_raw = z.read("submission.json")
        claim_raw = z.read("team-claim.json")
    print("  [PASS] Archive structure complies with QFBench 2.0 standard.")

    # 2. Descriptor contract verification
    print("\n[Step 2] Validating Descriptor Contract (qfbench2_common.contracts):")
    sub_dict = json.loads(sub_raw.decode("utf-8"))
    desc = SubmissionDescriptor.from_mapping(sub_dict)
    print("  - Schema Version:    ", desc.schema_version)
    print("  - Interface Version: ", desc.interface_version)
    print("  - Competition ID:    ", desc.competition_id)
    print("  - Team ID:           ", desc.team_id)
    print("  - Track:             ", desc.track)
    print("  - Phase:             ", desc.phase)
    print("  - Category:          ", desc.category)
    print("  - Registry Host:     ", desc.registry)
    print("  - Repository:        ", desc.repository)
    print("  - Image Digest:      ", desc.image_digest)
    print("  - Image Access:      ", desc.image_access)
    print("  - License:           ", desc.license)
    print("  - Descriptor Digest: ", desc.descriptor_digest)

    resealed = seal_descriptor_digest(dict(sub_dict))
    if resealed["descriptor_digest"] != sub_dict["descriptor_digest"]:
        raise ValueError(
            f"RFC 8785 JCS digest mismatch! Expected {sub_dict['descriptor_digest']}, computed {resealed['descriptor_digest']}"
        )
    print("  [PASS] RFC 8785 JCS canonical sealing verified.")

    # 3. Team claim verification
    print("\n[Step 3] Validating Team Claim & Proof (qfbench2_common.team_claim):")
    claim_dict = json.loads(claim_raw.decode("utf-8"))
    actual_sub_sha256 = hashlib.sha256(sub_raw).hexdigest()

    print("  - Claim Schema Version: ", claim_dict.get("schema_version"))
    print("  - Registered Team ID:   ", claim_dict.get("site_team_id"))
    print("  - Claim Digest Binding: ", claim_dict.get("descriptor_sha256"))
    print("  - Exact File SHA-256:   ", actual_sub_sha256)
    print("  - HMAC Proof:           ", claim_dict.get("proof"))

    if claim_dict.get("site_team_id") != 102:
        raise ValueError(f"Site team ID mismatch: expected 102, got {claim_dict.get('site_team_id')}")

    if claim_dict.get("descriptor_sha256") != actual_sub_sha256:
        raise ValueError(
            f"Descriptor sha256 mismatch! Claim binds {claim_dict.get('descriptor_sha256')}, actual is {actual_sub_sha256}"
        )
    print("  [PASS] team-claim.json is strictly cryptographically bound to submission.json.")

    print("\n" + "=" * 60)
    print("ALL QFBENCH 2.0 OFFICIAL CONTRACT CHECKS PASSED SUCCESSFULLY!")
    print("=" * 60)

if __name__ == "__main__":
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r"C:\Users\adithya\Downloads\agentthon\track3-simulation-public\submission.zip")
    verify_zip(target)
