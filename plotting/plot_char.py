"""S01 batch characteristic plotting entry; component renderers follow S05/S06."""
import argparse
from pathlib import Path
import sys

from plot import PROJECT_ROOT


def main():
    parser = argparse.ArgumentParser(description="ATRcore component plots (S01 entry only)")
    parser.add_argument("--csv", type=Path, action="append", required=True)
    parser.add_argument("--outdir", type=Path, default=PROJECT_ROOT / "results/figures/char")
    parser.add_argument("--dpi", type=int, default=300)
    parser.parse_args()
    print("S01: component CSV renderers await S05/S06; no images generated.", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
