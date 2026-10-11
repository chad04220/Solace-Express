"""Join the two delivery parts into the exact verified ZIP. No extraction."""
import argparse
import hashlib
from pathlib import Path

PARTS = (
    ("Solace-Express-Global-Elevation.zip.001", 337623069,
     "af0accd1b8e88f05e856c72267654ad8575dac24395062811a6a5ca790f3f79d"),
    ("Solace-Express-Global-Elevation.zip.002", 337623069,
     "c57cb9ba2aeef8e0b11367c1d4a8e7d238067fbcf7aee5257364df09c0c5e700"),
)
ARCHIVE_SHA256 = "e2c73659133353f43fbe55326b2f94bb781ca680ad1b00eb1bc781f46b7f13da"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parts-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("output already exists; choose a new file")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(args.output.name + ".assembling")
    complete = hashlib.sha256()
    opened = False
    try:
        with temporary.open("xb") as dst:
            opened = True
            for name, size, expected in PARTS:
                part = args.parts_dir / name
                if part.is_symlink() or not part.is_file() or part.stat().st_size != size:
                    raise ValueError("missing/incorrect part: " + name)
                digest = hashlib.sha256()
                with part.open("rb") as src:
                    for block in iter(lambda: src.read(8 * 1024 * 1024), b""):
                        dst.write(block)
                        digest.update(block)
                        complete.update(block)
                if digest.hexdigest() != expected:
                    raise ValueError("part SHA256 mismatch: " + name)
        if complete.hexdigest() != ARCHIVE_SHA256:
            raise ValueError("assembled ZIP SHA256 mismatch")
        if args.output.exists() or args.output.is_symlink():
            raise ValueError("output appeared during assembly")
        temporary.rename(args.output)
        print("Verified ZIP:", args.output, complete.hexdigest())
    except BaseException:
        if opened:
            temporary.unlink(missing_ok=True)
        raise


if __name__ == "__main__":
    main()
