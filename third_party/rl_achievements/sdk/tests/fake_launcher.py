"""Stands in for RecompLauncher: opens the channel, sends a list, records what the recomp sends."""
import json, os, socket, subprocess, sys, tempfile, threading

exe = sys.argv[1]
path = os.path.join(tempfile.mkdtemp(), "rl.sock")
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(path); srv.listen(1)
got = []

def serve():
    c, _ = srv.accept()
    f = c.makefile("rwb")
    for raw in f:
        m = json.loads(raw)
        got.append(m)
        if m["t"] == "hello":
            f.write(b'{"t":"quiet"}\n')
            f.write(json.dumps({"t": "list", "achievements": [
                {"id": 10, "trigger": "0xH0019=2"},
                {"id": 11, "trigger": "0xH0019=9"}], "unlocked": [11],
                "leaderboards": [{"id": 5, "start": "0xH0020=1", "cancel": "0xH0020=2", "submit": "0xH0020=3", "value": "0xH0021"}],
                "rich_presence": "Display:\nWorld @Num(0xH0022)\n\nFormat:Num\nFormatType=VALUE\n"}).encode() + b"\n")
            f.flush()
        if m["t"] == "event":
            f.write(b'{"t":"peek","address":25,"size":4,"seq":7}\n'); f.flush()

t = threading.Thread(target=serve, daemon=True); t.start()
out = subprocess.run([exe], env=dict(os.environ, RL_SESSION=path), capture_output=True, text=True, timeout=30)
t.join(2)
types = [m["t"] for m in got]
print("recomp printed:", out.stdout.strip())
print("launcher got:  ", json.dumps(got))
ok = (types[:2] == ["hello", "native_list"] and got[1]["achievements"][0]["title"] == "Pants On"
      and "list_ok" in types and got[0]["game"] == "supermarioworldrecomp" and next(m for m in got if m["t"] == "list_ok")["rules"] == 1
      and {"t": "unlock", "id": 10} .items() <= next(m for m in got if m["t"] == "unlock").items()
      and "save_loaded" in types and any(m["t"] == "native" and m["id"] == 42 for m in got)
      and any(m["t"] == "event" and m["name"] == "beat_bowser" for m in got)
      and any(m["t"] == "mem" and m["seq"] == 7 and m["hex"] == "02000000" for m in got)
      and "quiet=1" in out.stdout
      and next(m for m in got if m["t"] == "list_ok").get("boards") == 1 and next(m for m in got if m["t"] == "list_ok").get("presence") is True
      and any(m["t"] == "lboard" and m["id"] == 9 and m["value"] == 1234 for m in got)
      and any(m["t"] == "lboard_start" and m["id"] == 5 for m in got)
      and any(m["t"] == "lboard" and m["id"] == 5 and m["value"] == 77 for m in got)
      and any(m["t"] == "presence" and m["text"] == "World 3" for m in got))
print("PASS" if ok else "FAIL"); sys.exit(0 if ok else 1)
