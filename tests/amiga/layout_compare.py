#!/usr/bin/env python3
"""Compare a host layout reference with values extracted from the 68k object."""
import argparse
import sys


def read_reference(path):
    names = []
    values = []
    with open(path, "r", encoding="utf-8") as source:
        for line in source:
            line = line.rstrip("\n")
            if not line:
                continue
            name, separator, value = line.partition("\t")
            if not separator:
                raise ValueError("bad reference line: %s" % line)
            names.append(name)
            values.append(int(value))
    return names, values


def read_values(path):
    with open(path, "r", encoding="utf-8") as source:
        return [int(value) for value in source.read().split()]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", required=True)
    parser.add_argument("--probe", required=True)
    parser.add_argument("--label", default="layout")
    parser.add_argument("--skip", type=int, default=0,
                        help="skip this many leading probe values")
    args = parser.parse_args()

    names, reference = read_reference(args.reference)
    probe = read_values(args.probe)[args.skip:]
    if len(probe) != len(reference):
        sys.stderr.write(
            "%s: %d probe values, %d reference values\n"
            % (args.label, len(probe), len(reference)))
        return 2

    mismatches = 0
    for name, expected, actual in zip(names, reference, probe):
        if expected != actual:
            mismatches += 1
            print("DIFF %-40s host=%d 68k=%d" % (name, expected, actual))
            if name.endswith("_align"):
                print("     (alignment differs; shared ABI alignment must be explicit)")
    if mismatches:
        print("FAIL %s: %d of %d values differ" % (args.label, mismatches, len(reference)))
        return 1
    print("PASS %s: %d values match" % (args.label, len(reference)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
