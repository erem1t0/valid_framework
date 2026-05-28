#!/usr/bin/env python3
"""
Visualization for valid_framework benchmark logs with Memory tracking support.

Usage:
    python plot_logs_v2.py single   log.json
    python plot_logs_v2.py compare  test.json valid.json [--test-name X --valid-name Y]
    python plot_logs_v2.py overlay  ./logs/
    python plot_logs_v2.py batch    ./logs/
    python plot_logs_v2.py summary  ./logs/

Common options:
    --smooth N        moving average window
    --log-y           logarithmic Y axis
    --ymin / --ymax   fix Y axis range
    --normalize       show relative to first chunk (ratio)
"""

import json
import math
import argparse
from pathlib import Path
from collections import defaultdict

import numpy as np
import plotly.graph_objects as go
from plotly.subplots import make_subplots


def load_log(path: str) -> dict:
    with open(path) as f:
        data = json.load(f)
    data["_path"] = str(path)
    return data


def scan_dir(directory: str) -> dict:
    files = sorted(Path(directory).glob("*.json"))
    pairs = defaultdict(dict)
    unmatched = []

    for f in files:
        stem = f.stem
        if stem.endswith("_test"):
            pairs[stem.rsplit("_test", 1)[0]]["test"] = load_log(str(f))
        elif stem.endswith("_valid"):
            pairs[stem.rsplit("_valid", 1)[0]]["valid"] = load_log(str(f))
        else:
            unmatched.append(f)

    for f in unmatched:
        log = load_log(str(f))
        role = log.get("metadata", {}).get("type", "unknown")
        pairs[f.stem][role] = log

    return dict(pairs)


def load_all(directory: str) -> list:
    files = sorted(Path(directory).glob("*.json"))
    return [load_log(str(f)) for f in files]


def series(log: dict, normalize: bool = False) -> dict:
    ts = log.get("timeseries", [])
    ops_sec = [p["ops_per_sec"] for p in ts]

    if normalize and ops_sec:
        base = ops_sec[0] if ops_sec[0] > 0 else 1
        ops_sec = [v / base for v in ops_sec]

    return {
        "op":      [p["op"]            for p in ts],
        "cum_ms":  [p["cumulative_ms"] for p in ts],
        "win_ms":  [p["chunk_ms"]      for p in ts],
        "ops_sec": ops_sec,
        "memory":  [p.get("memory_bytes", 0) for p in ts],
    }


def get_meta(log: dict, key: str, default: str = "?") -> str:
    return log.get("metadata", {}).get(key, default)


def get_container_name(log: dict, fallback: str = "") -> str:
    name = get_meta(log, "container_name", "")
    if name:
        return name
    name = get_meta(log, "type", "")
    if name:
        return name
    if fallback:
        return fallback
    return Path(log.get("_path", "unknown")).stem


def make_label(log: dict, name_override: str = "") -> str:
    return name_override if name_override else get_container_name(log)


def moving_avg(vals: list, w: int) -> list:
    if w <= 1 or not vals:
        return list(vals)
    cs = np.cumsum(vals, dtype=float)
    out = np.empty(len(vals))
    out[:w] = cs[:w] / np.arange(1, w + 1)
    out[w:] = (cs[w:] - cs[:-w]) / w
    return out.tolist()


def global_y_range(logs: list, normalize: bool = False, margin: float = 0.05):
    all_vals = []
    for log in logs:
        s = series(log, normalize=normalize)
        all_vals.extend(s["ops_sec"])
    if not all_vals:
        return None, None
    lo, hi = min(all_vals), max(all_vals)
    pad = (hi - lo) * margin if hi > lo else max(hi * margin, 1)
    return max(0, lo - pad), hi + pad


COLORS = {"test": "#636EFA", "valid": "#00CC96"}

TEST_COLORS = ["#636EFA", "#AB63FA", "#19D3F3", "#8E44AD"]
VALID_COLORS = ["#EF553B", "#FFA15A", "#FECB52", "#FF6692"]
OTHER_COLORS = ["#00CC96", "#B6E880", "#2ca02c", "#54a24b"]

ROLE_PALETTES = {
    "test": TEST_COLORS,
    "valid": VALID_COLORS,
}


def role_color(role: str) -> str:
    return COLORS.get(role, "#AB63FA")


def plotly_mode(style: str) -> str:
    return {
        "dots": "markers",
        "lines": "lines",
        "lines+markers": "lines+markers",
    }.get(style, "lines+markers")


