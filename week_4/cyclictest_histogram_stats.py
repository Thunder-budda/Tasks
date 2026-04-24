#!/usr/bin/env python3
"""
Calculate secondary statistics from cyclictest histogram output.

Usage:
  python3 cyclictest_histogram_stats.py no_load.log load.log

The input file should contain cyclictest output with a "# Histogram" section,
for example:

  # Histogram
  000030 028754
  ...
  # Min Latencies: 00008
"""

from __future__ import annotations

import argparse
import pathlib
from dataclasses import dataclass


@dataclass(frozen=True)
class HistogramStats:
    samples: int
    min_us: int
    avg_us: float
    p50_us: int
    p90_us: int
    p99_us: int
    p999_us: int
    le_50: int
    le_50_pct: float
    le_100: int
    le_100_pct: float
    gt_100: int
    gt_100_pct: float
    gt_200: int
    gt_200_pct: float
    max_us: int


def parse_summary_value(text: str, label: str) -> int | None:
    prefix = f"# {label}:"

    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line.startswith(prefix):
            continue

        values = line[len(prefix) :].split()
        if not values:
            return None

        try:
            return int(values[0])
        except ValueError:
            return None

    return None


def parse_histogram(text: str) -> list[tuple[int, int]]:
    rows: list[tuple[int, int]] = []
    in_histogram = False

    for raw_line in text.splitlines():
        line = raw_line.strip()

        if line == "# Histogram":
            in_histogram = True
            continue

        if in_histogram and line.startswith("#"):
            break

        if not in_histogram or not line:
            continue

        parts = line.split()
        if len(parts) != 2:
            continue

        try:
            latency_us = int(parts[0])
            count = int(parts[1])
        except ValueError:
            continue

        rows.append((latency_us, count))

    if not rows:
        raise ValueError("no cyclictest histogram rows found")

    return rows


def percentile(rows: list[tuple[int, int]], ratio: float, samples: int) -> int:
    target = int(samples * ratio + 0.999999)
    cumulative = 0

    for latency_us, count in rows:
        cumulative += count
        if cumulative >= target:
            return latency_us

    return rows[-1][0]


def count_le(rows: list[tuple[int, int]], threshold_us: int) -> int:
    return sum(count for latency_us, count in rows if latency_us <= threshold_us)


def count_gt(rows: list[tuple[int, int]], threshold_us: int) -> int:
    return sum(count for latency_us, count in rows if latency_us > threshold_us)


def calculate_stats(rows: list[tuple[int, int]], min_us: int | None = None) -> HistogramStats:
    samples = sum(count for _, count in rows)
    weighted_sum = sum(latency_us * count for latency_us, count in rows)
    le_50 = count_le(rows, 50)
    le_100 = count_le(rows, 100)
    gt_100 = count_gt(rows, 100)
    gt_200 = count_gt(rows, 200)

    return HistogramStats(
        samples=samples,
        min_us=min_us if min_us is not None else min(latency_us for latency_us, count in rows if count),
        avg_us=weighted_sum / samples,
        p50_us=percentile(rows, 0.50, samples),
        p90_us=percentile(rows, 0.90, samples),
        p99_us=percentile(rows, 0.99, samples),
        p999_us=percentile(rows, 0.999, samples),
        le_50=le_50,
        le_50_pct=le_50 * 100 / samples,
        le_100=le_100,
        le_100_pct=le_100 * 100 / samples,
        gt_100=gt_100,
        gt_100_pct=gt_100 * 100 / samples,
        gt_200=gt_200,
        gt_200_pct=gt_200 * 100 / samples,
        max_us=max(latency_us for latency_us, count in rows if count),
    )


def print_stats(name: str, stats: HistogramStats) -> None:
    print(f"## {name}")
    print()
    print("| Metric | Value |")
    print("|---|---:|")
    print(f"| Samples | {stats.samples} |")
    print(f"| Min | {stats.min_us} us |")
    print(f"| Avg | {stats.avg_us:.2f} us |")
    print(f"| P50 | {stats.p50_us} us |")
    print(f"| P90 | {stats.p90_us} us |")
    print(f"| P99 | {stats.p99_us} us |")
    print(f"| P99.9 | {stats.p999_us} us |")
    print(f"| <=50 us | {stats.le_50} / {stats.samples} = {stats.le_50_pct:.4f}% |")
    print(f"| <=100 us | {stats.le_100} / {stats.samples} = {stats.le_100_pct:.4f}% |")
    print(f"| >100 us | {stats.gt_100} / {stats.samples} = {stats.gt_100_pct:.4f}% |")
    print(f"| >200 us | {stats.gt_200} / {stats.samples} = {stats.gt_200_pct:.4f}% |")
    print(f"| Max | {stats.max_us} us |")
    print()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Calculate secondary statistics from cyclictest histogram output."
    )
    parser.add_argument(
        "files",
        nargs="+",
        type=pathlib.Path,
        help="cyclictest output files containing a '# Histogram' section",
    )
    args = parser.parse_args()

    for path in args.files:
        text = path.read_text(encoding="utf-8")
        rows = parse_histogram(text)
        stats = calculate_stats(rows, parse_summary_value(text, "Min Latencies"))
        print_stats(path.name, stats)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
