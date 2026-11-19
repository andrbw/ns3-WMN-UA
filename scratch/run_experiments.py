#!/usr/bin/env python3
"""Run the Wi-Fi lab experiment grids from a configured ns-3.48 tree."""
import argparse
import itertools
from pathlib import Path
import subprocess


def cases(group, quick):
    if group == "association":
        return [dict(mode="infra"), dict(mode="infra", activeProbing=1),
                dict(mode="infra", badSsid=1), dict(mode="adhoc"),
                dict(mode="infra", traffic="peer", nStations=2),
                dict(mode="adhoc", traffic="peer", nStations=2)]
    if group == "mcs":
        distances = list(range(5, 111, 5)) if quick else list(range(5, 111))
        # Extend the MCS comparison beyond the default preamble RSSI cutoff.
        return [dict(mcs=m, distance=d, rateMbps=20, minimumRssi=-95)
                for m, d in itertools.product([0, 2, 4, 7], distances)]
    if group == "retries":
        return [dict(nStations=1, mcs=7, distance=d, retryLimit=k,
                     payload=p, rateMbps=r)
                for d, k, p, r in itertools.product([5, 30], [1, 3, 7, 15],
                                                   [200, 1200], [1, 20])]
    if group == "saturation":
        rates = [1, 10, 100] if quick else [0.1, 0.5, 1, 2, 5, 10, 20, 50, 100]
        return [dict(nStations=n, rateMbps=r)
                for n, r in itertools.product([1, 4, 8], rates)]
    if group == "fairness":
        return [dict(nStations=n, rateMbps=50) for n in [1, 2, 4, 8]] + [
            dict(nStations=4, rateMbps=50, slowStation=1)]
    if group == "aggregation":
        return [dict(ampdu=a, payload=p, rateMbps=r)
                for a, p, r in itertools.product([0, 8192, 65535], [200, 1200], [1, 100])]
    raise ValueError(group)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("group", choices=["association", "mcs", "retries", "saturation", "fairness", "aggregation"])
    parser.add_argument("--ns3-root", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=Path("results"))
    parser.add_argument("--runs", default="1,2,3")
    parser.add_argument("--duration", type=float, default=10)
    parser.add_argument("--quick", action="store_true", help="Reduced distance/load grid (not a substitute for repetitions)")
    parser.add_argument("--pcap", action="store_true", help="Use only for small diagnostic grids")
    args = parser.parse_args()
    root = args.ns3_root.resolve()
    wrapper = root / "ns3"
    if not wrapper.is_file():
        parser.error("--ns3-root must contain the ns3 wrapper")
    output = (args.out / args.group).resolve()
    output.mkdir(parents=True, exist_ok=True)
    runs = [int(r) for r in args.runs.split(",")]
    if not runs or any(r < 1 for r in runs) or args.duration <= 0:
        parser.error("Use positive run numbers and duration")
    matrix = cases(args.group, args.quick)
    for number, params in enumerate(matrix, 1):
        for run in runs:
            prefix = output / f"case{number:03d}-r{run}"
            # ns3 parses the program argument string with shlex. Use quoting for paths.
            import shlex
            settings = dict(params, RngSeed=1, RngRun=run, duration=args.duration,
                            pcap=int(args.pcap), prefix=str(prefix))
            program = "wifi-lab " + " ".join(shlex.quote(f"--{k}={v}") for k, v in settings.items())
            print(f"{args.group}: case {number}/{len(matrix)}, run {run}", flush=True)
            subprocess.run([str(wrapper), "run", "--no-build", program], cwd=root, check=True)


if __name__ == "__main__":
    main()
