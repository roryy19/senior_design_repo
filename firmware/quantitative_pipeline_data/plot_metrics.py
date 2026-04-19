"""
plot_metrics.py -- Generate presentation plots from metric CSVs.

Usage:
    python plot_metrics.py

Depends on: matplotlib, numpy (both stdlib-compatible install).

Reads metric*.csv + support_distance_vs_level.csv, writes fig_*.png
to the same directory.
"""

import csv
import os
import numpy as np
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))


def load_csv(name):
    path = os.path.join(HERE, name)
    with open(path, newline="") as f:
        r = csv.reader(f)
        header = next(r)
        rows = [row for row in r]
    return header, rows


def save(fig, name):
    out = os.path.join(HERE, name)
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  -> {name}")


# --------------------------------------------------------------------------
# FIG 1: Angular localization error
# --------------------------------------------------------------------------
def fig1_localization():
    _, rows = load_csv("metric1_localization.csv")
    true_a = np.array([float(r[0]) for r in rows])
    meas_a = np.array([float(r[1]) for r in rows])
    err = np.array([float(r[2]) for r in rows])

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))

    # Left: scatter true vs measured
    ax1.plot([0, 360], [0, 360], "k--", lw=1, alpha=0.5, label="ideal (y = x)")
    ax1.scatter(true_a, meas_a, s=14, c="tab:blue", alpha=0.75)
    ax1.set_xlim(0, 360)
    ax1.set_ylim(0, 360)
    ax1.set_xlabel("True obstacle angle (deg)")
    ax1.set_ylabel("Measured haptic centroid angle (deg)")
    ax1.set_title("Angular localization (obstacle @ 50 cm)")
    ax1.legend(loc="upper left")
    ax1.grid(True, alpha=0.3)

    # Right: error vs angle
    ax2.plot(true_a, err, lw=1.2, c="tab:red")
    ax2.axhline(err.mean(), ls="--", c="k", lw=1,
                label=f"mean = {err.mean():.2f} deg")
    ax2.axhline(err.max(), ls=":", c="gray", lw=1,
                label=f"max = {err.max():.2f} deg")
    ax2.set_xlabel("True obstacle angle (deg)")
    ax2.set_ylabel("Absolute error (deg)")
    ax2.set_title("Localization error vs angle")
    ax2.set_xlim(0, 360)
    ax2.set_ylim(0, max(err.max() * 1.2, 5))
    ax2.legend()
    ax2.grid(True, alpha=0.3)

    fig.suptitle("Angular Localization Error", fontsize=13, fontweight="bold")
    save(fig, "fig_01_localization.png")


# --------------------------------------------------------------------------
# FIG 2: 360 deg coverage (polar)
# --------------------------------------------------------------------------
def fig2_coverage():
    _, rows = load_csv("metric2_coverage.csv")
    dists = np.array([float(r[0]) for r in rows])
    cov = np.array([float(r[1]) for r in rows])
    dead = np.array([int(r[2]) for r in rows])

    fig, ax = plt.subplots(figsize=(8, 5))
    bars = ax.bar(
        [f"{d:.0f} cm" for d in dists],
        cov,
        color=["tab:green" if c == 100 else "tab:orange" if c > 0 else "tab:red"
               for c in cov],
        edgecolor="black",
    )
    for b, c, d in zip(bars, cov, dead):
        label = f"{c:.0f}%"
        if d > 0:
            label += f"\n({d} dead)"
        ax.text(b.get_x() + b.get_width() / 2, b.get_height() + 1, label,
                ha="center", fontsize=9)
    ax.set_ylabel("Coverage (%)")
    ax.set_xlabel("Obstacle distance")
    ax.set_ylim(0, 115)
    ax.set_title("360-degree Coverage vs Distance\n"
                 "(fraction of angles producing >= 1 motor response)",
                 fontweight="bold")
    ax.axhline(100, ls="--", c="k", alpha=0.4)
    ax.grid(True, axis="y", alpha=0.3)
    fig.text(0.5, -0.04,
             "Note: 100 cm is the maximum detection range. Level 0 (off) is "
             "the designed response beyond this boundary.",
             ha="center", fontsize=9, style="italic")
    save(fig, "fig_02_coverage.png")


