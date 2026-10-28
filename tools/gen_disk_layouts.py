#!/usr/bin/env python3
"""Generate src/port/disk_layouts.h: how game records stored in files map to
native structures.

Some game files hold records that the original loads in place and whose
32-bit offsets it turns into pointers (stage mesh files: scene nodes, meshes,
stage objects, sectors, collision records). On 64-bit targets the structures
holding those pointers are larger, so the port builds native copies. This
script compiles the game headers for i386 (the file layout, as MSVC6 laid it
out) and x86-64, and writes for each record type:

- runs: byte ranges without pointers, copied as they are (their internal
  layout is checked to be the same on both targets);
- pointers: fields that hold a 32-bit offset in the file and a native pointer
  in memory, with both offsets, so the loader can relocate them;
- swaps: scalar fields' disk and native offsets and widths (2, 4 or 8 bytes),
  applied by Disk_Unpack/Disk_Pack on big-endian. Pointers, char arrays,
  padding and bitfields are excluded; overlapping union members are
  deduplicated by offset and size.

    tools/gen_disk_layouts.py [--check]

--check exits 1 if the committed header is out of date. Run it after
syncing game/ when one of the structures changes.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "src/port/disk_layouts.h"
HEADERS = ["SceneNode.h", "Mesh.h", "Sector.h", "MeshCollisionRecord.h", "Car.h", "CarParts.h", "ReplayStream.h",
           "Input.h", "FinRecord.h"]
# Stage mesh file records, then the per-car records the state capture writes
# in the original layout (tests/stage_probe.cpp), then the controller file.
RECORDS = ["SceneNode", "Mesh", "StageObject", "Sector", "MeshCollisionRecord", "Car", "CarPartSet", "ReplayStream",
           "ControllerData", "FinRecord"]
TARGETS = {"disk": "i386-pc-linux-gnu", "native": "x86_64-pc-linux-gnu",
           "windows": "x86_64-pc-windows-msvc"}
LINE = re.compile(r"^\s*(\d+)(?::\d+-\d+)? \|( *)(\S.*)$")


def dump(target, optional=False):
    source = "".join(f'#include "{h}"\n' for h in HEADERS)
    source += "".join(f"int sizeof_{r} = sizeof({r});\n" for r in RECORDS)
    cmd = ["clang++", "-target", target, "-fsyntax-only", "-x", "c++", "-std=gnu++17", "-fms-extensions",
           "-Wno-everything", f"-I{ROOT / 'game'}", f"-I{ROOT / 'src'}", f"-I{ROOT / 'src/port/compat'}",
           "-include", str(ROOT / "src/port/types.h"), "-Xclang", "-fdump-record-layouts", "-"]
    result = subprocess.run(cmd, input=source, capture_output=True, text=True)
    if result.returncode != 0:
        if optional:
            print(f"note: no {target} headers here; its layout is not compared", file=sys.stderr)
            return None
        sys.exit(f"clang ({target}) failed:\n{result.stderr[:2000]}")
    return result.stdout


TYPE_SIZES = {
    "char": 1, "signed char": 1, "unsigned char": 1, "BYTE": 1, "UCHAR": 1,
    "bool": 1, "_Bool": 1,
    "short": 2, "short int": 2, "signed short": 2, "unsigned short": 2,
    "unsigned short int": 2, "WORD": 2, "USHORT": 2,
    "int": 4, "signed int": 4, "unsigned": 4, "unsigned int": 4,
    "long": 4, "long int": 4, "unsigned long": 4, "unsigned long int": 4,
    "DWORD": 4, "LONG": 4, "ULONG": 4, "UINT": 4, "INT32": 4, "UINT32": 4,
    "BOOL": 4, "INT": 4,
    "float": 4, "FLOAT": 4,
    "long long": 8, "unsigned long long": 8, "LONGLONG": 8, "ULONGLONG": 8,
    "double": 8,
}

_ARRAY = re.compile(r"^(.*?)\[(\d+)\](.*)$")


def record_block(text, record):
    return re.search(r"\*\*\* Dumping AST Record Layout\n\s*0 \| (?:struct|class) %s\n(.*?)\n\s*\| \[sizeof=(\d+)"
                     % re.escape(record), text, re.S)


def leaves(text, record):
    """(path, offset, pointer element count or 0, type text) of every leaf."""
    block = record_block(text, record)
    if not block:
        sys.exit(f"no layout for {record}")
    rows = []
    for line in block.group(1).split("\n"):
        m = LINE.match(line)
        if m:
            rows.append((int(m.group(1)), len(m.group(2)) // 2, m.group(3)))
    out, stack = [], []
    for i, (offset, depth, text) in enumerate(rows):
        name = text.split()[-1]
        stack = stack[:depth - 1] + [name]
        is_leaf = i + 1 == len(rows) or rows[i + 1][1] <= depth
        if not is_leaf:
            continue
        kind = text[:len(text) - len(name)].strip()
        count = 0
        if "*" in kind:
            m = re.search(r"\[(\d+)\]$", kind)
            count = int(m.group(1)) if m else 1
        if ":" in text:
            kind = "BITFIELD"
        out.append((".".join(stack), offset, count, kind))
    return out, int(block.group(2))


def strip_type(kind):
    kind = kind.strip()
    for prefix in ("struct ", "class ", "const ", "volatile "):
        if kind.startswith(prefix):
            kind = kind[len(prefix):].strip()
    return kind


def split_array(kind):
    m = _ARRAY.match(kind.strip())
    if not m:
        return kind.strip(), None, ""
    return m.group(1).strip(), int(m.group(2)), m.group(3).strip()


def type_size(text, kind):
    text = text.strip()
    base, count, rest = split_array(kind)
    if count is not None:
        element = (base + rest).strip()
        size = type_size(text, element)
        return None if size is None else count * size
    if "*" in base:
        return None
    base = strip_type(base)
    if base in TYPE_SIZES:
        return TYPE_SIZES[base]
    if base.startswith("enum "):
        return 4
    if base.startswith("union "):
        return None
    block = record_block(text, base)
    if not block:
        return None
    return int(block.group(2))


def scalarize(text, kind, disk_base, native_base):
    """(disk offset, native offset, size) for every scalar in a field type."""
    kind = kind.strip()
    base, count, rest = split_array(kind)
    if count is not None:
        inner = (base + rest).strip()
        element_size = type_size(text, inner)
        if element_size is None:
            sys.exit(f"unsupported array element type {inner}")
        out = []
        for i in range(count):
            out += scalarize(text, inner, disk_base + i * element_size,
                             native_base + i * element_size)
        return out
    if "*" in base or kind == "BITFIELD":
        return []
    base = strip_type(base)
    if base in TYPE_SIZES:
        size = TYPE_SIZES[base]
        return [] if size == 1 else [(disk_base, native_base, size)]
    if base.startswith("enum "):
        return [(disk_base, native_base, 4)]
    if base.startswith("union "):
        return []
    block = record_block(text, base)
    if not block:
        sys.exit(f"unsupported scalar type {kind}")
    element_size = int(block.group(2))
    if element_size == 0:
        return []
    leaves_text, _ = leaves(text, base)
    if any(count for _, _, count, _ in leaves_text):
        sys.exit(f"array/struct {base} contains pointers; extend the generator")
    out = []
    for _, offset, _, inner_kind in leaves_text:
        out += scalarize(text, inner_kind, disk_base + offset, native_base + offset)
    return out


def scalar_swaps(disk, native, dleaves, nleaves):
    swaps = []
    for d, n in zip(dleaves, nleaves):
        path, disk_offset, count, kind = d
        _, native_offset, _, native_kind = n
        if count:
            continue
        if kind == "BITFIELD" or native_kind == "BITFIELD":
            continue
        disk_size = type_size(disk, kind)
        native_size = type_size(native, native_kind)
        if disk_size is not None and native_size is not None and disk_size != native_size:
            sys.exit(f"{path}: scalar size differs between targets")
        swaps += scalarize(disk, kind, disk_offset, native_offset)
    swaps.sort(key=lambda s: (s[0], -s[2], s[1]))
    out = []
    for disk_offset, native_offset, size in swaps:
        contained = False
        for o_disk, o_native, o_size in out:
            if (disk_offset >= o_disk and disk_offset + size <= o_disk + o_size and
                    native_offset >= o_native and native_offset + size <= o_native + o_size):
                contained = True
                break
        if contained:
            continue
        for o_disk, o_native, o_size in out:
            if disk_offset < o_disk + o_size and o_disk < disk_offset + size:
                sys.exit(f"overlapping scalar swaps at 0x{disk_offset:x} and 0x{o_disk:x}")
        out.append((disk_offset, native_offset, size))
    out.sort()
    return out


def layout(disk, native, record):
    dleaves, dsize = leaves(disk, record)
    nleaves, nsize = leaves(native, record)
    if [l[0] for l in dleaves] != [l[0] for l in nleaves]:
        sys.exit(f"{record}: field lists differ between targets")
    # A union that aliases a pointer and a 32-bit scalar is a pointer in
    # memory and a file offset/index on disk: the loader relocates it, so the
    # scalar twin is neither copied nor swapped (FinRecord.sector/pNext).
    pointer_offsets = {l[1] for l in dleaves if l[2]}
    keep = [not (l[2] == 0 and l[1] in pointer_offsets) for l in dleaves]
    dleaves = [l for l, k in zip(dleaves, keep) if k]
    nleaves = [l for l, k in zip(nleaves, keep) if k]
    fields = sorted(((d[1], n[1], d[2], d[3], d[0]) for d, n in zip(dleaves, nleaves)),
                    key=lambda f: (f[0], -f[2]))
    runs, pointers, run = [], [], None

    def close(end32, end64):
        nonlocal run
        if run is not None:
            size = end32 - run[0]
            if run[1] + size > end64:
                sys.exit(f"{record}: run at 0x{run[0]:x} does not fit natively")
            if size > 0:
                runs.append((run[0], run[1], size))
        run = None

    for d32, d64, count, kind, name in fields:
        if count:
            close(d32, d64)
            for k in range(count):
                pointers.append((d32 + 4 * k, d64 + 8 * k, name + (f"[{k}]" if count > 1 else "")))
            continue
        if run is not None and d32 >= run[0] and d64 - d32 == run[1] - run[0]:
            continue
        if run is not None and d32 < run[2]:
            sys.exit(f"{record}: overlapping fields with different layouts at 0x{d32:x}")
        close(d32, d64)
        run = [d32, d64, d32]
    close(dsize, nsize)
    for start, _, size in runs:
        for p in pointers:
            if start <= p[0] < start + size:
                sys.exit(f"{record}: pointer {p[2]} inside a copied run")

    swaps = scalar_swaps(disk, native, dleaves, nleaves)
    for disk_offset, native_offset, size in swaps:
        if not any(start <= disk_offset and disk_offset + size <= start + run_size
                   for start, _, run_size in runs):
            sys.exit(f"{record}: swap 0x{disk_offset:x}/{size} is outside a disk run")
        if not any(native_start <= native_offset and native_offset + size <= native_start + run_size
                   for _, native_start, run_size in runs):
            sys.exit(f"{record}: swap 0x{disk_offset:x}/{size} is outside a native run")
    return dsize, nsize, runs, pointers, swaps


def emit_swaps(out, record, swaps, native):
    out.append(f"static const DiskSwap g_diskSwaps_{record}[] = {{")
    if swaps:
        for disk_offset, native_offset, size in swaps:
            target = native_offset if native else disk_offset
            out.append(f"    {{0x{disk_offset:x}, 0x{target:x}, {size}}},")
    else:
        out.append("    {0, 0, 0},")
    out.append("};")


def generate():
    disk, native = dump(TARGETS["disk"]), dump(TARGETS["native"])
    windows = dump(TARGETS["windows"], optional=True)
    out = ["// Generated by tools/gen_disk_layouts.py from the game headers. Do not edit.",
           "// File records of the stage mesh loader (see that script): byte runs copied",
           "// as they are, pointer fields stored as 32-bit file offsets, and scalar",
           "// swaps applied on big-endian targets.",
           "#ifndef OPENCMR2_PORT_DISK_LAYOUTS_H", "#define OPENCMR2_PORT_DISK_LAYOUTS_H", "",
           '#include "port/disk.h"'] + [f'#include "{h}"' for h in HEADERS] + [""]
    for record in RECORDS:
        dsize, nsize, runs, pointers, swaps = layout(disk, native, record)
        if windows is not None and layout(disk, windows, record) != (dsize, nsize, runs, pointers, swaps):
            sys.exit(f"{record}: Windows x64 lays it out differently; extend the generator")
        out.append(f"// {record}: 0x{dsize:x} bytes in files, 0x{nsize:x} on 64-bit targets.")
        out.append("#if UINTPTR_MAX > 0xffffffffu")
        out.append(f"static const DiskRun g_diskRuns_{record}[] = {{")
        out += [f"    {{0x{a:x}, 0x{b:x}, 0x{c:x}}}," for a, b, c in runs]
        out.append("};")
        out.append(f"static const DiskPointer g_diskPointers_{record}[] = {{")
        out += [f"    {{0x{a:x}, 0x{b:x}}}, // {n}" for a, b, n in pointers]
        out.append("};")
        emit_swaps(out, record, swaps, True)
        out.append(f"static_assert(sizeof({record}) == 0x{nsize:x}, \"run tools/gen_disk_layouts.py\");")
        out.append("#else")
        out.append(f"static const DiskRun g_diskRuns_{record}[] = {{{{0, 0, 0x{dsize:x}}}}};")
        out.append(f"static const DiskPointer g_diskPointers_{record}[] = {{")
        out += [f"    {{0x{a:x}, 0x{a:x}}}, // {n}" for a, b, n in pointers]
        out.append("};")
        emit_swaps(out, record, swaps, False)
        out.append(f"static_assert(sizeof({record}) == 0x{dsize:x}, \"run tools/gen_disk_layouts.py\");")
        out.append("#endif")
        out.append(f"static const DiskLayout g_diskLayout_{record} = {{0x{dsize:x}, sizeof({record}), "
                   f"g_diskRuns_{record}, sizeof(g_diskRuns_{record}) / sizeof(DiskRun), "
                   f"g_diskPointers_{record}, sizeof(g_diskPointers_{record}) / sizeof(DiskPointer), "
                   f"g_diskSwaps_{record}, {len(swaps)}}};")
        for a, b, n in pointers:
            if "[" not in n:
                out.append(f"#define DISK_{record}_{n.split('.')[-1]} 0x{a:x}")
        out.append("")
    out.append("#endif")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    text = generate()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != text:
            print(f"{OUTPUT.relative_to(ROOT)} is out of date; run tools/gen_disk_layouts.py")
            return 1
        return 0
    OUTPUT.write_text(text)
    print(f"wrote {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
