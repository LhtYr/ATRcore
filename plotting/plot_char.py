"""GasGenerator characteristics from C-produced CSV; visualization only."""
import argparse
from pathlib import Path
import sys

from plot import PROJECT_ROOT, setup_style


def render_gas_generator(path, outdir, dpi):
    import matplotlib.pyplot as plt
    import numpy as np

    data = np.atleast_1d(np.genfromtxt(path, delimiter=",", names=True))
    required = {"p_chamber_Pa", "W_ox_requested_kg_s", "Wg_requested_kg_s",
                "T4_total_K", "nomConT", "flags", "status"}
    if not required.issubset(data.dtype.names or ()) or not data.size:
        raise ValueError(f"{path}: missing GasGenerator columns or rows")
    if np.any(data["status"] != 1):
        raise ValueError(f"{path}: failed evaluations cannot be plotted as valid characteristics")
    if any(not np.all(np.isfinite(data[key])) for key in required):
        raise ValueError(f"{path}: nonfinite values")
    pressures = np.unique(data["p_chamber_Pa"])
    if len(pressures) > 8:
        raise ValueError("At most eight pressure slices per figure")
    outdir.mkdir(parents=True, exist_ok=True)
    fig, axes = plt.subplots(1, len(pressures), figsize=(5 * len(pressures), 4.5), squeeze=False)
    vmin, vmax = data["T4_total_K"].min(), data["T4_total_K"].max()
    for ax, pressure in zip(axes[0], pressures):
        group = data[data["p_chamber_Pa"] == pressure]
        scatter = ax.scatter(group["Wg_requested_kg_s"], group["W_ox_requested_kg_s"],
                             c=group["T4_total_K"], vmin=vmin, vmax=vmax, cmap="viridis", s=28)
        ax.set(xlabel="燃料流量 (kg/s)", ylabel="LOX 流量 (kg/s)", title=f"总压 {pressure / 1e6:g} MPa")
        ax.grid(alpha=0.2, linestyle="--")
        fig.colorbar(scatter, ax=ax, label="出口总温 (K)")
    fig.suptitle("固定 ELM 温度计算值（数学扫描，非试验数据）")
    fig.tight_layout()
    output = outdir / f"{path.stem}_temperature.png"
    fig.savefig(output, dpi=dpi, bbox_inches="tight")
    plt.close(fig)
    print(f"saved {output}")

    fig, ax = plt.subplots(figsize=(7, 4.5))
    for pressure in pressures:
        group = data[data["p_chamber_Pa"] == pressure]
        oxygen = np.unique(group["W_ox_requested_kg_s"])
        middle = oxygen[len(oxygen) // 2]
        series = np.sort(group[group["W_ox_requested_kg_s"] == middle], order="Wg_requested_kg_s")
        ax.plot(series["Wg_requested_kg_s"], series["T4_total_K"], linewidth=2,
                label=f"{pressure / 1e6:g} MPa，LOX={middle:g} kg/s")
    ax.axhspan(1021.1, 1224.0, color="gray", alpha=0.12, label="反归一化尺度（不夹取输出）")
    ax.set(xlabel="燃料流量 (kg/s)", ylabel="出口总温 (K)", title="燃气发生器特性截面（固定 ELM）")
    ax.grid(alpha=0.2, linestyle="--")
    ax.legend(frameon=False, fontsize=9, loc="upper center", bbox_to_anchor=(0.5, -0.18), ncol=2)
    fig.tight_layout()
    output = outdir / f"{path.stem}_fuel_slices.png"
    fig.savefig(output, dpi=dpi, bbox_inches="tight")
    plt.close(fig)
    print(f"saved {output}")


def main():
    parser = argparse.ArgumentParser(description="ATRcore GasGenerator CSV 后处理（不替代 C 校验）")
    parser.add_argument("--csv", type=Path, action="append", required=True)
    parser.add_argument("--outdir", type=Path, default=PROJECT_ROOT / "results/figures/char")
    parser.add_argument("--dpi", type=int, default=300)
    args = parser.parse_args()
    try:
        import matplotlib
        matplotlib.use("Agg")
        setup_style()
        if args.dpi <= 0:
            raise ValueError("dpi must be positive")
        stems = [path.stem for path in args.csv]
        if len(stems) != len(set(stems)):
            raise ValueError("CSV stems must be distinct to avoid overwriting figures")
        for path in args.csv:
            render_gas_generator(path, args.outdir, args.dpi)
    except (ImportError, OSError, ValueError) as exc:
        print(f"plot failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
