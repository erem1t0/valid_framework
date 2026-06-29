# Script for drawing comparison graphics for 2 log files

import json
import sys
from pathlib import Path

import plotly.graph_objects as go
from plotly.subplots import make_subplots


TEST_COLOR  = "#1f77b4"
VALID_COLOR = "#ff0e0e"

def line(title, x1, y1, name1, color1, x2, y2, name2, color2, y_title):
    fig = go.Figure()

    fig.add_trace(go.Scatter(
        x=x1,
        y=y1,
        mode="lines",
        name=name1,
        line=dict(color=color1),
    ))

    fig.add_trace(go.Scatter(
        x=x2,
        y=y2,
        mode="lines",
        name=name2,
        line=dict(color=color2),
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

def ratio_line(title, x, y, y_title, name):
    fig = go.Figure()

    fig.add_trace(go.Scatter(
        x=x,
        y=y,
        mode="lines",
        name=name,
    ))

    fig.add_hline(y=1.0, line_dash="dash")

    fig.update_layout(
        title=title,
        xaxis_title="operation",
        yaxis_title=y_title,
        template="plotly_white",
        hovermode="x unified",
        showlegend=True,
    )

    return fig

def metric_by_op(rows, metric):
    result = dict()

    for row in rows:
        if metric not in row:
            continue

        result[int(row["op"])] = float(row[metric])

    return result

def make_ratio(test_rows, valid_rows, metric):
    test  = metric_by_op(test_rows, metric)
    valid = metric_by_op(valid_rows, metric)

    ops = sorted(set(test) & set(valid))

    x = []
    y = []

    for op in ops:
        if valid[op] == 0:
            continue

        x.append(op)
        y.append(test[op] / valid[op])

    return x, y

def main():

    if len(sys.argv) != 4:
        print("usage: python compare_logs.py test_log.json valid_log.json out_dir")
        return 1

    test_path   = Path(sys.argv[1])
    valid_path  = Path(sys.argv[2])
    out_dir     = Path(sys.argv[3])

    with test_path.open("r", encoding="utf-8") as file:
        test_log = json.load(file) 
    with valid_path.open("r", encoding="utf-8") as file:
        valid_log = json.load(file)

    test_rows  = test_log.get("timeseries", [])
    valid_rows = valid_log.get("timeseries", [])

    if not test_rows:
        print("no timeseries records found in test log")
        return 1

    if not valid_rows:
        print("no timeseries records found in valid log")
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)

    test_name  = test_log.get("metadata", {}).get("container_name", test_path.stem)
    valid_name = valid_log.get("metadata", {}).get("container_name", valid_path.stem)

    test_ops  = [row["op"] for row in test_rows]
    valid_ops = [row["op"] for row in valid_rows]

    test_throughput  = [row["ops_per_sec"] for row in test_rows]
    valid_throughput = [row["ops_per_sec"] for row in valid_rows]

    test_memory  = [row["memory_bytes"] for row in test_rows]
    valid_memory = [row["memory_bytes"] for row in valid_rows]

    speedup_x, speedup_y = make_ratio(test_rows, valid_rows, "ops_per_sec")
    memory_ratio_x, memory_ratio_y = make_ratio(test_rows, valid_rows, "memory_bytes")

    throughput = line(
        "Throughput comparison",
        test_ops,  test_throughput,  test_name,  TEST_COLOR,
        valid_ops, valid_throughput, valid_name, VALID_COLOR,
        "ops/sec",
    )

    memory = line(
        "Memory comparison",
        test_ops,  test_memory,  test_name,  TEST_COLOR,
        valid_ops, valid_memory, valid_name, VALID_COLOR,
        "bytes",
    )

    speedup = ratio_line(
        "Throughput ratio",
        speedup_x,
        speedup_y,
        "test / valid",
        f"{test_name} / {valid_name}",
    )

    throughput.write_html(str(out_dir / "throughput.html"), include_plotlyjs="cdn")
    memory.write_html(str(out_dir / "memory.html"),         include_plotlyjs="cdn")
    speedup.write_html(str(out_dir / "speedup.html"),       include_plotlyjs="cdn")

    has_memory_ratio = len(memory_ratio_x) > 0

    if has_memory_ratio:
        memory_ratio = ratio_line(
            "Memory ratio",
            memory_ratio_x,
            memory_ratio_y,
            "test / valid",
            f"{test_name} / {valid_name}",
        )
        
        memory_ratio.write_html(str(out_dir / "memory_ratio.html"), include_plotlyjs="cdn")

    rows = 4 if has_memory_ratio else 3

    titles = [
        "Throughput comparison",
        "Memory comparison",
        "Throughput ratio",
    ]

    if has_memory_ratio:
        titles.append("Memory ratio")

    summary = make_subplots(
        rows=rows,
        cols=1,
        shared_xaxes=False,
        subplot_titles=titles,
    )

    summary.add_trace(
        go.Scatter(
            x=test_ops,
            y=test_throughput,
            mode="lines",
            name=f"{test_name}: ops/sec",
            line=dict(color=TEST_COLOR),
        ),
        row=1, col=1,
    )

    summary.add_trace(
        go.Scatter(
            x=valid_ops,
            y=valid_throughput,
            mode="lines",
            name=f"{valid_name}: ops/sec",
            line=dict(color=VALID_COLOR),
        ),
        row=1, col=1,
    )

    summary.add_trace(
        go.Scatter(
            x=test_ops,
            y=test_memory,
            mode="lines",
            name=f"{test_name}: memory",
            line=dict(color=TEST_COLOR),
            showlegend=False,
        ),
        row=2, col=1,
    )

    summary.add_trace(
        go.Scatter(
            x=valid_ops,
            y=valid_memory,
            mode="lines",
            name=f"{valid_name}: memory",
            line=dict(color=VALID_COLOR),
            showlegend=False,
        ),
        row=2, col=1,
    )

    summary.add_trace(
        go.Scatter(
            x=speedup_x,
            y=speedup_y,
            mode="lines",
            name=f"{test_name} / {valid_name}: throughput ratio",
        ),
        row=3, col=1,
    )

    summary.add_hline(y=1.0, line_dash="dash", row=3, col=1)

    if has_memory_ratio:
        summary.add_trace(
            go.Scatter(
                x=memory_ratio_x,
                y=memory_ratio_y,
                mode="lines",
                name=f"{test_name} / {valid_name}: memory ratio",
            ),
            row=4, col=1,
        )

        summary.add_hline(y=1.0, line_dash="dash", row=4, col=1)

    summary.update_layout(
        title=f"Comparison: {test_name} vs {valid_name}",
        template="plotly_white",
        hovermode="x unified",
        height=1300 if has_memory_ratio else 1000,
        showlegend=True,
    )

    summary.update_yaxes(title_text="ops/sec", row=1, col=1)
    summary.update_yaxes(title_text="bytes",   row=2, col=1)
    summary.update_yaxes(title_text="ratio",   row=3, col=1)

    if has_memory_ratio:
        summary.update_yaxes(title_text="ratio", row=4, col=1)

    summary.write_html(str(out_dir / "summary.html"), include_plotlyjs="cdn")

    print(f"written: {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
