"""Read-only inspection of the Amiberry guest over IPC (READ_MEM, GET_CPU_REGS).

READ_MEM takes TAB-separated "0xADDR<TAB>width" and answers a decimal value.
ExecBase is the longword at address 4 once the system has booted.
"""
import re

from ami import cmd


def rd(addr, width):
    reply = cmd("READ_MEM\t0x%X\t%d" % (addr, width)).strip().split("\t")
    return int(reply[1]) if reply[0] == "OK" else None


def cstr(addr, limit=48):
    data = b""
    for i in range(0, limit, 4):
        value = rd(addr + i, 4)
        if value is None:
            break
        data += value.to_bytes(4, "big")
    return data.split(b"\0")[0].decode("latin1")


def regs():
    reply = cmd("GET_CPU_REGS")
    return {k: int(v, 16) for k, v in re.findall(r"(\w+)=([0-9A-F]+)", reply)
            if k not in ("T", "S", "X", "N", "Z", "V", "C")}


def tasks():
    """(list, node, type, name, state, sigwait, stack lower, upper, saved SP) per exec task."""
    exec_base = rd(4, 4)
    out = []
    for label, offset in (("READY", 0x196), ("WAIT", 0x1A4)):
        node = rd(exec_base + offset, 4)
        while node and rd(node, 4):
            name = rd(node + 10, 4)
            out.append((label, node, rd(node + 8, 1), cstr(name) if name else None, rd(node + 15, 1),
                        rd(node + 0x16, 4), rd(node + 0x3A, 4), rd(node + 0x3E, 4), rd(node + 0x36, 4)))
            node = rd(node, 4)
    this = rd(exec_base + 0x114, 4)
    out.append(("THIS", this, rd(this + 8, 1), cstr(rd(this + 10, 4)), rd(this + 15, 1), 0, 0, 0, 0))
    return out
