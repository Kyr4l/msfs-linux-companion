#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kyr4l
"""Tiny fake SimConnect TCP server for testing msfs-linux-companion without MSFS.

It speaks just enough of the wire protocol: answers Open, logs mapped/transmitted
events, emulates the MobiFlight WASM module command/response areas (replies
MF.Pong to MF.Ping and prints executed calculator code) and answers system state
requests. Run:  tools/mock_simconnect.py [--port 500] and point the app at it.

Options: --aircraft FOLDER   aircraft folder name reported by AircraftLoaded (default FlyByWire_A320_NEO), e.g. Asobo_CJ4
         --late N            the first N EnumerateInputEvents requests answer with an empty list (cockpit still loading)
Watched values: MF.SimVars.Clear / MF.SimVars.Add.<code> fill the module's list for the client; slot n holds the n-th code, its
value is a fixed number derived from the code. Like the real module the area keeps its old content until the new value is
written a moment after the Add, changes are pushed to ON_SET requests and every second all 512 slots go to PERIOD_SECOND ones.
Input events: a small fixed set per aircraft, Set/Get work and are logged ("INPUT EVENT SET name=value").
"""
import argparse
import socket
import struct
import threading
import time
import zlib

RECV_EXCEPTION, RECV_OPEN, RECV_QUIT, RECV_EVENT, RECV_EVENT_FILENAME = 1, 2, 3, 4, 6
RECV_SIMOBJECT_DATA, RECV_SYSTEM_STATE, RECV_CLIENT_DATA = 8, 15, 16


def cstr(b):
    return b.split(b"\0", 1)[0].decode("latin1", "replace")


INPUT_EVENTS = {
    "default": [("LIGHTING_LANDING_1", 0.0), ("LIGHTING_LANDING_2", 0.0), ("LIGHTING_STROBE_0", 2.0), ("ELECTRICAL_BATTERY_1", 0.0),
                ("FUEL_PUMP_1", 0.0), ("AS1000_PFD_VOLUME_1_INC", 0.0), ("AS1000_PFD_SOFTKEYS_1", 0.0), ("HANDLING_FLAPS", 1.0)],
    "Asobo_CJ4": [("CJ4_AVIONICS_MASTER", 0.0), ("LIGHTING_LANDING_1", 0.0), ("LIGHTING_TAXI_1", 0.0), ("ELECTRICAL_BATTERY_1", 0.0),
                  ("ELECTRICAL_STANDBY_BATTERY", 0.0), ("FUEL_BOOST_PUMP_1", 0.0), ("ANTI_ICE_ENGINE_1", 0.0), ("TRIM_AILERON", 0.5)],
    "Asobo_FA18E": [("AS04F_MASTER_ARM", 0.0), ("LIGHTING_FORMATION", 0.0), ("ELECTRICAL_BATTERY_1", 1.0), ("HANDLING_TAILHOOK", 0.0),
                    ("ENGINE_L_THROTTLE_CUTOFF", 0.0), ("RADIO_COMM1_VOLUME", 3.0)],
}
LVARS = ["A32NX_FOO", "ASOBO_LIGHT_STATE", "CJ4_PANEL_BRT", "LIGHTING_LANDING_1", "XMLVAR_TEST_VALUE"]
FNV_OFFSET, FNV_PRIME = 0xcbf29ce484222325, 0x100000001b3


def lvar_value(code):
    return float(zlib.crc32(code.encode()) % 97 + 1)


LVAR_SLOTS = 512