def apply_y_axis(fig, args, logs=None, row=1, col=1, is_subplot=False):
    if args.log_y:
        if is_subplot:
            fig.update_yaxes(type="log", row=row, col=col)
        else:
            fig.update_layout(yaxis_type="log")

    lo, hi = args.ymin, args.ymax
    if lo is None and hi is None and logs and len(logs) > 1:
        lo, hi = global_y_range(logs, normalize=getattr(args, "normalize", False))

    if lo is not None or hi is not None:
        if lo is None:
            lo = 0
        if hi is None:
            hi = lo * 10 if lo > 0 else 1000
        if args.log_y:
            lo = max(lo, 1)
            actual = [math.log10(lo), math.log10(hi)]
        else:
            actual = [lo, hi]
        if is_subplot:
            fig.update_yaxes(range=actual, row=row, col=col)
        else:
            fig.update_layout(yaxis_range=actual)


def add_throughput(fig, log: dict, name: str, color: str,
                   style: str, smooth: int, normalize: bool = False,
                   row=None, col=None, showlegend=True, visible=True,
                   legendgroup: str = None):
    s = series(log, normalize=normalize)
    mode = plotly_mode(style)
    has_smooth = smooth > 1
    lg_name = legendgroup or name

    kwargs = {}
    if row is not None and col is not None:
        kwargs["row"], kwargs["col"] = row, col

    fig.add_trace(go.Scatter(
        x=s["op"], y=s["ops_sec"],
        mode=mode, name=name,
        marker=dict(size=3, color=color),
        line=dict(width=1.2, color=color),
        opacity=0.35 if has_smooth else 1.0,
        showlegend=showlegend,
        legendgroup=lg_name,
        visible=visible,
    ), **kwargs)

    if has_smooth:
        fig.add_trace(go.Scatter(
            x=s["op"], y=moving_avg(s["ops_sec"], smooth),
            mode="lines", name=f"{name} (avg {smooth})",
            line=dict(width=2.5, color=color),
            showlegend=False,
            legendgroup=lg_name,
            visible=visible,
        ), **kwargs)


def add_cumulative(fig, log: dict, name: str, color: str,
                   row=None, col=None, visible=True,
                   legendgroup: str = None):
    s = series(log)
    lg_name = legendgroup or name
    kwargs = {}
    if row is not None and col is not None:
        kwargs["row"], kwargs["col"] = row, col

    fig.add_trace(go.Scatter(
        x=s["op"], y=s["cum_ms"],
        mode="lines", name=f"{name} cumul.",
        line=dict(width=2, color=color),
        showlegend=False,
        legendgroup=lg_name,
        visible=visible,
    ), **kwargs)


def add_memory(fig, log: dict, name: str, color: str,
               row=None, col=None, visible=True,
               legendgroup: str = None):
    s = series(log)
    lg_name = legendgroup or name
    kwargs = {}
    if row is not None and col is not None:
        kwargs["row"], kwargs["col"] = row, col

    # Convert bytes to megabytes
    mem_mb = [m / (1024 * 1024) for m in s["memory"]]

    fig.add_trace(go.Scatter(
        x=s["op"], y=mem_mb,
        mode="lines", name=f"{name} memory",
        line=dict(width=2, color=color, dash="dash"),
        showlegend=False,
        legendgroup=lg_name,
        visible=visible,
    ), **kwargs)


def throughput_title(args) -> str:
    return "Throughput (relative to first chunk)" if args.normalize else "Throughput (ops/sec)"


def throughput_yaxis(args) -> str:
    return "ratio" if args.normalize else "ops/sec"


def cmd_single(args):
    log = load_log(args.file)
    label = make_label(log)
    color = role_color(get_meta(log, "type", ""))

    fig = make_subplots(
        rows=3, cols=1, shared_xaxes=True,
        subplot_titles=(throughput_title(args), "Cumulative time (ms)", "Memory usage (MB)"),
        row_heights=[0.45, 0.25, 0.30], vertical_spacing=0.06,
    )

    add_throughput(fig, log, label, color, args.style, args.smooth,
                   normalize=args.normalize, row=1, col=1)
    add_cumulative(fig, log, label, color, row=2, col=1)
    add_memory(fig, log, label, color, row=3, col=1)
    apply_y_axis(fig, args, logs=[log], row=1, col=1, is_subplot=True)

    fig.update_layout(
        title=f"Benchmark: {label}", title_font_size=22,
        xaxis3_title="Operation #",
        yaxis_title=throughput_yaxis(args), yaxis2_title="ms", yaxis3_title="MB",
        height=850, template="plotly_white", font=dict(size=15),
        legend=dict(font=dict(size=14)),
    )
    fig.update_xaxes(title_font_size=16)
    fig.update_yaxes(title_font_size=16)
    fig.write_html(args.output, include_plotlyjs="cdn")
    print(f"Saved → {args.output}")


