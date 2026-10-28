#!/usr/bin/env python3
"""Source guard for the approved big-endian hooks in the divergence manifest.

An upstream sync can silently undo a small game/ edit even while every runtime
test still passes on a little-endian host. These checks fail the host suite if
the weather-map count, the packed menu colours or the weather-select value list
are reverted to their x86-only forms.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
FAILURES = []


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


def require(condition, message):
    if not condition:
        FAILURES.append(message)


def require_rgba(source, name, path):
    match = re.search(r"\b%s\s*(?:\[[^\]]*\])?\s*=\s*" % re.escape(name), source)
    require(match is not None, "%s: missing %s" % (path, name))
    if match is not None:
        window = source[match.end():match.end() + 400]
        require("OPENCMR2_RGBA32" in window,
                "%s: %s is not initialised with OPENCMR2_RGBA32" % (path, name))


def main():
    game_info = read("game/GameInfo.cpp")
    rally_data = read("game/RallyData.cpp")

    for path, source in (("game/GameInfo.cpp", game_info),
                         ("game/RallyData.cpp", rally_data)):
        require('#include "port/colour.h"' in source,
                "%s: missing port/colour.h" % path)

    require("*(BYTE *)&g_weatherMaps.count" not in rally_data,
            "game/RallyData.cpp: weather-map count is written through a BYTE pointer")
    require(re.search(r"g_weatherMaps\.count\s*=\s*4\s*;", rally_data) is not None,
            "game/RallyData.cpp: weather-map count is not assigned directly")
    require("*(int *)&g_mapSrcRect[" not in rally_data,
            "game/RallyData.cpp: g_mapSrcRect is still copied through an int pointer")
    require("pSplits = (short *)RallyData_GetDistinctValueList" not in rally_data,
            "game/RallyData.cpp: distinct value list is read through a short pointer")
    require("pSplits = (int *)RallyData_GetDistinctValueList" in rally_data,
            "game/RallyData.cpp: distinct value list is not read as int[]")

    for name in ("g_optionTextColours", "g_optionTextBrightColours",
                 "g_unk0x005297ac", "g_unk0x00527378", "g_highlightRowColour"):
        require_rgba(game_info, name, "game/GameInfo.cpp")

    for name in ("g_unk0x005270ec", "g_unk0x005270f0",
                 "g_unk0x005270fc", "g_unk0x00527100"):
        require_rgba(rally_data, name, "game/RallyData.cpp")

    if FAILURES:
        for failure in FAILURES:
            print("portability_source_guard: " + failure, file=sys.stderr)
        return 1
    print("portability_source_guard: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
