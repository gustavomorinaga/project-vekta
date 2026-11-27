"""Plays RecompLauncher's side of the Recomp Achievements channel for test_rl_bridge (after the SDK's own
third_party/rl_achievements/sdk/tests/fake_launcher.py, which checks the SDK's test program, not ours): opens a Unix
socket, starts the program with RL_SESSION set, answers hello with quiet, and checks what port/kz1hd/rl_bridge.cpp sent.
"""
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading

exe = sys.argv[1]
tmp = tempfile.mkdtemp()
path = os.path.join(tmp, "rl.sock")
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(path)
srv.listen(1)
got = []


def serve():
    c, _ = srv.accept()
    f = c.makefile("rwb")
    for raw in f:
        m = json.loads(raw)
        got.append(m)
        if m["t"] == "hello":
            f.write(b'{"t":"quiet"}\n')
            f.flush()


t = threading.Thread(target=serve, daemon=True)
t.start()
out = subprocess.run([exe, "channel"], env=dict(os.environ, RL_SESSION=path), capture_output=True, text=True,
                     timeout=30)
t.join(2)
os.unlink(path)
os.rmdir(tmp)
print("program printed:", out.stdout.strip(), out.stderr.strip())
print("launcher got:   ", json.dumps(got))

types = [m["t"] for m in got]
natives = [m for m in got if m["t"] == "native"]
checks = {
    "exit 0": out.returncode == 0,
    "hello first, ps3, no memory, the version": types[:1] == ["hello"] and got[0]["console"] == "ps3"
        and got[0]["memory"] is False and got[0].get("version") == "9.9.9" and "game" not in got[0],
    "one native_list right after hello, 1..3, no platinum": types[1:2] == ["native_list"]
        and types.count("native_list") == 1
        and [a["id"] for a in got[1]["achievements"]] == [1, 2, 3]
        and got[1]["achievements"][1] == {"id": 2, "title": "Second", "description": "Do the Second thing",
                                          "gamerscore": 0},
    "1 earlier": [m["id"] for m in natives if m.get("earlier")] == [1],
    "casual debug": {"t": "casual", "reason": "debug"} in got,
    "one save_loaded": types.count("save_loaded") == 1,
    "2 live, platinum never": [m["id"] for m in natives if not m.get("earlier")] == [2],
    "quiet seen": "quiet=1" in out.stdout,
}
for name, ok in checks.items():
    print(("ok   " if ok else "FAIL ") + name)
sys.exit(0 if all(checks.values()) else 1)
