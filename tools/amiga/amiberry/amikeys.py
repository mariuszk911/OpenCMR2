"""Type into the Amiga guest through Amiberry's IPC (SEND_KEY, Amiga raw keycodes, US keymap).

    from amikeys import *
    key(103, 1); key(18, 1); key(18, 0); key(103, 0)   # RAmiga+E: Workbench "Execute Command"
    type_text("DH20:pc GAME:FrontEnd/Videos/cm.bik 12 > DH20:out.log", ret=True)

Take a SCREENSHOT before typing: if the Execute dialog or a Shell does not have the
focus, the text goes to whatever window does (workbench shortcuts included).
"""
import time

from ami import cmd

ROWS = {'`': 0x00, '-': 0x0B, '=': 0x0C, '\\': 0x0D, '[': 0x1A, ']': 0x1B, ';': 0x29, "'": 0x2A,
        ',': 0x38, '.': 0x39, '/': 0x3A, ' ': 0x40}
for i, c in enumerate("1234567890"):
    ROWS[c] = 0x01 + i
for i, c in enumerate("qwertyuiop"):
    ROWS[c] = 0x10 + i
for i, c in enumerate("asdfghjkl"):
    ROWS[c] = 0x20 + i
for i, c in enumerate("zxcvbnm"):
    ROWS[c] = 0x31 + i
SHIFTED = {':': ';', '>': '.', '_': '-', '<': ',', '"': "'"}
SHIFT, RETURN, BACKSPACE = 0x60, 0x44, 0x41


def key(code, state):
    cmd("SEND_KEY\t%d\t%d" % (code, state))


def tap(code, shift=False):
    if shift:
        key(SHIFT, 1)
        time.sleep(.03)
    key(code, 1)
    time.sleep(.04)
    key(code, 0)
    time.sleep(.04)
    if shift:
        key(SHIFT, 0)
        time.sleep(.03)


def type_text(text, ret=False):
    for ch in text:
        if ch in SHIFTED:
            tap(ROWS[SHIFTED[ch]], True)
        elif ch.isupper():
            tap(ROWS[ch.lower()], True)
        else:
            tap(ROWS[ch])
    if ret:
        tap(RETURN)


def shot(path):
    return cmd("SCREENSHOT\t" + path)
