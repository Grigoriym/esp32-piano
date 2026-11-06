#!/usr/bin/env python3
"""Serves docs/live.html and streams what the "Chicco Piano" port plays to it.

    python3 tools/live_server.py          then open http://127.0.0.1:21929/

The PC side of the toy (tools/pc_synth.sh) starts it too. It runs `aseqdump` on
the bridge's ALSA port, so the page sees what FluidSynth gets, at the time it
gets it (the beat and the loop come from the bridge already on time). Each
message goes to the page as a Server-Sent Event: JSON with t (seconds, this
server's clock), type (on/off/cc/prog/bend), ch (1-16) and the values. When
the bridge isn't running, it retries every 2 s; the page shows "waiting".
"""
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(os.environ.get("LIVE_PORT", "21929"))
DOCS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs")
SOURCE = "Chicco Piano"

clients = set()
clients_lock = threading.Lock()
state = {"source": False, "programs": {}}  # programs: the last one per channel, for a page opened later

LINE = re.compile(r"^\s*\d+:\d+\s+(Note on|Note off|Control change|Program change|Pitch bend)\s+(\d+),\s*(.*)$")


def broadcast(ev):
    data = json.dumps(ev)
    with clients_lock:
        for q in list(clients):
            try:
                q.put_nowait(data)
            except queue.Full:
                pass  # a stuck page loses events, never the reader


def parse(line):
    m = LINE.match(line)
    if not m:
        return None
    kind, ch, rest = m.group(1), int(m.group(2)) + 1, m.group(3)
    vals = dict((k, int(v)) for k, v in re.findall(r"(\w+) (-?\d+)", rest))
    t = time.monotonic()
    if kind == "Note on" and vals.get("velocity", 0) > 0:
        return {"t": t, "type": "on", "ch": ch, "note": vals["note"], "vel": vals["velocity"]}
    if kind in ("Note on", "Note off"):
        return {"t": t, "type": "off", "ch": ch, "note": vals["note"]}
    if kind == "Control change":
        return {"t": t, "type": "cc", "ch": ch, "cc": vals["controller"], "value": vals["value"]}
    if kind == "Program change":
        return {"t": t, "type": "prog", "ch": ch, "program": vals["program"]}
    return {"t": t, "type": "bend", "ch": ch, "value": vals["value"]}


def reader():
    while True:
        try:
            p = subprocess.Popen(["aseqdump", "-p", SOURCE], stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL, text=True, bufsize=1)
        except FileNotFoundError:
            sys.exit("aseqdump not found (alsa-utils)")
        first = True
        for line in p.stdout:
            if first:
                first = False
                state["source"] = True
                broadcast({"t": time.monotonic(), "type": "source", "up": True})
            ev = parse(line)
            if ev:
                if ev["type"] == "prog":
                    state["programs"][ev["ch"]] = ev["program"]
                broadcast(ev)
        p.wait()
        if state["source"]:
            state["source"] = False
            broadcast({"t": time.monotonic(), "type": "source", "up": False})
        time.sleep(2)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if self.path == "/events":
            self.events()
        elif self.path in ("/", "/live.html"):
            with open(os.path.join(DOCS, "live.html"), "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_error(404)

    def events(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        q = queue.Queue(maxsize=2000)
        with clients_lock:
            clients.add(q)
        try:
            hello = {"t": time.monotonic(), "type": "source", "up": state["source"], "now": time.monotonic(),
                     "programs": state["programs"]}
            self.wfile.write(f"data: {json.dumps(hello)}\n\n".encode())
            self.wfile.flush()
            while True:
                try:
                    data = q.get(timeout=10)
                    self.wfile.write(f"data: {data}\n\n".encode())
                except queue.Empty:
                    self.wfile.write(b": ping\n\n")  # notices a closed page
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            with clients_lock:
                clients.discard(q)


if __name__ == "__main__":
    threading.Thread(target=reader, daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    server.daemon_threads = True
    print(f"live: http://127.0.0.1:{PORT}/", flush=True)
    server.serve_forever()