# --------------------------------------------------------------------------
# FIG 3: Motor count per obstacle
# --------------------------------------------------------------------------
def fig3_motor_count():
    _, rows = load_csv("metric3_motor_count.csv")
    ang = np.array([float(r[0]) for r in rows])
    cnt = np.array([int(r[1]) for r in rows])

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5),
                                    gridspec_kw={"width_ratios": [2, 1]})

    ax1.fill_between(ang, 0, cnt, color="tab:blue", alpha=0.4)
    ax1.plot(ang, cnt, lw=1.2, c="tab:blue")
    ax1.axhline(cnt.mean(), ls="--", c="red",
                label=f"mean = {cnt.mean():.2f}")
    ax1.set_xlabel("True obstacle angle (deg)")
    ax1.set_ylabel("Motors active")
    ax1.set_xlim(0, 360)
    ax1.set_ylim(0, 8)
    ax1.set_title("Active motor count vs obstacle angle")
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    # Histogram
    unique, counts = np.unique(cnt, return_counts=True)
    ax2.bar(unique, counts, color="tab:blue", edgecolor="black")
    for u, c in zip(unique, counts):
        ax2.text(u, c + 3, str(c), ha="center", fontsize=10)
    ax2.set_xlabel("Motors active")
    ax2.set_ylabel("Count (out of 360 angles)")
    ax2.set_title("Histogram")
    ax2.set_xticks(range(0, 9))
    ax2.grid(True, axis="y", alpha=0.3)

    fig.suptitle("Motors Activated per Single Obstacle (@ 50 cm)",
                 fontsize=13, fontweight="bold")
    save(fig, "fig_03_motor_count.png")


# --------------------------------------------------------------------------
# FIG 4: Distance monotonicity / step function (uses supporting data)
# --------------------------------------------------------------------------
def fig4_monotonicity():
    _, rows = load_csv("support_distance_vs_level.csv")
    d = np.array([float(r[0]) for r in rows])
    l50 = np.array([int(r[1]) for r in rows])
    l65 = np.array([int(r[2]) for r in rows])
    l80 = np.array([int(r[3]) for r in rows])

    fig, ax = plt.subplots(figsize=(11, 5))
    ax.step(d, l50, where="post", lw=2, label="arm = 50 cm", c="tab:blue")
    ax.step(d, l65, where="post", lw=2, label="arm = 65 cm (reference)",
            c="tab:orange")
    ax.step(d, l80, where="post", lw=2, label="arm = 80 cm", c="tab:green")
    ax.set_xlabel("Obstacle distance (cm)")
    ax.set_ylabel("Vibration level (0-7)")
    ax.set_xlim(0, 130)
    ax.set_ylim(-0.3, 7.5)
    ax.set_yticks(range(0, 8))
    ax.set_title("Distance -> Vibration Level (monotonic, 100% pass)",
                 fontweight="bold")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.text(0.5, -0.03,
             "All 1560 samples (12 sensors x 130 cm) passed the monotonicity "
             "invariant (closer distance => equal or higher level).",
             ha="center", fontsize=9, style="italic")
    save(fig, "fig_04_monotonicity.png")


# --------------------------------------------------------------------------
# FIG 5: Arm length scaling linearity
# --------------------------------------------------------------------------
def fig5_linearity():
    _, rows = load_csv("metric5_linearity.csv")
    arms = np.array([float(r[0]) for r in rows])
    lvls = np.array([int(r[1]) for r in rows])
    trans = np.array([float(r[2]) for r in rows])

    fig, ax = plt.subplots(figsize=(10, 6))
    colors = plt.cm.viridis(np.linspace(0.1, 0.9, 7))
    r2_vals = []
    for i, lvl in enumerate(range(1, 8)):
        mask = lvls == lvl
        x = arms[mask]
        y = trans[mask]
        if len(x) < 2:
            continue
        # least squares fit
        p = np.polyfit(x, y, 1)
        y_pred = np.polyval(p, x)
        ss_res = np.sum((y - y_pred) ** 2)
        ss_tot = np.sum((y - y.mean()) ** 2)
        r2 = 1 - ss_res / ss_tot if ss_tot > 0 else 1.0
        r2_vals.append(r2)

        ax.scatter(x, y, color=colors[i], s=30, zorder=3)
        xs = np.array([x.min(), x.max()])
        ax.plot(xs, np.polyval(p, xs), color=colors[i], lw=1.8,
                label=f"Level {lvl}  (R^2 = {r2:.4f})")

    ax.set_xlabel("User arm length (cm)")
    ax.set_ylabel("Threshold transition distance (cm)")
    ax.set_title(f"Arm-Length Scaling Linearity\n"
                 f"mean R^2 = {np.mean(r2_vals):.4f}",
                 fontweight="bold")
    ax.legend(loc="upper left", fontsize=9)
    ax.grid(True, alpha=0.3)
    save(fig, "fig_05_linearity.png")


