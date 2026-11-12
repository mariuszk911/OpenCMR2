#!/usr/bin/env python3
"""Compare two replay traces written by src/diagnostics/replay_trace.cpp.

Prints the first tick where any field differs, the differing fields, and the
ticks before it (default 20). Exit code 1 when the traces differ, 2 when one
is shorter and otherwise equal, 0 when identical.
"""
import argparse
import csv
import sys


def load(path):
    with open(path, newline="") as f:
        rows = list(csv.reader(f))
    return rows[0], [[int(x) for x in r] for r in rows[1:]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--before", type=int, default=20, help="ticks to print before the divergence")
    ap.add_argument("--after", type=int, default=3)
    ap.add_argument("--ignore", default="", help="comma-separated fields to ignore")
    args = ap.parse_args()
    ha, a = load(args.a)
    hb, b = load(args.b)
    if ha != hb:
        print("different headers:\n  %s\n  %s" % (ha, hb))
        return 1
    ignore = set(x for x in args.ignore.split(",") if x)
    cols = [i for i, n in enumerate(ha) if n not in ignore]
    n = min(len(a), len(b))
    first = None
    for t in range(n):
        if any(a[t][c] != b[t][c] for c in cols):
            first = t
            break
    if first is None:
        if len(a) != len(b):
            print("equal for %d ticks; lengths differ: %d vs %d" % (n, len(a), len(b)))
            return 2
        print("identical: %d ticks" % n)
        return 0
    diff = [ha[c] for c in cols if a[first][c] != b[first][c]]
    print("first difference at tick %d (row %d): %s" % (a[first][0], first, ", ".join(diff)))
    lo = max(0, first - args.before)
    hi = min(n, first + args.after + 1)
    show = [i for i, name in enumerate(ha)]
    width = max(len(x) for x in ha) + 1
    for t in range(lo, hi):
        mark = ">>" if t == first else "  "
        print("%s row %d" % (mark, t))
        for c in show:
            va, vb = a[t][c], b[t][c]
            flag = " *" if va != vb else ""
            if t == first or flag or c < 6:
                print("     %-*s %12d %12d%s" % (width, ha[c], va, vb, flag))
    return 1


if __name__ == "__main__":
    sys.exit(main())
