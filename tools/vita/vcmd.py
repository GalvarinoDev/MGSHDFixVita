#!/usr/bin/env python3
"""Send one command to the vitacompanion command port (1338) and print the reply.
   vcmd.py "<command>" [vita-ip]    e.g. vcmd.py "launch PCSE00020"
   The IP comes from the argument, else VITA_IP, else local/vita.env."""
import os, socket, sys
from pathlib import Path

def vita_ip():
    if len(sys.argv) > 2:
        return sys.argv[2]
    if os.environ.get("VITA_IP"):
        return os.environ["VITA_IP"]
    env = Path(__file__).resolve().parents[2] / "local" / "vita.env"
    if env.is_file():
        for ln in env.read_text().splitlines():
            if ln.startswith("VITA_IP="):
                return ln.split("=", 1)[1].strip().strip('"')
    sys.exit("vcmd: no Vita IP. Set VITA_IP or write VITA_IP=<ip> in local/vita.env")

if len(sys.argv) < 2:
    sys.exit(__doc__)
try:
    s = socket.create_connection((vita_ip(), 1338), timeout=5)
except OSError as e:
    sys.exit(f"vcmd: cannot connect to the Vita: {e}")
s.sendall((sys.argv[1] + "\n").encode())
s.settimeout(3)
data = b""
try:
    while chunk := s.recv(4096):
        data += chunk
except socket.timeout:
    pass
print(data.decode(errors="replace").strip())
