"""Summarize actual guest wall-clock measurements; never synthesize samples."""
import argparse
import json
import re
import statistics
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("logs", nargs="+", type=Path)
args = parser.parse_args()
reports = []
for path in args.logs:
    text = path.read_text(errors="replace")
    rows = [dict(zip(("cycle", "create_ns", "batch32_ns", "destroy_ns"), map(int, row)))
            for row in re.findall(r"ultra-bench: cycle=(\d+) create_ns=(\d+) 32_rpc_ns=(\d+) destroy_ns=(\d+)", text)]
    if [row["cycle"] for row in rows] != list(range(8)):
        parser.error(f"{path}: expected eight complete, ordered lifecycle samples")
    if not re.search(r"ultra-space:[^\r\n]* PASS", text):
        parser.error(f"{path}: no successful Ultra-Space validation marker")
    summary = {key: {"min": min(row[key] for row in rows),
                     "median": statistics.median(row[key] for row in rows),
                     "max": max(row[key] for row in rows)}
               for key in ("create_ns", "batch32_ns", "destroy_ns")}
    summary["per_rpc_ns"] = {key: value / 32 for key, value in summary["batch32_ns"].items()}
    reports.append({"log": str(path), "metric": "guest monotonic elapsed nanoseconds",
                    "samples": rows, "summary": summary})
print(json.dumps(reports, indent=2))