def name_hash(name):
    h = FNV_OFFSET
    for ch in name.encode():
        h = ((h ^ ch) * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


class Config:
    aircraft = "FlyByWire_A320_NEO"
    late = 0
    enum_requests = 0
    values = {}
    lvar_area = {}  # slot -> value; the module's area outlives the clients


class Client(threading.Thread):
    def __init__(self, sock, addr, verbose):
        super().__init__(daemon=True)
        self.sock, self.addr, self.verbose = sock, addr, verbose
        events = INPUT_EVENTS.get(Config.aircraft, INPUT_EVENTS["default"])
        if not Config.values:  # the simulator keeps its switch positions when a client reconnects
            Config.values.update({n: v for n, v in events})
        self.input_values = Config.values
        self.input_by_hash = {name_hash(n): n for n, _ in events}
        self.mf_clients = {}
        self.events = {}
        self.client_data_names = {}
        self.defs = {}
        self.requests = {}  # request id -> (data id, define id)
        self.sim_requests = {}  # request id -> define id (simobject data)
        self.sim_defs = {}  # define id -> list of (name, units)
        self.req_info = {}  # request id -> (client data id, define id, period, flags)
        self.lvar_codes = []  # the module's list for this client
        self.send_lock = threading.Lock()
        self.ticker = None

    def aircraft_path(self):
        return ("C:\\mock\\SimObjects\\Airplanes\\%s\\aircraft.CFG" % Config.aircraft).encode()

    def send(self, msg_id, payload):
        data = struct.pack("<III", 12 + len(payload), 5, msg_id) + payload
        with self.send_lock:
            self.sock.sendall(data)

    def lvar_requests(self, period):
        return [(rid, did) for rid, (cid, did, per, fl) in self.req_info.items()
                if per == period and self.client_data_names.get(cid, "").endswith(".LVars")]

    def push_lvars(self, period, slots):
        pairs = b"".join(struct.pack("<If", i, Config.lvar_area.get(i, 0.0)) for i in slots)
        for rid, did in self.lvar_requests(period):
            try:
                self.send(RECV_CLIENT_DATA, struct.pack("<7I", rid, 0, did, 0, 0, 0, len(slots)) + pairs)
            except OSError:
                return

    def tick(self):
        while True:
            time.sleep(1.0)
            if self.lvar_requests(4):
                self.push_lvars(4, range(LVAR_SLOTS))

    def module_writes(self, slot, code):
        Config.lvar_area[slot] = lvar_value(code)
        self.push_lvars(3, [slot])

    def log(self, *a):
        print(f"[{self.addr[1]}]", *a, flush=True)

    def run(self):
        buf = b""
        try:
            while True:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                buf += chunk
                while len(buf) >= 16:
                    size, proto, ptype, send_id = struct.unpack_from("<IIII", buf, 0)
                    if len(buf) < size:
                        break
                    body = buf[16:size]
                    buf = buf[size:]
                    self.handle(ptype & 0x0FFFFFFF, send_id, body, proto)
        except (ConnectionResetError, BrokenPipeError):
            pass
        self.log("disconnected")

    def handle(self, t, send_id, b, proto):
        if t == 0x01:
            name = cstr(b[:256])
            self.log(f"OPEN from '{name}' (protocol {proto})")
            payload = b"Mock Flight Simulator".ljust(256, b"\0") + struct.pack("<10I", 11, 0, 282174, 999, 11, 0, 62651, 3, 0, 0)
            self.send(RECV_OPEN, payload)
        elif t == 0x04:
            eid, = struct.unpack_from("<I", b, 0)
            name = cstr(b[4:260])
            self.events[eid] = name
            self.log(f"map event {eid} -> {name}")
        elif t == 0x05:
            obj, eid, data, group, flags = struct.unpack_from("<IIIII", b, 0)
            self.log(f"EVENT {self.events.get(eid, '?')} data={struct.unpack('<i', struct.pack('<I', data))[0]} group={group} flags={flags:#x}")
        elif t == 0x0C:
            did, = struct.unpack_from("<I", b, 0)
            name, units = cstr(b[4:260]), cstr(b[260:516])
            self.sim_defs.setdefault(did, []).append((name, units))
            if self.verbose:
                self.log(f"data def {did} += {name} ({units})")
        elif t == 0x0E:
            rid, did, obj, period, flags, origin, interval, limit = struct.unpack_from("<8I", b, 0)
            self.sim_requests[rid] = (did, flags)
            self.log(f"request data: req {rid} def {did} ({len(self.sim_defs.get(did, []))} vars) period {period} interval {interval} flags {flags}")
            self.send_sim_data(rid, did, flags)
        elif t == 0x17:
            eid, = struct.unpack_from("<I", b, 0)
            name = cstr(b[4:260])
            self.log(f"subscribe system event {eid} {name}")
            if name == "Sim":
                self.send(RECV_EVENT, struct.pack("<III", 0xFFFFFFFF, eid, 1))
            if name == "AircraftLoaded":
                self.send(RECV_EVENT_FILENAME, struct.pack("<III", 0xFFFFFFFF, eid, 0) + self.aircraft_path().ljust(260, b"\0") + struct.pack("<I", 0))
        elif t == 0x35:
            rid, = struct.unpack_from("<I", b, 0)
            state = cstr(b[4:260])
            self.log(f"request system state {state}")
            s = self.aircraft_path() if state == "AircraftLoaded" else b""
            self.send(RECV_SYSTEM_STATE, struct.pack("<IIf", rid, 1, 0.0) + s.ljust(260, b"\0"))
        elif t == 0x37:
            name = cstr(b[:256])
            cid, = struct.unpack_from("<I", b, 256)
            self.client_data_names[cid] = name
            self.log(f"map client data '{name}' -> {cid}")
        elif t == 0x39:
            did, off, size, eps, datum = struct.unpack_from("<IIIfI", b, 0)
            self.defs[did] = (off, size)
        elif t == 0x3B:
            cid, rid, did, period, flags, *_ = struct.unpack_from("<8I", b, 0)
            self.requests[rid] = (cid, did)
            self.req_info[rid] = (cid, did, period, flags)
            self.log(f"request client data {self.client_data_names.get(cid)} req {rid} period {period} flags {flags}")
            if self.client_data_names.get(cid, "").endswith(".LVars") and period == 4 and self.ticker is None:
                self.ticker = threading.Thread(target=self.tick, daemon=True)
                self.ticker.start()
        elif t == 0x3C:
            cid, did, res, count, unit = struct.unpack_from("<IIIII", b, 0)
            text = cstr(b[20:20 + unit])
            name = self.client_data_names.get(cid, "?")
            if text == "MF.DummyCmd":
                return
            self.log(f"client data '{name}' <- {text}")
            if name.endswith(".Command"):
                prefix = name[: -len(".Command")]
                responses = []
                if text == "MF.Ping":
                    responses.append("MF.Pong")
                elif text.startswith("MF.Clients.Add."):
                    cname = text[len("MF.Clients.Add."):]
                    self.mf_clients[cname] = True
                    responses.append(f"MF.Clients.Add.{cname}.Finished")
                elif text == "MF.LVars.List":
                    responses = ["MF.LVars.List.Start"] + sorted(LVARS) + ["MF.LVars.List.End"]
                elif text == "MF.SimVars.Clear":
                    self.lvar_codes = []  # the area keeps its content (stale until the new codes are written)
                elif text.startswith("MF.SimVars.Add."):
                    code = text[len("MF.SimVars.Add."):]
                    slot = len(self.lvar_codes)
                    self.lvar_codes.append(code)
                    self.log(f"   watch slot {slot}: {code}")
                    if slot in Config.lvar_area:
                        self.push_lvars(3, [slot])  # the old content of the slot goes out before the module has written the new value
                    threading.Timer(0.05, self.module_writes, (slot, code)).start()
                elif text.startswith("MF.SimVars.Set."):
                    self.log(f"   execute_calculator_code: {text[len('MF.SimVars.Set.'):]}")
                for resp in responses:
                    for rid, (rcid, rdid) in self.requests.items():
                        if self.client_data_names.get(rcid) == prefix + ".Response":
                            payload = struct.pack("<7I", rid, 0, rdid, 0, 0, 0, 1) + resp.encode().ljust(1024, b"\0")
                            self.send(RECV_CLIENT_DATA, payload)
        elif t == 0x4F:  # EnumerateInputEvents
            rid, = struct.unpack_from("<I", b, 0)
            Config.enum_requests += 1
            if Config.enum_requests <= Config.late:
                entries = []
                self.log(f"enumerate input events -> empty (cockpit still loading, request {Config.enum_requests}/{Config.late})")
            else:
                entries = [(n, h) for h, n in self.input_by_hash.items()]
                self.log(f"enumerate input events -> {len(entries)} events")
            # two packets, to exercise the client's accumulation; packet index semantics (entry number / packets in total)
            chunks = [entries[i:i + 5] for i in range(0, len(entries), 5)] or [[]]
            for idx, chunk in enumerate(chunks):
                body = struct.pack("<IIII", rid, len(chunk), idx, len(chunks))
                for n, h in chunk:
                    body += n.encode().ljust(64, b"\0") + struct.pack("<QI", h, 0)
                self.send(34, body)
        elif t == 0x50:  # GetInputEvent
            rid, h = struct.unpack_from("<IQ", b, 0)
            n = self.input_by_hash.get(h)
            if n is None:
                self.send(RECV_EXCEPTION, struct.pack("<III", 42, send_id, 0))  # GET_INPUT_EVENT_FAILED
            else:
                self.send(35, struct.pack("<IId", rid, 0, self.input_values[n]))
        elif t == 0x51:  # SetInputEvent
            h, length = struct.unpack_from("<QI", b, 0)
            n = self.input_by_hash.get(h)
            if n is None:
                self.log(f"INPUT EVENT SET unknown hash {h:#x}")
                self.send(RECV_EXCEPTION, struct.pack("<III", 43, send_id, 0))  # SET_INPUT_EVENT_FAILED
            else:
                self.input_values[n], = struct.unpack_from("<d", b, 12)
                self.log(f"INPUT EVENT SET {n}={self.input_values[n]:g}")
        else:
            if self.verbose:
                self.log(f"packet type {t:#x} ({len(b)} bytes)")

    def send_sim_data(self, rid, did, flags):
        vars_ = self.sim_defs.get(did, [])
        values = []
        for name, _ in vars_:
            v = 0.0
            if name.upper().startswith("SIM ON GROUND"):
                v = 1.0
            elif name.upper().startswith("G FORCE"):
                v = 1.0
            elif name.upper().startswith("GROUND VELOCITY"):
                v = 42.0
            values.append(v)
        if flags & 2:  # tagged: (datum id, value) pairs
            body = b"".join(struct.pack("<Id", i, v) for i, v in enumerate(values))
        else:
            body = struct.pack(f"<{len(values)}d", *values)
        payload = struct.pack("<7I", rid, 0, did, flags, 0, 1, len(values)) + body
        self.send(RECV_SIMOBJECT_DATA, payload)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=500)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--aircraft", default="FlyByWire_A320_NEO")
    ap.add_argument("--late", type=int, default=0)
    args = ap.parse_args()
    Config.aircraft, Config.late = args.aircraft, args.late
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(4)
    print(f"mock SimConnect listening on {args.host}:{args.port}", flush=True)
    while True:
        sock, addr = srv.accept()
        print(f"[{addr[1]}] connected", flush=True)
        Client(sock, addr, args.verbose).start()


if __name__ == "__main__":
    main()
