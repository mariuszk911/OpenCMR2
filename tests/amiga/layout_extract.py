#!/usr/bin/env python3
"""Extract the opencmr2_layout_values .long sequence from a GCC -S listing."""
import re
import sys


def extract(path):
    values = []
    active = False
    with open(path, "r", encoding="utf-8", errors="replace") as source:
        for line in source:
            if not active:
                if re.match(r"^_?opencmr2_layout_values:\s*$", line):
                    active = True
                continue
            match = re.match(r"^\s*\.long\s+(-?\d+)\s*$", line)
            if match:
                values.append(int(match.group(1)))
            elif values:
                break
    return values


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: layout_extract.py <assembly.s>\n")
        return 2
    values = extract(sys.argv[1])
    if not values:
        sys.stderr.write("layout_extract: no opencmr2_layout_values in %s\n" % sys.argv[1])
        return 1
    sys.stdout.write("\n".join(str(value) for value in values) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
