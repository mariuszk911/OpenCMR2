"""Minimal client for Amiberry's IPC socket (tab-separated commands).

Amiberry (8.5) listens on /run/user/<uid>/amiberry.sock, or amiberry_N.sock for a
second instance. Arguments must be separated by TAB, not spaces; a space-separated
command is answered with "Unknown command". Commands are upper-cased by the server
for matching only.
    python3 ami.py GET_STATUS
    python3 ami.py "SCREENSHOT<TAB>/tmp/shot.png"
"""
import os
import socket
import sys

SOCKET = os.environ.get("AMIBERRY_SOCK", "/run/user/%d/amiberry.sock" % os.getuid())


def cmd(command, timeout=5):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(SOCKET)
    s.sendall((command + "\n").encode())
    try:
        reply = s.recv(65536).decode(errors="replace")
    except Exception as error:  # timeout
        reply = "ERR " + str(error)
    s.close()
    return reply


if __name__ == "__main__":
    print(cmd(" ".join(sys.argv[1:])))
