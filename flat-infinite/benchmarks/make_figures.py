import json
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

RESULTS_DIR = os.path.join(os.path.dirname(__file__), "results")
FIG_DIR = os.path.join(os.path.dirname(__file__), "figures")
os.makedirs(FIG_DIR, exist_ok=True)

plt.rcParams.update({
    "font.family": "serif",
    "font.size": 9,
    "axes.labelsize": 9,
    "axes.titlesize": 9,
    "legend.fontsize": 7.5,
    "xtick.labelsize": 8,
    "ytick.labelsize": 8,
    "axes.linewidth": 0.7,
    "lines.linewidth": 1.3,
    "lines.markersize": 4,
})

BLUE = "#1a5fb4"
ORANGE = "#e08000"
GREEN = "#2a8a3e"
GRAY = "#555555"
RED = "#b03030"


def load(name):
    with open(os.path.join(RESULTS_DIR, name)) as f:
        return json.load(f)


def fig_scaling():
    d1 = load("study1_resolution_scaling.json")
    d2 = load("study1_density_scaling.json")

    recs = d1["records"]
    by_res = {}
    for r in recs:
        by_res.setdefault(r["resolution"], []).append(r["elapsed_seconds"])
    resolutions = sorted(by_res.keys())
    cells = np.array([r * r for r in resolutions], dtype=float)
    means = np.array([np.mean(by_res[r]) for r in resolutions])
    stds = np.array([np.std(by_res[r], ddof=1) for r in resolutions])

    slope, intercept = np.polyfit(np.log(cells), np.log(means), 1)
    fit_x = np.linspace(cells.min(), cells.max(), 100)
    fit_y = np.exp(intercept) * fit_x ** slope

    ns = np.array([r["n_plates"] for r in d2["density_results"]], dtype=float)
    dmeans = np.array([r["total_time_seconds"]["mean"] for r in d2["density_results"]])
    dstds = np.array([r["total_time_seconds"]["std"] for r in d2["density_results"]])
    dslope, dintercept = np.polyfit(np.log(ns), np.log(dmeans), 1)

    fig, axes = plt.subplots(1, 2, figsize=(6.6, 2.55))

    ax = axes[0]
    ax.errorbar(cells, means, yerr=stds, fmt="o", color=BLUE, capsize=2.5, label="measured mean $\\pm$ 1 std (n=50)")
    ax.plot(fit_x, fit_y, "--", color=ORANGE, label=f"power-law fit, exponent={slope:.2f}")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("grid cells $N = \\mathrm{resolution}^2$")
    ax.set_ylabel("cache build time (s)")
    ax.set_title("(a) grid-resolution scaling")
    ax.legend(loc="upper left", frameon=False)

    ax = axes[1]
    ax.errorbar(ns, dmeans, yerr=dstds, fmt="o", color=GREEN, capsize=2.5, label="bootstrap mean $\\pm$ 1 std")
    fit_x2 = np.linspace(ns.min(), ns.max(), 100)
    ax.plot(fit_x2, np.exp(dintercept) * fit_x2 ** dslope, "--", color=ORANGE, label=f"power-law fit, exponent={dslope:.2f}")
    ax.set_xlabel("intersecting plates $N$")
    ax.set_ylabel("total cache build time (s)")
    ax.set_title("(b) plate-density scaling")
    ax.legend(loc="upper left", frameon=False)

    fig.tight_layout()
    fig.savefig(os.path.join(FIG_DIR, "scaling.pdf"))
    plt.close(fig)
    print("scaling.pdf written; resolution exponent", slope, "density exponent", dslope)


def fig_hydrology():
    d = load("study2_hydrology.json")
    counts = {int(k): v for k, v in d["order_counts"].items()}
    orders = sorted(counts.keys())
    ratios = d["pooled_bifurcation_ratios"]

    hack_points = d["hack_points"]
    A = np.array([p["land_area_plate_cells2"] for p in hack_points])
    L = np.array([p["max_path_length_plate_cells"] for p in hack_points])
    hl = d["hacks_law"]

    fig, axes = plt.subplots(1, 2, figsize=(6.6, 2.6))

    ax = axes[0]
    bars = ax.bar([str(o) for o in orders], [counts[o] for o in orders], color=BLUE, width=0.6)
    ax.set_yscale("log")
    ax.set_xlabel("Strahler order $\\omega$")
    ax.set_ylabel("stream count $N_\\omega$ (log scale)")
    ax.set_title("(a) bifurcation structure")
    for i, r in enumerate(ratios):
        ax.annotate(f"$R_b$={r['ratio']:.2f}", xy=(i + 0.5, max(counts[r['order_low']], counts[r['order_high']]) * 1.15),
                    ha="center", fontsize=7, color=RED)

    ax = axes[1]
    ax.scatter(A, L, s=10, color=BLUE, alpha=0.6, label=f"plates (n={len(A)})")
    if hl is not None:
        fit_x = np.linspace(A.min(), A.max(), 100)
        fit_y = hl["C_coefficient"] * fit_x ** hl["h_exponent"]
        ax.plot(fit_x, fit_y, "--", color=ORANGE, label=f"fit: $h$={hl['h_exponent']:.3f}, $R^2$={hl['r_squared']:.3f}")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("catchment (plate land) area $A$ (plate-cells$^2$)")
    ax.set_ylabel("longest channel length $L$ (plate-cells)")
    ax.set_title("(b) Hack's law")
    ax.legend(loc="upper left", frameon=False)

    fig.tight_layout()
    fig.savefig(os.path.join(FIG_DIR, "hydrology.pdf"))
    plt.close(fig)
    print("hydrology.pdf written")