def cmd_compare(args):
    tlog = load_log(args.test_file)
    vlog = load_log(args.valid_file)
    t_label = make_label(tlog, args.test_name or "")
    v_label = make_label(vlog, args.valid_name or "")

    fig = make_subplots(
        rows=3, cols=1, shared_xaxes=True,
        subplot_titles=(f"{throughput_title(args)}: {t_label} vs {v_label}", "Cumulative time (ms)", "Memory usage (MB)"),
        row_heights=[0.45, 0.25, 0.30], vertical_spacing=0.06,
    )

    add_throughput(fig, tlog, t_label, COLORS["test"], args.style, args.smooth,
                   normalize=args.normalize, row=1, col=1)
    add_throughput(fig, vlog, v_label, COLORS["valid"], args.style, args.smooth,
                   normalize=args.normalize, row=1, col=1)
    add_cumulative(fig, tlog, t_label, COLORS["test"], row=2, col=1)
    add_cumulative(fig, vlog, v_label, COLORS["valid"], row=2, col=1)
    add_memory(fig, tlog, t_label, COLORS["test"], row=3, col=1)
    add_memory(fig, vlog, v_label, COLORS["valid"], row=3, col=1)
    apply_y_axis(fig, args, logs=[tlog, vlog], row=1, col=1, is_subplot=True)

    fig.update_layout(
        title=f"{t_label} vs {v_label}", title_font_size=22,
        xaxis3_title="Operation #", yaxis_title=throughput_yaxis(args), yaxis2_title="ms", yaxis3_title="MB",
        height=850, template="plotly_white", font=dict(size=15),
        legend=dict(font=dict(size=14)),
    )
    fig.update_xaxes(title_font_size=16)
    fig.update_yaxes(title_font_size=16)
    fig.write_html(args.output, include_plotlyjs="cdn")
    print(f"Saved → {args.output}")


def cmd_overlay(args):
    logs = load_all(args.directory)
    if not logs:
        print(f"No logs in {args.directory}")
        return

    fig = make_subplots(
        rows=3, cols=1, shared_xaxes=True,
        subplot_titles=(throughput_title(args), "Cumulative time (ms)", "Memory usage (MB)"),
        row_heights=[0.45, 0.25, 0.30], vertical_spacing=0.06,
    )

    role_counters = defaultdict(int)
    seen_legend_groups = set()

    for log in logs:
        legend_group_name = make_label(log)
        is_first_in_group = legend_group_name not in seen_legend_groups
        if is_first_in_group:
            seen_legend_groups.add(legend_group_name)

        role = get_meta(log, "type", "unknown")
        palette = ROLE_PALETTES.get(role, OTHER_COLORS)
        color_idx = role_counters[role]
        color = palette[color_idx % len(palette)]
        role_counters[role] += 1

        add_throughput(fig, log, legend_group_name, color,
                       args.style, args.smooth, normalize=args.normalize,
                       row=1, col=1,
                       legendgroup=legend_group_name,
                       showlegend=is_first_in_group)

        add_cumulative(fig, log, legend_group_name, color,
                       row=2, col=1,
                       legendgroup=legend_group_name)
                       
        add_memory(fig, log, legend_group_name, color,
                   row=3, col=1,
                   legendgroup=legend_group_name)

    apply_y_axis(fig, args, logs=logs, row=1, col=1, is_subplot=True)

    fig.update_layout(
        title="Overlay Comparison", title_font_size=22,
        xaxis3_title="Operation #", yaxis_title=throughput_yaxis(args), yaxis2_title="ms", yaxis3_title="MB",
        height=850, template="plotly_white", font=dict(size=15),
        legend=dict(font=dict(size=14)),
    )
    fig.update_xaxes(title_font_size=16)
    fig.update_yaxes(title_font_size=16)
    fig.write_html(args.output, include_plotlyjs="cdn")
    print(f"Saved → {args.output}")


