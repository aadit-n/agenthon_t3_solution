"""Create a validation-only public unit copy with original LF text bytes.

Windows Git autocrlf changes text checksums. Normalization occurs in a separate
copy; the organizer checkout is never edited. Binary Parquet stays untouched.
"""
import argparse
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--track-repo", type=Path, default=Path(__file__).resolve().parents[2] / "track3-simulation-public")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[1] / "results/reference-public")
    args = parser.parse_args()
    for path in (args.track_repo / "units").rglob("*"):
        target = args.out / path.relative_to(args.track_repo)
        if path.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif path.is_file():
            if path.suffix.lower() in {".json", ".toml", ".md", ".txt"}:
                target.write_bytes(path.read_bytes().replace(b"\r\n", b"\n"))
            else:
                shutil.copyfile(path, target)
    print(args.out)


if __name__ == "__main__":
    main()
