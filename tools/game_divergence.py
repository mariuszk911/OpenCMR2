#!/usr/bin/env python3
"""Measure how far game/ has drifted from upstream, and keep the manifest honest.

game/ should follow upstream (fork-first, AGENTS.md rules 1-2). Every line that
differs is a future merge conflict. This compares game/ with the upstream commit
named in tools/game_divergence_base (the last one merged; update it with each
sync) and reports, per file, the changed lines and the `PORT: AMIGA` markers.

    tools/game_divergence.py            report
    tools/game_divergence.py --check    exit 1 when the divergence grew or is unlisted
    tools/game_divergence.py --update-budget   record the current numbers as the limit

--check fails when:
  * a game/ file differs from upstream but is not named in the divergence
    manifest (docs/AMIGA_PORT.md), or
  * the number of differing files or changed lines exceeds
    tools/game_divergence_budget.json (a ratchet: lower it as hooks move into
    port/, never raise it without approval).

It needs the base commit in the repository history (a full clone).
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE_FILE = ROOT / "tools" / "game_divergence_base"
BUDGET_FILE = ROOT / "tools" / "game_divergence_budget.json"
MANIFEST = ROOT / "docs" / "AMIGA_PORT.md"
MARKER = "PORT: AMIGA"


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True, text=True,
                          capture_output=True).stdout


def base_commit(override):
    ref = override or BASE_FILE.read_text().split()[0]
    try:
        return git("rev-parse", "--verify", ref + "^{commit}").strip()
    except subprocess.CalledProcessError:
        sys.exit(f"base commit {ref} not found: fetch the full history (git fetch --unshallow)")


def divergence(base):
    """{file: (added, removed, markers)} for game/ files that differ from base."""
    result = {}
    for line in git("diff", "--numstat", base, "--", "game").splitlines():
        added, removed, path = line.split("\t", 2)
        if added == "-":  # binary
            added = removed = 0
        markers = 0
        if (ROOT / path).exists():
            markers = (ROOT / path).read_text(errors="replace").count(MARKER)
        result[path] = (int(added), int(removed), markers)
    return result


def manifest_files():
    """game/ files named in the divergence manifest, with {a,b} braces expanded."""
    text = MANIFEST.read_text()
    start = text.index("## Divergence manifest")
    end = text.find("\n## ", start + 1)
    section = text[start:end if end != -1 else len(text)]
    names = set()
    first_cells = []
    for row in section.splitlines():
        if row.startswith("| `game/"):
            first_cells.append(row.split("|")[1])
    for token in re.findall(r"`(game/[^`]+)`", "\n".join(first_cells)):
        match = re.fullmatch(r"(game/[^{}]*)\{([^{}]+)\}", token)
        if match:
            for part in match.group(2).split(","):
                names.add(f"{match.group(1)}.{part}" if not part.startswith(".") else match.group(1) + part)
        else:
            names.add(token)
    return names


def totals(files):
    return {
        "files": len(files),
        "added": sum(v[0] for v in files.values()),
        "removed": sum(v[1] for v in files.values()),
        "markers": sum(v[2] for v in files.values()),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", help="commit to compare with (default: tools/game_divergence_base)")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--update-budget", action="store_true")
    args = ap.parse_args()

    base = base_commit(args.base)
    files = divergence(base)
    listed = manifest_files()
    total = totals(files)
    total["changed"] = total["added"] + total["removed"]

    print(f"game/ against {base[:10]}: {total['files']} files differ, "
          f"+{total['added']} -{total['removed']} lines, {total['markers']} '{MARKER}' markers")
    print(f"{'changed':>8} {'markers':>8}  file")
    for path, (added, removed, markers) in sorted(files.items(), key=lambda kv: -(kv[1][0] + kv[1][1])):
        flag = "" if path in listed else "  <- not in the manifest"
        print(f"{added + removed:>8} {markers:>8}  {path}{flag}")

    unlisted = sorted(p for p in files if p not in listed)
    stale = sorted(p for p in listed if p not in files and (ROOT / p).exists())
    if stale:
        print("\nIn the manifest but identical to upstream (remove the row):")
        for path in stale:
            print("  " + path)

    if args.update_budget:
        BUDGET_FILE.write_text(json.dumps({"files": total["files"], "changed": total["changed"]}, indent=2) + "\n")
        print(f"\nbudget written to {BUDGET_FILE.relative_to(ROOT)}")
        return 0

    if not args.check:
        return 0

    problems = []
    if unlisted:
        problems.append("game/ files that differ from upstream but are not in the divergence manifest: "
                        + ", ".join(unlisted))
    if BUDGET_FILE.exists():
        budget = json.loads(BUDGET_FILE.read_text())
        if total["files"] > budget["files"]:
            problems.append(f"{total['files']} files differ from upstream, budget is {budget['files']}")
        if total["changed"] > budget["changed"]:
            problems.append(f"{total['changed']} lines differ from upstream, budget is {budget['changed']}")
    if problems:
        print("\nFAIL:")
        for problem in problems:
            print("  " + problem)
        return 1
    print("\nOK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