def cmd_batch(args):
    scenarios = scan_dir(args.directory)
    if not scenarios:
        print(f"No logs in {args.directory}")
        return

    all_logs = []
    for pair in scenarios.values():
        all_logs.extend(pair.values())

    names = list(scenarios.keys())
    fig = make_subplots(
        rows=3, cols=1, shared_xaxes=True,
        subplot_titles=(throughput_title(args), "Cumulative time (ms)", "Memory usage (MB)"),
        row_heights=[0.45, 0.25, 0.30], vertical_spacing=0.06,
    )
    trace_ranges = []

    for name in names:
        pair = scenarios[name]
        start = len(fig.data)
        for role in ("test", "valid"):
            if role in pair:
                label = f"{name} — {make_label(pair[role])}"
                add_throughput(fig, pair[role], label, role_color(role), args.style,
                               args.smooth, normalize=args.normalize, row=1, col=1, visible=False)
                add_cumulative(fig, pair[role], label, role_color(role), row=2, col=1, visible=False)
                add_memory(fig, pair[role], label, role_color(role), row=3, col=1, visible=False)
        trace_ranges.append((start, len(fig.data) - start))

    if trace_ranges:
        s, c = trace_ranges[0]
        for i in range(s, s + c):
            fig.data[i].visible = True

    buttons = [
        dict(
            label=name,
            method="update",
            args=[{"visible": [i in range(*trace_ranges[idx]) for i in range(len(fig.data))]}],
        )
        for idx, name in enumerate(names)
    ]

    fig.update_layout(
        updatemenus=[dict(type="dropdown", x=0.0, y=1.15, buttons=buttons, font=dict(size=14))],
        title="Batch Results (shared Y scale)", title_font_size=22,
        xaxis3_title="Operation #", yaxis_title=throughput_yaxis(args), yaxis2_title="ms", yaxis3_title="MB",
        height=850, template="plotly_white", font=dict(size=15),
        legend=dict(font=dict(size=14)),
    )
    fig.update_xaxes(title_font_size=16)
    fig.update_yaxes(title_font_size=16)
    apply_y_axis(fig, args, logs=all_logs, row=1, col=1, is_subplot=True)
    fig.write_html(args.output, include_plotlyjs="cdn")
    print(f"Saved → {args.output}")


