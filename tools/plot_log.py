# Script for drawing graphics for a 1 log file

import json
import sys
from pathlib import Path

import plotly.graph_objects as go
from plotly.subplots import make_subplots


def line(title, x, y, y_title, name):
    fig = go.Figure()

    fig.add_trace(go.Scatter(
        x=x,
        y=y,
        mode="lines",
        name=name,
    ))

    fig.update_layout(
        title=title,
        xaxis_title="operation",
        yaxis_title=y_title,
        template="plotly_white",
        hovermode="x unified",
        showlegend=True,
    )

    return fig

def main():

    if len(sys.argv) != 3:
        print("usage: python plot_log.py log.json out_dir")
        return 1

    log_path = Path(sys.argv[1])
    out_dir  = Path(sys.argv[2])

    with log_path.open("r", encoding="utf-8") as file:
        log = json.load(file)

    rows = log.get("timeseries", [])

    if not rows:
        print("no timeseries records found")
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)

    name = log.get("metadata", {}).get("container_name", log_path.stem)

    ops             = [row["op"]            for row in rows]
    throughput      = [row["ops_per_sec"]   for row in rows]
    memory          = [row["memory_bytes"]  for row in rows]
    chunk_time      = [row["chunk_ms"]      for row in rows]
    cumulative_time = [row["cumulative_ms"] for row in rows]

    charts = [
        ("throughput.html",      line("Throughput",      ops, throughput,      "ops/sec", name)),
        ("memory.html",          line("Memory usage",    ops, memory,          "bytes",   name)),
        ("chunk_time.html",      line("Chunk time",      ops, chunk_time,      "ms",      name)),
        ("cumulative_time.html", line("Cumulative time", ops, cumulative_time, "ms",      name)),
    ]

    for filename, fig in charts:
        fig.write_html(str(out_dir / filename), include_plotlyjs="cdn")

    summary = make_subplots(
        rows=4,
        cols=1,
        shared_xaxes=False,
        subplot_titles=[
            "Throughput",
            "Memory usage",
            "Chunk time",
            "Cumulative time",
        ],
        vertical_spacing=0.08,
    )

    summary.add_trace(
        go.Scatter(x=ops, y=throughput, mode="lines", name=f"{name}: ops/sec"),
        row=1, col=1,
    )

    summary.add_trace(
        go.Scatter(x=ops, y=memory, mode="lines", name=f"{name}: memory"),
        row=2, col=1,
    )

    summary.add_trace(
        go.Scatter(x=ops, y=chunk_time, mode="lines", name=f"{name}: chunk time"),
        row=3, col=1,
    )

    summary.add_trace(
        go.Scatter(x=ops, y=cumulative_time, mode="lines", name=f"{name}: cumulative time"),
        row=4, col=1,
    )

    summary.update_layout(
        title=f"Run summary: {name}",
        template="plotly_white",
        hovermode="x unified",
        height=1200,
        showlegend=True,
    )

    for row in range(1, 5):
        summary.update_xaxes(title_text="operation", row=row, col=1)

    summary.update_yaxes(title_text="ops/sec",  row=1, col=1)
    summary.update_yaxes(title_text="bytes",    row=2, col=1)
    summary.update_yaxes(title_text="ms",       row=3, col=1)
    summary.update_yaxes(title_text="ms",       row=4, col=1)

    summary.write_html(str(out_dir / "summary.html"), include_plotlyjs="cdn")

    print(f"written: {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
