"""S01 postprocessing entry. Rendering follows the approved CSV stages."""
import argparse
from pathlib import Path
import sys

PROJECT_ROOT = Path(__file__).resolve().parent.parent


def setup_style():
    """Match atr_drive_sim fonts; importing this entry requires no plotting libs."""
    import matplotlib.font_manager as fm
    import matplotlib.pyplot as plt

    fonts = Path(__file__).resolve().parent / "fonts"
    for name in ("TimesNewRoman.ttf", "SimSun.ttf"):
        fm.fontManager.addfont(str(fonts / name))
    plt.rcParams["font.family"] = ["Times New Roman", "SimSun", "DejaVu Serif"]
    plt.rcParams["axes.unicode_minus"] = False
    plt.rcParams["font.size"] = 12


def main():
    parser = argparse.ArgumentParser(description="ATRcore system plots (S01 entry only)")
    parser.add_argument("--csv", type=Path, default=PROJECT_ROOT / "results/simulation.csv")
    parser.add_argument("--outdir", type=Path, default=PROJECT_ROOT / "results/figures")
    parser.add_argument("--dpi", type=int, default=300)
    args = parser.parse_args()
    if not args.csv.is_file():
        print(f"S01: CSV not found: {args.csv}; no model CSV is generated yet.", file=sys.stderr)
    else:
        print("S01: rendering awaits the approved CSV schema and integration stages.", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
