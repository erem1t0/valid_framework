#!/usr/bin/env python3
import json
import shutil
from pathlib import Path


CUTOFF_OPS = 1_000_000


def find_cutoff_time(timeseries, cutoff_ops):
    """
    Возвращает cumulative_ms на момент cutoff_ops.

    Если есть точка op == cutoff_ops, берем ее.
    Если точной точки нет, пробуем линейно интерполировать между соседними точками.
    """
    if not timeseries:
        return 0.0

    # Точная точка
    for row in timeseries:
        if row.get("op") == cutoff_ops:
            return float(row.get("cumulative_ms", 0.0))

    # Если cutoff раньше первой точки
    first = timeseries[0]
    if cutoff_ops < first.get("op", 0):
        op1 = first["op"]
        t1 = float(first["cumulative_ms"])
        return t1 * cutoff_ops / op1

    # Интерполяция между соседними точками
    prev = None
    for row in timeseries:
        op = row.get("op", 0)

        if prev is not None and prev["op"] < cutoff_ops < op:
            op0 = prev["op"]
            op1 = op

            t0 = float(prev["cumulative_ms"])
            t1 = float(row["cumulative_ms"])

            alpha = (cutoff_ops - op0) / (op1 - op0)
            return t0 + alpha * (t1 - t0)

        prev = row

    # Если cutoff после последней точки
    return float(timeseries[-1].get("cumulative_ms", 0.0))


def trim_timeseries(timeseries, cutoff_ops, cutoff_time):
    """
    Удаляет строки с op <= cutoff_ops.
    Для оставшихся строк:
      op := op - cutoff_ops
      cumulative_ms := cumulative_ms - cutoff_time
    """
    result = []

    for row in timeseries:
        old_op = row.get("op")

        if old_op is None:
            continue

        if old_op <= cutoff_ops:
            continue

        new_row = dict(row)
        new_row["op"] = old_op - cutoff_ops

        if "cumulative_ms" in new_row:
            new_row["cumulative_ms"] = round(
                float(new_row["cumulative_ms"]) - cutoff_time,
                6,
            )

        # chunk_ms и ops_per_sec оставляем как есть,
        # потому что они относятся к конкретному чанку измерения.
        result.append(new_row)

    return result


def trim_errors(errors, cutoff_ops):
    """
    Если в errors есть поле op, удаляем ошибки из первых cutoff_ops операций,
    а оставшимся пересчитываем op.
    Если поля op нет, оставляем ошибку как есть.
    """
    if not isinstance(errors, list):
        return errors

    result = []

    for err in errors:
        if not isinstance(err, dict):
            result.append(err)
            continue

        if "op" not in err:
            result.append(err)
            continue

        old_op = err["op"]

        if old_op <= cutoff_ops:
            continue

        new_err = dict(err)
        new_err["op"] = old_op - cutoff_ops
        result.append(new_err)

    return result


def update_summary(data, old_total_ops, cutoff_ops, cutoff_time, new_timeseries):
    summary = data.setdefault("summary", {})

    new_total_ops = max(0, old_total_ops - cutoff_ops)

    if new_timeseries:
        new_total_time_ms = float(new_timeseries[-1].get("cumulative_ms", 0.0))
    else:
        old_total_time_ms = float(summary.get("total_time_ms", 0.0))
        new_total_time_ms = max(0.0, old_total_time_ms - cutoff_time)

    summary["total_ops"] = new_total_ops
    summary["total_time_ms"] = round(new_total_time_ms, 6)

    if new_total_time_ms > 0:
        summary["avg_ops_per_sec"] = int(round(new_total_ops / new_total_time_ms * 1000))
    else:
        summary["avg_ops_per_sec"] = 0

    # failed_ops оставляем как есть, если не умеем точно пересчитывать.
    # Если errors имеют поле op, можно поставить len(data["errors"]).
    if isinstance(data.get("errors"), list):
        errors_with_op = [
            e for e in data["errors"]
            if isinstance(e, dict) and "op" in e
        ]
        if errors_with_op:
            summary["failed_ops"] = len(data["errors"])

    return summary


def trim_file(path: Path, cutoff_ops: int, make_backup: bool = True):
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)

    timeseries = data.get("timeseries", [])

    if not isinstance(timeseries, list):
        print(f"[skip] {path.name}: no valid timeseries")
        return

    old_total_ops = int(data.get("summary", {}).get("total_ops", 0))

    if old_total_ops and old_total_ops <= cutoff_ops:
        print(f"[skip] {path.name}: total_ops <= cutoff")
        return

    cutoff_time = find_cutoff_time(timeseries, cutoff_ops)
    new_timeseries = trim_timeseries(timeseries, cutoff_ops, cutoff_time)

    data["timeseries"] = new_timeseries

    if "errors" in data:
        data["errors"] = trim_errors(data["errors"], cutoff_ops)

    update_summary(
        data=data,
        old_total_ops=old_total_ops,
        cutoff_ops=cutoff_ops,
        cutoff_time=cutoff_time,
        new_timeseries=new_timeseries,
    )

    data.setdefault("metadata", {})
    data["metadata"]["trimmed_first_ops"] = cutoff_ops

    if make_backup:
        backup_path = path.with_suffix(path.suffix + ".bak")
        shutil.copy2(path, backup_path)

    with path.open("w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=4)

    print(f"[ok] {path.name}: trimmed first {cutoff_ops} ops")


def main():
    json_files = sorted(
        p for p in Path(".").glob("*.json")
        if not p.name.endswith(".bak")
    )

    if not json_files:
        print("No .json files found in current directory")
        return

    for path in json_files:
        trim_file(path, CUTOFF_OPS, make_backup=True)


if __name__ == "__main__":
    main()