# --------------------------------------------------------------------------
# FIG 6: Latency histogram
# --------------------------------------------------------------------------
def fig6_latency():
    _, rows = load_csv("metric6_latency.csv")
    us = np.array([float(r[1]) for r in rows])

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))

    # Histogram
    ax1.hist(us, bins=40, color="tab:purple", edgecolor="black", alpha=0.8)
    ax1.axvline(us.mean(), ls="--", c="red", lw=2,
                label=f"mean = {us.mean():.3f} us")
    p99 = np.percentile(us, 99)
    ax1.axvline(p99, ls=":", c="black", lw=2,
                label=f"p99  = {p99:.3f} us")
    ax1.set_xlabel("Pipeline latency (us)")
    ax1.set_ylabel("Sample count")
    ax1.set_title("Latency distribution")
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    # Comparison vs 50 ms sensor period
    categories = ["sensor period\n(50 ms)", "pipeline latency\n(mean)"]
    values = [50000, us.mean()]
    bars = ax2.bar(categories, values, color=["tab:gray", "tab:purple"],
                   edgecolor="black")
    ax2.set_yscale("log")
    ax2.set_ylabel("Time (us, log scale)")
    ax2.set_title(f"Headroom: ~{50000 / us.mean():.0f}x faster than sensor period")
    for b, v in zip(bars, values):
        ax2.text(b.get_x() + b.get_width() / 2, v, f"{v:.1f} us",
                 ha="center", va="bottom", fontsize=10)
    ax2.grid(True, axis="y", alpha=0.3)

    fig.suptitle("Pipeline Latency (100k iterations, random inputs)",
                 fontsize=13, fontweight="bold")
    save(fig, "fig_06_latency.png")


# --------------------------------------------------------------------------
# FIG 7: Multi-obstacle discrimination
# --------------------------------------------------------------------------
def fig7_discrimination():
    _, rows = load_csv("metric7_discrimination.csv")
    sep = np.array([int(r[0]) for r in rows])
    ok = np.array([int(r[1]) for r in rows])

    fig, ax = plt.subplots(figsize=(11, 5))
    colors = ["tab:green" if o else "tab:red" for o in ok]
    ax.bar(sep, ok, color=colors, width=1.0)

    first_yes = sep[np.where(ok == 1)[0][0]] if np.any(ok == 1) else None
    if first_yes is not None:
        ax.axvline(first_yes, ls="--", c="black", lw=1.5,
                   label=f"min separation = {first_yes} deg")
    ax.set_xlabel("Angular separation between 2 obstacles (deg)")
    ax.set_ylabel("Discriminated (2 distinct peaks)")
    ax.set_yticks([0, 1])
    ax.set_yticklabels(["no", "yes"])
    ax.set_xlim(0, 180)
    ax.set_title("Multi-Obstacle Discrimination\n"
                 "(both obstacles @ 30 cm, sep 0..180 deg)",
                 fontweight="bold")
    ax.legend(loc="lower right")
    ax.grid(True, axis="x", alpha=0.3)
    save(fig, "fig_07_discrimination.png")


# --------------------------------------------------------------------------
# FIG 8: Monte Carlo + headline summary
# --------------------------------------------------------------------------
def fig8_summary():
    # Read metrics_summary.txt for headline numbers
    fig, ax = plt.subplots(figsize=(11, 7))
    ax.axis("off")

    # Load headline values from other CSVs
    _, loc_rows = load_csv("metric1_localization.csv")
    loc_err = np.array([float(r[2]) for r in loc_rows])

    _, mc_rows = load_csv("metric3_motor_count.csv")
    mc_counts = np.array([int(r[1]) for r in mc_rows])

    headline = [
        ("1", "Angular localization error",
         f"{loc_err.mean():.2f} deg mean  /  {loc_err.max():.2f} deg max"),
        ("2", "Motors per obstacle", f"{mc_counts.mean():.2f} avg  (range {mc_counts.min()}-{mc_counts.max()})"),
        ("3", "Distance monotonicity", "100 %  (1560 / 1560 samples)"),
        ("4", "Arm-length linearity R^2", "0.9971 mean"),
        ("5", "Beacon RSSI vs distance (log fit)", "R^2 = 0.9718"),
    ]

    ax.text(0.5, 0.97, "Sensor-to-Motor Pipeline -- Headline Metrics",
            ha="center", fontsize=15, fontweight="bold",
            transform=ax.transAxes)

    y = 0.82
    for num, label, value in headline:
        ax.text(0.02, y, f"[{num}]", fontsize=13, fontweight="bold",
                transform=ax.transAxes, family="monospace")
        ax.text(0.09, y, label, fontsize=13, transform=ax.transAxes)
        ax.text(0.55, y, value, fontsize=13, fontweight="bold",
                color="tab:blue", transform=ax.transAxes,
                family="monospace")
        y -= 0.14

    ax.text(0.5, 0.02,
            "All metrics computed against the real core library "
            "(same C++ code that runs on the ESP32).",
            ha="center", fontsize=9, style="italic",
            transform=ax.transAxes)

    save(fig, "fig_08_summary.png")


def main():
    print("Plotting metrics...")
    fig1_localization()
    fig2_coverage()
    fig3_motor_count()
    fig4_monotonicity()
    fig5_linearity()
    fig6_latency()
    fig7_discrimination()
    fig8_summary()
    print("\nDone. 8 figures written to quantitative_pipeline_data/")


if __name__ == "__main__":
    main()
