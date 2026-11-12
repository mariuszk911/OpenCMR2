#!/usr/bin/env python3
"""Source guard for the approved big-endian hooks in the divergence manifest.

An upstream sync can silently undo a small game/ edit even while every runtime
test still passes on a little-endian host. These checks fail the host suite if
the weather-map count, the packed menu colours, the weather-select value list,
the credits parser call or the credits y table are reverted to their x86-only
forms. It also keeps the option-preview converted-vertex key and colour byte
value-based, bounded by meshCount, and gated on every mesh key being unique.
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
    frontend_screens = read("game/FrontendScreens.cpp")
    stage_objects = read("game/StageObjects.cpp")

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
    require("CreditsText_Parse(" in game_info,
            "game/GameInfo.cpp: credits text is not parsed by the fork-owned helper")
    require("((unsigned short *)FrontendCredits_GetRoleTable())" not in frontend_screens,
            "game/FrontendScreens.cpp: credits y table is read through a short pointer")
    require("(short)((int *)FrontendCredits_GetRoleTable())[i]" in frontend_screens,
            "game/FrontendScreens.cpp: credits y is not read from the int table")

    # The option-preview key and colour byte used the object representation,
    # which is the MSB/alpha byte on 68k. The host cannot exercise that defect.
    require("*(BYTE *)&param3->geometry.nodes[i]->flags" not in game_info,
            "game/GameInfo.cpp: preview node key is read through a BYTE pointer")
    require("(BYTE)(param3->geometry.nodes[i]->flags & 0xff)" in game_info,
            "game/GameInfo.cpp: preview node key is not the low value byte")
    require("nb[2] = *pNormal" not in game_info,
            "game/GameInfo.cpp: preview colour blue is read through the first object byte")
    require("nb[2] = (BYTE)n;" in game_info,
            "game/GameInfo.cpp: preview colour blue is not the low value byte")
    require("for (; j <= param3->meshCount; j++)" not in game_info,
            "game/GameInfo.cpp: preview key search reads one past the converted arrays")
    require("for (; j < param3->meshCount; j++)" in game_info,
            "game/GameInfo.cpp: preview key search is not bounded by meshCount")
    require("param3->verticesAssigned = allAssigned;" in game_info,
            "game/GameInfo.cpp: preview vertex assignment is set without checking every mesh")
    require("match = -2;" in game_info,
            "game/GameInfo.cpp: preview duplicate node keys are not rejected")

    # The car's per-frame light colour used to be read through int pointers from
    # 4-byte RGBA arrays, which is big-endian-unsafe and reads past the array.
    require("(*(int *)(light + 0) & 0xff)" not in stage_objects,
            "game/StageObjects.cpp: car light red is still read through an int pointer")
    require("(*(int *)((BYTE *)ambient + 0) & 0xff)" not in stage_objects,
            "game/StageObjects.cpp: car ambient red is still read through an int pointer")
    require("(int)((BYTE *)&light)[0] << 16" in stage_objects,
            "game/StageObjects.cpp: car light red is not read from the byte view")
    require("(int)((BYTE *)&ambient)[0] * 0x10000" in stage_objects,
            "game/StageObjects.cpp: car ambient red is not read from the byte view")

    # The callback cursor is written through its BYTE member; its DWORD's low
    # byte is that member only on little-endian, so the second player's race
    # slot never advanced on 68k.
    game = read("game/Game.cpp")
    require("records[m_callbackIndex.packed & 0xff]" not in game,
            "game/Game.cpp: callback timers index records through the DWORD's low byte")
    require("records[m_callbackIndex.index]" in game,
            "game/Game.cpp: callback timers do not index records by the BYTE cursor")

    # The rev bar's 16.16 width (0x4b0000) truncated to 0 in a short, so the
    # head-to-head (split-screen) bar never filled.
    hud_dash = read("game/HudDash.cpp")
    bar = hud_dash[hud_dash.find("void Dash_DrawBar("):hud_dash.find("// Gear and speed.")]
    require("short scale;" not in bar,
            "game/HudDash.cpp: Dash_DrawBar keeps its 16.16 scale in a short")
    require("int scale;" in bar,
            "game/HudDash.cpp: Dash_DrawBar scale is not an int")

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
