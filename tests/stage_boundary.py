#!/usr/bin/env python3
"""Exercise the residue-3, two-panel weather preview path on the host.

Rally stage 3 in a two-panel mode makes RallyData_SetupWeatherTextureEntries
compute the old maps[4] index for the second preview panel. The probe selects
Single Rally (two panels), forces stage 3 before the options screen, and
expands the preview map so the panel's map texture is used. Pre-fix builds
segfault here; the fixed build completes. Run it under linux-debug to check
the boundary under ASan/UBSan.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

SUMMARY = re.compile(r"probe: \d+ ticks, \d+ driving ticks, .* complete\s*$", re.MULTILINE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--ticks", type=int, default=700)
    args = parser.parse_args()

    if not args.probe.is_file():
        raise SystemExit(f"probe not found: {args.probe}")
    if not args.data.is_dir():
        raise SystemExit(f"data directory not found: {args.data}")
    if args.ticks < 126:
        raise SystemExit("use at least 126 ticks; the first 125 are the countdown")

    with tempfile.TemporaryDirectory(prefix="opencmr2-stage-boundary-") as user_dir:
        user_dir = Path(user_dir)
        trace = user_dir / "trace.jsonl"
        log = user_dir / "probe.log"
        env = dict(os.environ,
                   XDG_DATA_HOME=str(user_dir),
                   SDL_VIDEODRIVER="dummy",
                   SDL_AUDIODRIVER="dummy",
                   OPENCMR2_HEADLESS="1",
                   OPENCMR2_PROBE_CURSORS="88:1",
                   OPENCMR2_PROBE_EXPECT_STAGE="3",
                   OPENCMR2_PROBE_FORCE_STAGE="3",
                   OPENCMR2_PROBE_EXPAND_MAP="1")
        try:
            with log.open("w") as output:
                result = subprocess.run(
                    [str(args.probe), str(args.data), str(trace), "60", str(args.ticks)],
                    env=env, stdout=output, stderr=subprocess.STDOUT, timeout=120)
        except subprocess.TimeoutExpired:
            print(log.read_text(), file=sys.stderr)
            raise SystemExit("stage boundary probe timed out")
        text = log.read_text()
        if ("AddressSanitizer" in text or "runtime error:" in text or
                result.returncode != 0 or
                "probe: country=0 stage=3 mode=1 " not in text or
                not SUMMARY.search(text)):
            print(text, file=sys.stderr)
            raise SystemExit(f"stage boundary probe failed ({result.returncode})")

    print(f"stage boundary: OK (stage 3, mode 1, two panels, {args.ticks} ticks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
