#!/usr/bin/env python3
"""Screen m68k-amigaos-objdump -d output for an address register used as a
pointer before anything in the function loaded it.

Bebbo's GCC 6.5 backend optimiser ("bbb") can fold a load of a pointer into a
memory-indirect operand and drop the load while a later operand still uses the
register, e.g. `move.l ([8,a5],0),4(a0)` with a0 never loaded
(docs/AMIGA_PORT.md, GCC 6.5 gotchas). The same check also catches a scratch
register (a0/a1) used after a call without being reloaded.

The scan follows straight-line code only: at function entry a0-a4/a6 hold
nothing of the function (arguments are on the stack in this ABI), a `jsr`
clobbers a0/a1, and after an unconditional transfer (bra/jmp/rts) every
register counts as loaded, because that code is reached by a jump. It is a
screening tool; read every hit:
- text and tables stored in .text decode as nonsense instructions;
- a function returning a class by value receives the result address in a0
  (libstdc++'s string::begin() and the like), which is not a fault.

Usage:
    m68k-amigaos-objdump -d -C FILE.obj | tools/amiga/scan_unloaded_regs.py [LABEL]
Prints LABEL, function and instruction for every hit; exit status 0.
"""
import re
import sys

FUNC = re.compile(r'^[0-9a-f]+ [0-9a-f]+ <(.*)>:$')
INS = re.compile(r'^\s+[0-9a-f]+:\t[0-9a-f ]+\t(\S+)\s*(.*)$')
REG = re.compile(r'a([0-6])')
# a register used as an address: (aN) d(aN) (aN)+ -(aN) (d,aN,...) ([d,aN],...)
BASE = re.compile(r'\(\[?(?:-?\w+,)?a([0-6])[\],)+]|-\(a([0-6])\)')
TRACKED = frozenset({0, 1, 2, 3, 4, 6})
DATA_SYMBOL = re.compile(r'[-+]0x[0-9a-f]+$|^(g|s)_')


def split_operands(text):
    parts, depth, current = [], 0, ''
    for ch in text:
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        if ch == ',' and depth == 0:
            parts.append(current.strip())
            current = ''
        else:
            current += ch
    if current.strip():
        parts.append(current.strip())
    return parts


def movem_targets(operand):
    regs = set()
    for chunk in operand.split('/'):
        m = re.fullmatch(r'a([0-7])-a([0-7])', chunk)
        if m:
            regs.update(range(int(m.group(1)), int(m.group(2)) + 1))
            continue
        m = re.fullmatch(r'a([0-7])', chunk)
        if m:
            regs.add(int(m.group(1)))
    return regs


def scan(lines):
    func, unloaded = None, set()
    for line in lines:
        m = FUNC.match(line)
        if m:
            func = None if DATA_SYMBOL.search(m.group(1)) else m.group(1)
            unloaded = set(TRACKED)
            continue
        m = INS.match(line)
        if not m or func is None:
            continue
        mnemonic = m.group(1)
        operands = split_operands(m.group(2).split('\t')[0])
        for operand in operands:
            for use in BASE.finditer(operand):
                n = int(use.group(1) or use.group(2))
                if n in unloaded:
                    yield func, line.strip()
                    unloaded.discard(n)
        if mnemonic.startswith('movem') and len(operands) == 2:
            unloaded -= movem_targets(operands[1])
        elif mnemonic.startswith('exg'):
            for operand in operands:
                r = REG.fullmatch(operand)
                if r:
                    unloaded.discard(int(r.group(1)))
        elif operands and not mnemonic.startswith(('cmp', 'tst')):
            r = REG.fullmatch(operands[-1])
            if r:
                unloaded.discard(int(r.group(1)))
        if mnemonic.startswith('jsr'):
            unloaded |= {0, 1}
        if mnemonic.split('.')[0] in ('rts', 'rte', 'jmp', 'bra'):
            unloaded = set()


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else '-'
    for func, instruction in scan(sys.stdin.read().splitlines()):
        print('%s\t%s\t%s' % (label, func, instruction))
    return 0


if __name__ == '__main__':
    sys.exit(main())