def fig_boundary():
    d = load("study3_boundary_warp.json")
    dm = load("study3_border_margin.json")

    amp = [r for r in d["amplitude_sweep"] if r["factor"] >= 0]
    amp_x = np.array([r["value"] for r in amp])
    amp_y = np.array([r["fraction_summary"]["mean"] for r in amp])
    amp_std = np.array([r["fraction_summary"]["std"] for r in amp])

    freq = d["frequency_sweep"]
    freq_x = np.array([r["value"] for r in freq])
    freq_y = np.array([r["fraction_summary"]["mean"] for r in freq])
    freq_std = np.array([r["fraction_summary"]["std"] for r in freq])

    margins = dm["margins"]
    dens_mean = [r["drainage_density_summary"]["mean"] for r in dm["results"]]
    dens_std = [r["drainage_density_summary"]["std"] for r in dm["results"]]

    fig, axes = plt.subplots(1, 3, figsize=(6.9, 2.35))

    ax = axes[0]
    ax.errorbar(amp_x, amp_y, yerr=amp_std, fmt="o-", color=BLUE, capsize=2.5)
    ax.axvline(0.2, color=GRAY, linestyle=":", linewidth=0.9, label="paper default")
    ax.set_xlabel("PLT_STRETCHING (amplitude)")
    ax.set_ylabel("symmetric-diff. fraction")
    ax.set_title("(a) area error vs. amplitude")
    ax.legend(loc="upper left", frameon=False)

    ax = axes[1]
    ax.errorbar(freq_x, freq_y, yerr=freq_std, fmt="o-", color=GREEN, capsize=2.5)
    ax.axvline(0.0005, color=GRAY, linestyle=":", linewidth=0.9, label="paper default")
    ax.set_xlabel("PLT_BDR_SHAPE (frequency)")
    ax.set_ylabel("symmetric-diff. fraction")
    ax.set_title("(b) area error vs. frequency")
    ax.set_ylim(0, max(amp_y.max(), freq_y.max()) * 1.2)
    ax.legend(loc="upper left", frameon=False)

    ax = axes[2]
    ax.errorbar(margins, dens_mean, yerr=dens_std, fmt="o-", color=ORANGE, capsize=2.5)
    ax.axvline(100.0, color=GRAY, linestyle=":", linewidth=0.9, label="paper default")
    ax.set_xlabel("RIVR_BORDER_DIST")
    ax.set_ylabel("drainage density")
    ax.set_title("(c) margin vs. density")
    ax.legend(loc="lower left", frameon=False)

    fig.tight_layout(w_pad=1.4)
    fig.savefig(os.path.join(FIG_DIR, "boundary.pdf"))
    plt.close(fig)
    print("boundary.pdf written")


def fig_cache():
    d = load("study4_cache_eviction.json")
    scenario = d["scenarios"]["oscillating_fast"]

    caps = sorted(int(k) for k in scenario["lru"].keys())
    ttls = sorted(float(k) for k in scenario["ttl"].keys())

    fig, axes = plt.subplots(1, 2, figsize=(6.6, 2.55))

    ax = axes[0]
    peak = [scenario["lru"][str(c)]["peak_resident_bytes"] / 1024 for c in caps]
    hitrate = [scenario["lru"][str(c)]["hit_rate"] for c in caps]
    ax2 = ax.twinx()
    l1, = ax.plot(caps, peak, "o-", color=BLUE, label="peak resident memory")
    l2, = ax2.plot(caps, hitrate, "s--", color=ORANGE, label="hit rate")
    ax.set_xlabel("LRU capacity (resident plate caches)")
    ax.set_ylabel("peak resident memory (KiB)", color=BLUE)
    ax2.set_ylabel("hit rate", color=ORANGE)
    ax.set_title("(a) LRU capacity trade-off")
    ax.legend(handles=[l1, l2], loc="center right", frameon=False)

    ax = axes[1]
    peak_t = [scenario["ttl"][str(t)]["peak_resident_bytes"] / 1024 for t in ttls]
    hitrate_t = [scenario["ttl"][str(t)]["hit_rate"] for t in ttls]
    ax2 = ax.twinx()
    l1, = ax.plot(ttls, peak_t, "o-", color=GREEN, label="peak resident memory")
    l2, = ax2.plot(ttls, hitrate_t, "s--", color=ORANGE, label="hit rate")
    ax.set_xlabel("TTL (seconds since last access)")
    ax.set_ylabel("peak resident memory (KiB)", color=GREEN)
    ax2.set_ylabel("hit rate", color=ORANGE)
    ax.set_title("(b) TTL trade-off")
    ax.legend(handles=[l1, l2], loc="center right", frameon=False)

    fig.tight_layout()
    fig.savefig(os.path.join(FIG_DIR, "cache_eviction.pdf"))
    plt.close(fig)
    print("cache_eviction.pdf written")


if __name__ == "__main__":
    fig_scaling()
    fig_hydrology()
    fig_boundary()
    fig_cache()
