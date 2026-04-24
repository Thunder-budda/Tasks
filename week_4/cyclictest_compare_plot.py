#!/usr/bin/env python3
"""
Plot latency line charts from multiple cyclictest histogram log files on one figure.

Usage:
  python3 cyclictest_compare_plot.py file1.log file2.log file3.log [options]

Options:
  --output FILE     Save figure to FILE instead of displaying interactively
  --log-y           Use logarithmic scale on Y-axis (useful for long tails)
  --percent         Plot Y-axis as percentage of total samples instead of raw count
  --xlim MAX_US     Clip X-axis to MAX_US microseconds (default: auto)
  --title TITLE     Custom figure title
"""

from __future__ import annotations

import argparse
import pathlib
import sys


PERCENTILE_MARKS = [
    (0.50,  "P50",  "--",  0.6),
    (0.99,  "P99",  "-.",  0.7),
    (0.999, "P99.9", ":", 0.8),
]

AUTO_XLIM_PERCENTILE = 0.999
AUTO_XLIM_MARGIN = 1.15

COLORS = [
    "#1f77b4",  # blue
    "#d62728",  # red
    "#2ca02c",  # green
    "#ff7f0e",  # orange
    "#9467bd",  # purple
    "#8c564b",  # brown
]


# ---------------------------------------------------------------------------
# Parsing (reused from cyclictest_histogram_stats.py logic)
# ---------------------------------------------------------------------------

def parse_histogram(text: str) -> list[tuple[int, int]]:
    """Return [(latency_us, count), ...] from a cyclictest log."""
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
            rows.append((int(parts[0]), int(parts[1])))
        except ValueError:
            continue

    if not rows:
        raise ValueError("no histogram rows found")

    return rows


def parse_summary_value(text: str, label: str) -> int | None:
    prefix = f"# {label}:"
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if line.startswith(prefix):
            values = line[len(prefix):].split()
            try:
                return int(values[0]) if values else None
            except ValueError:
                return None
    return None


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------

def compute_percentile(rows: list[tuple[int, int]], ratio: float) -> int:
    samples = sum(c for _, c in rows)
    target = int(samples * ratio + 0.999999)
    cumulative = 0
    for latency_us, count in rows:
        cumulative += count
        if cumulative >= target:
            return latency_us
    return rows[-1][0]


