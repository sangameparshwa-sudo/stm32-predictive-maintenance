#!/usr/bin/env python3
# PdM Edge Node — Serial-to-Browser Bridge (4 sensors)
# Reads "current,vibration,acoustic,temperature" from COM5, serves JSON + dashboard.
# Run:  "C:\Users\Hp\AppData\Local\Programs\Python\Python313\python.exe" bridge.py
# Then open: http://localhost:8080   (close PuTTY first — one owner of COM5)

import serial, threading, json, os, time
from http.server import BaseHTTPRequestHandler, HTTPServer

COM_PORT = "COM5"
BAUD = 115200
HTTP_PORT = 8080
DASHBOARD_FILE = "pdm_dashboard_live.html"

latest = {"current":0.0,"vibration":0.0,"acoustic":0.0,"temperature":0.0,"connected":False}
lock = threading.Lock()

def serial_reader():
    while True:
        try:
            ser = serial.Serial(COM_PORT, BAUD, timeout=2)
            print(f"[bridge] Connected to {COM_PORT} @ {BAUD}")
            with lock: latest["connected"] = True
            while True:
                raw = ser.readline().decode("ascii", errors="ignore").strip()
                if not raw or raw.startswith("PdM"):   # skip the banner line
                    continue
                p = raw.split(",")
                if len(p) != 4:
                    continue
                try:
                    c,v,a,t = float(p[0]),float(p[1]),float(p[2]),float(p[3])
                except ValueError:
                    continue
                with lock:
                    latest.update(current=c,vibration=v,acoustic=a,temperature=t,connected=True)
        except serial.SerialException as e:
            print(f"[bridge] Serial error: {e} — retrying (PuTTY closed? board plugged in?)")
            with lock: latest["connected"] = False
            time.sleep(2)

class H(BaseHTTPRequestHandler):
    def log_message(self,*a): pass
    def do_GET(self):
        if self.path == "/data":
            with lock: body = json.dumps(latest).encode()
            self.send_response(200)
            self.send_header("Content-Type","application/json")
            self.send_header("Access-Control-Allow-Origin","*")
            self.send_header("Cache-Control","no-store")
            self.end_headers(); self.wfile.write(body)
        else:
            path = DASHBOARD_FILE if self.path in ("/","") else self.path.lstrip("/")
            if os.path.exists(path) and path.endswith(".html"):
                with open(path,"rb") as f: b=f.read()
                self.send_response(200); self.send_header("Content-Type","text/html")
                self.end_headers(); self.wfile.write(b)
            else:
                self.send_response(404); self.end_headers()
                self.wfile.write(b"Put pdm_dashboard_live.html next to bridge.py")

if __name__=="__main__":
    threading.Thread(target=serial_reader,daemon=True).start()
    print(f"[bridge] Dashboard: http://localhost:{HTTP_PORT}")
    print(f"[bridge] Press Ctrl+C to stop.")
    HTTPServer(("",HTTP_PORT),H).serve_forever()