def cmd_summary(args):
    scenarios = scan_dir(args.directory)
    if not scenarios:
        print(f"No logs in {args.directory}")
        return

    rows = []
    for name, pair in scenarios.items():
        for role in ("test", "valid"):
            if role in pair:
                sm = pair[role].get("summary", {})
                ts = pair[role].get("timeseries", [])
                max_mem_bytes = max([p.get("memory_bytes", 0) for p in ts]) if ts else 0
                rows.append(dict(
                    scenario=name,
                    role=role,
                    container=get_container_name(pair[role]),
                    total_ops=sm.get("total_ops", 0),
                    total_ms=sm.get("total_time_ms", 0),
                    avg_ops=sm.get("avg_ops_per_sec", 0),
                    failed=sm.get("failed_ops", 0),
                    peak_mem_mb=max_mem_bytes / (1024 * 1024),
                ))

    fig = make_subplots(
        rows=2,
        cols=1,
        specs=[[{"type": "table"}], [{"type": "bar"}]],
        row_heights=[0.42, 0.58],
        vertical_spacing=0.08,
    )

    fig.add_trace(go.Table(
        header=dict(
            values=["Scenario", "Container", "Total ops", "Time ms", "Avg ops/sec", "Peak Mem (MB)", "Failed"],
            fill_color="#636EFA",
            font=dict(color="white", size=14),
            align="center",
        ),
        cells=dict(
            values=[[r[k] for r in rows] for k in ["scenario", "container"]] +
                   [[f'{r[k]:,}' for r in rows] for k in ["total_ops"]] +
                   [[f'{r[k]:.1f}' for r in rows] for k in ["total_ms"]] +
                   [[f'{r[k]:,}' for r in rows] for k in ["avg_ops"]] +
                   [[f'{r[k]:.2f}' for r in rows] for k in ["peak_mem_mb"]] +
                   [[r[k] for r in rows] for k in ["failed"]],
            fill_color=[["#eef" if r["role"] == "test" else "#efe" for r in rows]] * 7,
            font=dict(size=13),
            align="center",
        ),
    ), row=1, col=1)

    containers = []
    for r in rows:
        if r["container"] not in containers:
            containers.append(r["container"])

    scenarios_by_container = {}
    for container in containers:
        container_scenarios = []
        for r in rows:
            if r["container"] == container and r["scenario"] not in container_scenarios:
                container_scenarios.append(r["scenario"])
        scenarios_by_container[container] = container_scenarios

    x_pos = {}
    tickvals = []
    ticktext = []
    cursor = 0.0
    group_gap = 1.5

    for container in containers:
        container_scenarios = scenarios_by_container[container]
        start = cursor

        for i, scenario in enumerate(container_scenarios):
            x_pos[(container, scenario)] = start + i

        end = start + max(len(container_scenarios) - 1, 0)
        tickvals.append((start + end) / 2)
        ticktext.append(container)
        cursor = end + group_gap + 1

    for role in ("test", "valid"):
        rr = [r for r in rows if r["role"] == role]
        if rr:
            fig.add_trace(go.Bar(
                x=[x_pos[(r["container"], r["scenario"])] for r in rr],
                y=[r["avg_ops"] for r in rr],
                name=role,
                marker_color=role_color(role),
                customdata=[[r["container"], r["scenario"]] for r in rr],
                hovertemplate=(
                    "Container: %{customdata[0]}<br>"
                    "Scenario: %{customdata[1]}<br>"
                    "Avg ops/sec: %{y:,}<extra>%{fullData.name}</extra>"
                ),
            ), row=2, col=1)

    fig.update_layout(
        title="Benchmark Summary",
        barmode="group",
        title_font_size=22,
        height=820,
        margin=dict(l=60, r=30, t=70, b=80),
        template="plotly_white",
        font=dict(size=14),
        legend=dict(font=dict(size=14)),
    )

    fig.update_yaxes(
        title_text="Avg ops/sec",
        row=2,
        col=1,
        title_font_size=16,
        tickfont=dict(size=13),
    )

    fig.update_xaxes(
        tickmode="array",
        tickvals=tickvals,
        ticktext=ticktext,
        tickfont=dict(size=13),
        title_font_size=16,
        row=2,
        col=1,
    )

    fig.write_html(args.output, include_plotlyjs="cdn")
    print(f"Saved → {args.output}")


STYLE_CHOICES = ["dots", "lines", "lines+markers"]


def add_common_args(p):
    p.add_argument("--style", default="lines+markers", choices=STYLE_CHOICES)
    p.add_argument("--smooth", type=int, default=0, help="Moving average window size")
    p.add_argument("--log-y", action="store_true", help="Logarithmic Y axis")
    p.add_argument("--ymin", type=float, help="Fix Y axis minimum")
    p.add_argument("--ymax", type=float, help="Fix Y axis maximum")
    p.add_argument("--normalize", action="store_true", help="Show relative to first chunk (ratio)")


def main():
    ap = argparse.ArgumentParser(description="valid_framework log visualizer")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("single", help="Single log file")
    p.add_argument("file")
    add_common_args(p)
    p.add_argument("--output", default="plot.html")

    p = sub.add_parser("compare", help="Two logs side by side")
    p.add_argument("test_file")
    p.add_argument("valid_file")
    p.add_argument("--test-name", default="")
    p.add_argument("--valid-name", default="")
    add_common_args(p)
    p.add_argument("--output", default="compare.html")

    p = sub.add_parser("overlay", help="All logs on one chart")
    p.add_argument("directory")
    add_common_args(p)
    p.add_argument("--output", default="overlay.html")

    p = sub.add_parser("batch", help="Dropdown per scenario")
    p.add_argument("directory")
    add_common_args(p)
    p.add_argument("--output", default="batch.html")

    p = sub.add_parser("summary", help="Table + bar chart")
    p.add_argument("directory")
    p.add_argument("--output", default="summary.html")

    args = ap.parse_args()

    if args.cmd == "summary":
        args.log_y, args.ymin, args.ymax, args.style, args.smooth, args.normalize = (
            False, None, None, "lines+markers", 0, False
        )

    {
        "single": cmd_single,
        "compare": cmd_compare,
        "overlay": cmd_overlay,
        "batch": cmd_batch,
        "summary": cmd_summary,
    }[args.cmd](args)


if __name__ == "__main__":
    main()
