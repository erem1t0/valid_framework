#!/usr/bin/env python3
"""
Average benchmark JSON timeseries in a directory and plot two line charts:
1) average ops/sec per measurement
2) average memory per measurement

Expected JSON shape:
{
  "timeseries": [
    {"op": 110000, "ops_per_sec": 2128018, "memory_bytes": 69621520},
    ...
  ]
}

Usage:
    python plot_json_average_ops_memory.py ./logs
    python plot_json_average_ops_memory.py ./logs --output averages.html
    python plot_json_average_ops_memory.py ./logs --csv averages.csv
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Any

import numpy as np
import plotly.graph_objects as go
from plotly.subplots import make_subplots


THROUGHPUT_FIELD = "ops_per_sec"

MEMORY_DIVISORS = {
    "bytes": 1.0,
    "kb": 1000.0,
    "mb": 1000.0 * 1000.0,
    "gb": 1000.0 * 1000.0 * 1000.0,
    "kib": 1024.0,
    "mib": 1024.0 * 1024.0,
    "gib": 1024.0 * 1024.0 * 1024.0,
}

MEMORY_LABELS = {
    "bytes": "bytes",
    "kb": "KB",
    "mb": "MB",
    "gb": "GB",
    "kib": "KiB",
    "mib": "MiB",
    "gib": "GiB",
}


def load_timeseries(path: Path) -> list[dict[str, Any]]:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)

    ts = data.get("timeseries")
    if not isinstance(ts, list) or not ts:
        raise ValueError(f"{path}: missing or empty 'timeseries'")

    for i, row in enumerate(ts):
        if not isinstance(row, dict):
            raise ValueError(f"{path}: timeseries[{i}] is not an object")
        for field in ("op", THROUGHPUT_FIELD, "memory_bytes"):
            if field not in row:
                raise ValueError(f"{path}: timeseries[{i}] has no '{field}'")

    return ts


def numeric_row(ts: list[dict[str, Any]], field: str, path: Path) -> list[float]:
    out: list[float] = []

    for i, row in enumerate(ts):
        try:
            out.append(float(row[field]))
        except KeyError as exc:
            raise ValueError(f"{path}: timeseries[{i}] has no '{field}'") from exc
        except (TypeError, ValueError) as exc:
            raise ValueError(f"{path}: timeseries[{i}]['{field}'] is not numeric") from exc

    return out


def assert_same_ops(reference_ops: list[int], ops: list[int], path: Path) -> None:
    if len(reference_ops) != len(ops):
        raise ValueError(
            f"{path}: different number of measurements "
            f"({len(ops)} instead of {len(reference_ops)})"
        )

    if reference_ops != ops:
        for i, (expected, actual) in enumerate(zip(reference_ops, ops)):
            if expected != actual:
                raise ValueError(
                    f"{path}: different op value at measurement #{i}: "
                    f"{actual} instead of {expected}"
                )

        raise ValueError(f"{path}: op values differ")


def collect_series(directory: Path, pattern: str, strict_ops: bool):
    files = sorted(p for p in directory.glob(pattern) if p.is_file())
    if not files:
        raise ValueError(f"No files matching {pattern!r} in {directory}")

    reference_ops: list[int] | None = None
    throughput_rows: list[list[float]] = []
    memory_rows: list[list[float]] = []
    used_files: list[Path] = []

    for path in files:
        ts = load_timeseries(path)
        ops = [int(row["op"]) for row in ts]

        if reference_ops is None:
            reference_ops = ops
        elif strict_ops:
            assert_same_ops(reference_ops, ops, path)
        elif len(ops) != len(reference_ops):
            raise ValueError(
                f"{path}: different number of measurements "
                f"({len(ops)} instead of {len(reference_ops)}). "
                "Disable strict op checks only when op values differ but lengths are equal."
            )

        throughput_rows.append(numeric_row(ts, THROUGHPUT_FIELD, path))
        memory_rows.append(numeric_row(ts, "memory_bytes", path))
        used_files.append(path)

    assert reference_ops is not None

    return (
        reference_ops,
        np.array(throughput_rows, dtype=float),
        np.array(memory_rows, dtype=float),
        used_files,
    )


def write_csv(
    output: Path,
    ops: list[int],
    avg_ops_per_sec: np.ndarray,
    avg_memory: np.ndarray,
    memory_unit: str,
) -> None:
    with output.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["op", "avg_ops_per_sec", f"avg_memory_{memory_unit}"])

        for op, ops_per_sec, memory in zip(ops, avg_ops_per_sec, avg_memory):
            writer.writerow([op, f"{ops_per_sec:.6f}", f"{memory:.6f}"])


def build_figure(
    ops: list[int],
    avg_ops_per_sec: np.ndarray,
    avg_memory: np.ndarray,
    memory_unit: str,
    files_count: int,
    source_dir: Path,
):
    memory_label = MEMORY_LABELS[memory_unit]

    fig = make_subplots(
        rows=2,
        cols=1,
        shared_xaxes=True,
        subplot_titles=(
            f"Average throughput ({files_count} files)",
            f"Average memory ({files_count} files)",
        ),
        vertical_spacing=0.10,
    )

    fig.add_trace(
        go.Scatter(
            x=ops,
            y=avg_ops_per_sec,
            mode="lines+markers",
            name="Average ops/sec",
            line=dict(width=2),
            marker=dict(size=5),
        ),
        row=1,
        col=1,
    )

    fig.add_trace(
        go.Scatter(
            x=ops,
            y=avg_memory,
            mode="lines+markers",
            name=f"Average memory, {memory_label}",
            line=dict(width=2),
            marker=dict(size=5),
        ),
        row=2,
        col=1,
    )

    fig.update_layout(
        title=f"Averaged benchmark measurements: {source_dir}",
        template="plotly_white",
        height=800,
        font=dict(size=14),
        legend=dict(font=dict(size=13)),
    )

    fig.update_xaxes(title_text="Operation #", row=2, col=1)
    fig.update_yaxes(title_text="ops/sec", row=1, col=1)
    fig.update_yaxes(title_text=memory_label, row=2, col=1)

    return fig


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Average all benchmark JSON timeseries in a directory and plot average ops/sec and memory."
    )
    parser.add_argument("directory", help="Directory with JSON log files")
    parser.add_argument("--pattern", default="*.json", help="Glob pattern for input files, default: *.json")
    parser.add_argument(
        "--memory-unit",
        choices=tuple(MEMORY_DIVISORS.keys()),
        default="mib",
        help="Memory unit for the plot, default: mib",
    )
    parser.add_argument("--output", default="averages.html", help="Output HTML file, default: averages.html")
    parser.add_argument("--csv", default="", help="Optional CSV output with averaged values")
    parser.add_argument(
        "--no-strict-ops",
        action="store_true",
        help="Allow files with the same number of measurements but different op values",
    )

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    directory = Path(args.directory)

    if not directory.is_dir():
        print(f"Error: not a directory: {directory}", file=sys.stderr)
        return 2

    try:
        ops, throughput_rows, memory_rows, used_files = collect_series(
            directory=directory,
            pattern=args.pattern,
            strict_ops=not args.no_strict_ops,
        )
    except ValueError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    memory_divisor = MEMORY_DIVISORS[args.memory_unit]
    avg_ops_per_sec = throughput_rows.mean(axis=0)
    avg_memory = memory_rows.mean(axis=0) / memory_divisor

    fig = build_figure(
        ops=ops,
        avg_ops_per_sec=avg_ops_per_sec,
        avg_memory=avg_memory,
        memory_unit=args.memory_unit,
        files_count=len(used_files),
        source_dir=directory,
    )

    output = Path(args.output)
    fig.write_html(output, include_plotlyjs="cdn")
    print(f"Read {len(used_files)} JSON files")
    print(f"Saved plot -> {output}")

    if args.csv:
        csv_output = Path(args.csv)
        write_csv(csv_output, ops, avg_ops_per_sec, avg_memory, args.memory_unit)
        print(f"Saved CSV  -> {csv_output}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
