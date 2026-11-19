#!/usr/bin/env python3
"""Summarize Wi-Fi lab CSVs; optional plots use matplotlib. Raw results are preserved."""
import argparse
import csv
from collections import defaultdict
import math
from pathlib import Path
import statistics

METRICS = ["aggregate_goodput_mbps", "mean_user_goodput_mbps", "min_user_goodput_mbps",
           "max_user_goodput_mbps", "jain", "pdr", "mean_delay_ms", "retry_fraction",
           "retry_limit_drops", "mean_mpdus_per_data_ppdu", "aggregate_throughput_mbps"]


def read(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def write(path, rows):
    if not rows:
        return
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def sample_mean(values):
    values = [float(v) for v in values if v is not None and math.isfinite(float(v))]
    return (statistics.mean(values), len(values)) if values else (None, 0)


def summarize(prefix):
    config = read(Path(str(prefix) + "-config.csv"))[0]
    flow = read(Path(str(prefix) + "-flows.csv"))
    mac = read(Path(str(prefix) + "-mac.csv"))
    n = int(config["n_sources"])
    goodput = [0.0] * n
    tx = rx = ip_bytes = 0
    delay_sum = 0.0
    for f in flow:
        goodput[int(f["user"])] = float(f["goodput_mbps"])
        ip_bytes += int(f["rx_ip_bytes"])
        tx += int(f["tx_packets"])
        rx += int(f["rx_packets"])
        if f["mean_delay_ms"]:
            delay_sum += float(f["mean_delay_ms"]) * int(f["rx_packets"])
    # For peer mode there is only one source (STA 0); exclude relay/receiver transmissions.
    source_mac = [m for m in mac if 1 <= int(m["node_id"]) <= n]
    attempts = sum(int(m["data_mpdu_attempts"]) for m in source_mac)
    retries = sum(int(m["retry_mpdu_attempts"]) for m in source_mac)
    ppdus = sum(int(m["data_ppdus"]) for m in source_mac)
    total = sum(goodput)
    sum_sq = sum(g * g for g in goodput)
    row = dict(config, group=prefix.parent.name,
               aggregate_offered_mbps=n * float(config["rate_mbps"]),
               aggregate_throughput_mbps=8 * ip_bytes / (float(config["duration_s"]) * 1e6),
               aggregate_goodput_mbps=total, mean_user_goodput_mbps=total / n,
               min_user_goodput_mbps=min(goodput), max_user_goodput_mbps=max(goodput),
               jain=total * total / (n * sum_sq) if sum_sq else None,
               pdr=rx / tx if tx else None, mean_delay_ms=delay_sum / rx if rx else None,
               retry_fraction=retries / attempts if attempts else None,
               retry_limit_drops=sum(int(m["retry_limit_drops"]) for m in source_mac),
               mean_mpdus_per_data_ppdu=attempts / ppdus if ppdus else None)
    return row, goodput


def plot_fairness(rows, user_rows, output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FormatStrFormatter, MaxNLocator
    equal = [r for r in rows if int(r["slow_station"]) == 0]
    runs = sorted({int(r["run"]) for r in equal})
    ns = sorted({int(r["n_stations"]) for r in equal})
    fig, axes = plt.subplots(2, 1, figsize=(6.9, 5.8), layout="constrained")
    colors = ["#0072b2", "#d55e00", "#009e73"]
    offsets = [(i-(len(runs)-1)/2)*.16 for i in range(len(runs))]
    deviations = [[] for _ in range(4)]
    for i, (run, offset) in enumerate(zip(runs, offsets)):
        users = sorted([r for r in user_rows if int(r["run"]) == run and
                        int(r["n_stations"]) == 4 and int(r["slow_station"]) == 0],
                       key=lambda r: int(r["user"]))
        if len(users) != 4:
            raise ValueError("Fairness plotting needs four equal-rate users in each run")
        rates = [float(r["goodput_mbps"]) for r in users]
        total = sum(rates)
        if total <= 0:
            raise ValueError("Shares are undefined when total goodput is zero")
        delta = [100*v/total-25 for v in rates]
        for u, d in enumerate(delta):
            deviations[u].append(d)
        axes[0].plot([u+offset for u in range(4)], delta, linestyle="none",
                     marker=["o", "s", "^"][i % 3], color=colors[i % 3], label=f"Run {run}")
        for j, n in enumerate(ns):
            value = next(r["jain"] for r in equal if int(r["run"]) == run and int(r["n_stations"]) == n)
            axes[1].plot(j+offset, float(value), marker=["o", "s", "^"][i % 3],
                         color=colors[i % 3], linestyle="none")
    axes[0].plot(range(4), [statistics.mean(v) for v in deviations], "kD", label="Mean of runs")
    axes[0].axhline(0, color=".4", linestyle="--")
    extent = max(1.25, 1.2*max(abs(v) for values in deviations for v in values))
    axes[0].set(xticks=range(4), xticklabels=[f"STA {u+1}" for u in range(4)],
                ylabel="Goodput share minus 25%\n(percentage points)", ylim=(-extent, extent),
                title="Four stations: zero marks an equal share")
    jains = [float(r["jain"]) for r in equal if r["jain"] not in [None, ""]]
    for j, n in enumerate(ns):
        mean = statistics.mean(float(r["jain"]) for r in equal if int(r["n_stations"]) == n)
        axes[1].plot(j, mean, "kD")
        axes[1].annotate(f"{mean:.5f}", (j, mean), xytext=(0, -18 if n == 4 else 9),
                         textcoords="offset points", ha="center", fontsize=9)
    margin = max(.0002, .1*(1-min(jains)))
    axes[1].axhline(1, color=".4", linestyle="--")
    axes[1].set(xticks=range(len(ns)), xticklabels=[str(n) for n in ns], xlabel="Number of stations",
                ylabel="Jain's goodput index", ylim=(max(0, min(jains)-margin), 1+margin),
                title="Jain's index: expanded vertical scale")
    axes[1].yaxis.set_major_locator(MaxNLocator(5))
    axes[1].yaxis.set_major_formatter(FormatStrFormatter("%.4f"))
    for ax in axes:
        ax.grid(alpha=.2)
        ax.spines[["top", "right"]].set_visible(False)
    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="outside upper center", ncol=min(len(runs)+1, 4), fontsize=9)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=240)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    parser.add_argument("--x", default="distance_m")
    parser.add_argument("--y", choices=METRICS, default="aggregate_goodput_mbps")
    parser.add_argument("--series", default="mcs", help="Comma-separated configuration fields")
    parser.add_argument("--plot", type=Path)
    parser.add_argument("--facet-rows", help="Configuration field for subplot rows")
    parser.add_argument("--facet-cols", help="Configuration field for subplot columns")
    parser.add_argument("--fairness-plot", type=Path, help="Plot equal-rate per-user shares and Jain indices")
    parser.add_argument("--log-y", action="store_true", help="Use a logarithmic metric axis")
    args = parser.parse_args()
    configs = sorted(args.results.rglob("*-config.csv"))
    if not configs:
        parser.error("No *-config.csv files found")
    rows, user_rows = [], []
    groups = defaultdict(list)
    user_groups = defaultdict(list)
    for path in configs:
        prefix = Path(str(path)[:-len("-config.csv")])
        row, users = summarize(prefix)
        rows.append(row)
        # A treatment consists of every config field except random seed and run.
        config = read(path)[0]
        key = tuple((k, v) for k, v in config.items() if k not in {"seed", "run", "pcap"}) + (("group", row["group"]),)
        groups[key].append(row)
        for user, value in enumerate(users):
            user_rows.append(dict(config, group=row["group"], user=user, goodput_mbps=value))
            user_groups[(key, user)].append(value)
    averages = []
    for key, samples in groups.items():
        averaged = dict(key)
        averaged["aggregate_offered_mbps"] = samples[0]["aggregate_offered_mbps"]
        averaged["runs"] = len(samples)
        for metric in METRICS:
            mean, count = sample_mean([s[metric] for s in samples])
            averaged[metric] = mean
            averaged[metric + "_valid_runs"] = count
        averages.append(averaged)
    user_averages = []
    for (key, user), values in user_groups.items():
        mean, count = sample_mean(values)
        user_averages.append(dict(key, user=user, goodput_mbps=mean, runs=count))
    write(args.results / "per-run.csv", rows)
    write(args.results / "averages.csv", averages)
    write(args.results / "per-user.csv", user_rows)
    write(args.results / "user-averages.csv", user_averages)
    print(f"Summarized {len(rows)} runs in {len(groups)} treatments; undefined values are blank.")
    if args.fairness_plot:
        plot_fairness(rows, user_rows, args.fairness_plot)
    if args.plot:
        try:
            import matplotlib
        except ImportError:
            parser.error("Summary CSVs were written; plotting requires the matplotlib package")
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        series_fields = args.series.split(",")
        facet_fields = [f for f in [args.facet_rows, args.facet_cols] if f]
        for row in averages:
            if args.x not in row or any(k not in row for k in series_fields + facet_fields):
                parser.error("Unknown x, series or facet field")
        def levels(field):
            if field is None:
                return [None]
            values = {str(row[field]) for row in averages}
            try:
                return sorted(values, key=float)
            except ValueError:
                return sorted(values)
        row_levels, col_levels = levels(args.facet_rows), levels(args.facet_cols)
        nr, nc = len(row_levels), len(col_levels)
        size = (7.0, 4.5) if nr * nc == 1 else (4.0 * nc, 3.1 * nr)
        fig, axs = plt.subplots(nr, nc, figsize=size, squeeze=False, layout="constrained",
                                sharex=True, sharey=True if args.y == "pdr" or args.log_y else "col")
        ylabels = {"aggregate_goodput_mbps": "Application goodput (Mbit/s)",
                   "aggregate_throughput_mbps": "IP throughput (Mbit/s)",
                   "pdr": "Packet delivery ratio",
                   "mean_delay_ms": "Mean delay of delivered packets (ms)"}
        for ri, rv in enumerate(row_levels):
            for ci_, cv in enumerate(col_levels):
                ax = axs[ri, ci_]
                series = defaultdict(list)
                for row in averages:
                    if args.facet_rows and str(row[args.facet_rows]) != rv:
                        continue
                    if args.facet_cols and str(row[args.facet_cols]) != cv:
                        continue
                    if row[args.y] is None:
                        continue
                    label = ", ".join(f"{key}={row[key]}" for key in series_fields)
                    series[label].append(row)
                for label, values in sorted(series.items()):
                    values.sort(key=lambda v: float(v[args.x]))
                    # Reject treatment mixing within each series and subplot.
                    x = [float(v[args.x]) for v in values]
                    if len(set(x)) != len(x):
                        parser.error("Duplicate x within a series; add fields to --series or use facets")
                    ax.plot(x, [v[args.y] for v in values], marker="o", label=label)
                ax.set_xlabel("Total permitted attempts" if args.x == "retry_limit" else args.x.replace("_", " "))
                if args.x == "retry_limit":
                    ax.set_xticks(sorted({float(row[args.x]) for row in averages}))
                ax.set_ylabel(ylabels.get(args.y, args.y.replace("_", " ")))
                titles = []
                if args.facet_rows:
                    titles.append(f"{args.facet_rows}={rv}")
                if args.facet_cols:
                    titles.append(f"{args.facet_cols}={cv}")
                ax.set_title(", ".join(titles), fontsize=10)
                if args.y == "pdr":
                    ax.set_ylim(0, 1.05)
                if args.log_y:
                    ax.set_yscale("log")
                ax.grid(alpha=0.3)
                if series:
                    ax.legend(fontsize=8)
        args.plot.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(args.plot, dpi=180)



if __name__ == "__main__":
    main()