def trim_trailing_zeros(rows: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """Remove trailing zero-count entries for a cleaner plot."""
    i = len(rows) - 1
    while i > 0 and rows[i][1] == 0:
        i -= 1
    return rows[: i + 1]


def round_up_nicely(value: int) -> int:
    """Round up to a chart-friendly axis maximum."""
    if value <= 0:
        return 1

    magnitude = 10 ** (len(str(value)) - 1)
    for factor in (1.0, 1.5, 2.0, 2.5, 5.0, 10.0):
        candidate = int(magnitude * factor)
        if value <= candidate:
            return candidate

    return 10 * magnitude


def choose_auto_xlim(datasets: list[tuple[str, list[tuple[int, int]], int | None]]) -> int:
    """Choose an X-axis limit that focuses on the dense latency region."""
    focus_max = 0

    for _, rows, _ in datasets:
        trimmed_rows = trim_trailing_zeros(rows)
        focus_max = max(focus_max, compute_percentile(trimmed_rows, AUTO_XLIM_PERCENTILE))

    buffered_limit = max(focus_max + 5, int(focus_max * AUTO_XLIM_MARGIN + 0.999999))
    return round_up_nicely(buffered_limit)


def build_plot(
    datasets: list[tuple[str, list[tuple[int, int]], int | None]],
    *,
    log_y: bool,
    percent: bool,
    xlim: int | None,
    title: str,
    output: pathlib.Path | None,
) -> None:
    try:
        import matplotlib
        if output:
            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import matplotlib.ticker as ticker
    except ImportError:
        sys.exit("Error: matplotlib is required. Install it with: pip install matplotlib")

    fig, ax = plt.subplots(figsize=(12, 6))
    visible_xlim = xlim if xlim is not None else choose_auto_xlim(datasets)

    for idx, (label, rows, _min_us) in enumerate(datasets):
        full_rows = trim_trailing_zeros(rows)
        visible_rows = [(lat, cnt) for lat, cnt in full_rows if lat <= visible_xlim]
        if not visible_rows:
            visible_rows = [next((row for row in full_rows if row[1] > 0), full_rows[0])]

        color = COLORS[idx % len(COLORS)]
        xs = [lat for lat, _ in visible_rows]
        total = sum(cnt for _, cnt in full_rows) if percent else sum(cnt for _, cnt in visible_rows)
        ys = [(cnt / total * 100) if percent else cnt for _, cnt in visible_rows]

        # Main line
        ax.plot(xs, ys, color=color, linewidth=1.4, label=label, zorder=3)

        # Percentile vertical markers
        for ratio, pname, lstyle, alpha in PERCENTILE_MARKS:
            p_val = compute_percentile(full_rows, ratio)
            # Only draw if within visible x range
            if p_val <= visible_xlim:
                ax.axvline(
                    x=p_val,
                    color=color,
                    linestyle=lstyle,
                    linewidth=0.9,
                    alpha=alpha,
                    zorder=2,
                )
                # Annotate the last dataset's percentile to avoid clutter;
                # for multiple datasets, annotate each at a slight y offset
                y_max = ax.get_ylim()[1] if ax.get_ylim()[1] > 0 else 1
                ax.annotate(
                    f"{pname}={p_val}µs",
                    xy=(p_val, 0),
                    xytext=(p_val + 1, (0.05 + idx * 0.08) * (ax.get_ylim()[1] or 1)),
                    fontsize=7,
                    color=color,
                    rotation=90,
                    va="bottom",
                )

    # Axes labels and formatting
    ax.set_xlabel("Latency (µs)", fontsize=12)
    y_label = "Percentage of Samples (%)" if percent else "Sample Count"
    ax.set_ylabel(y_label, fontsize=12)
    ax.set_title(title, fontsize=13, fontweight="bold")

    if log_y:
        ax.set_yscale("log")
        ax.yaxis.set_major_formatter(ticker.LogFormatter(minor_thresholds=(2, 0.4)))
    else:
        ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda v, _: f"{int(v):,}"))

    ax.set_xlim(left=0, right=visible_xlim)

    ax.set_ylim(bottom=0 if not log_y else None)
    ax.grid(True, which="both" if log_y else "major", linestyle="--", linewidth=0.5, alpha=0.6)
    ax.legend(fontsize=10, loc="upper right", framealpha=0.85)
    fig.tight_layout()

    if output:
        fig.savefig(output, dpi=150)
        print(f"Figure saved to: {output}")
    else:
        plt.show()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare cyclictest latency distributions from multiple log files in one plot."
    )
    parser.add_argument(
        "files",
        nargs="+",
        type=pathlib.Path,
        help="cyclictest log files (2–6 files recommended)",
    )
    parser.add_argument(
        "--output", "-o",
        type=pathlib.Path,
        default=None,
        metavar="FILE",
        help="save figure to FILE (png/pdf/svg) instead of displaying interactively",
    )
    parser.add_argument(
        "--log-y",
        action="store_true",
        help="use logarithmic scale on Y-axis",
    )
    parser.add_argument(
        "--percent",
        action="store_true",
        help="plot Y-axis as percentage of total samples",
    )
    parser.add_argument(
        "--xlim",
        type=int,
        default=None,
        metavar="MAX_US",
        help="clip X-axis to MAX_US microseconds (default: auto)",
    )
    parser.add_argument(
        "--title",
        default="Cyclictest Latency Distribution Comparison",
        help="custom figure title",
    )
    args = parser.parse_args()

    datasets: list[tuple[str, list[tuple[int, int]], int | None]] = []
    for path in args.files:
        try:
            text = path.read_text(encoding="utf-8")
        except OSError as e:
            print(f"Error reading {path}: {e}", file=sys.stderr)
            return 1
        try:
            rows = parse_histogram(text)
        except ValueError as e:
            print(f"Error parsing {path}: {e}", file=sys.stderr)
            return 1
        min_us = parse_summary_value(text, "Min Latencies")
        datasets.append((path.stem, rows, min_us))

    build_plot(
        datasets,
        log_y=args.log_y,
        percent=args.percent,
        xlim=args.xlim,
        title=args.title,
        output=args.output,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
