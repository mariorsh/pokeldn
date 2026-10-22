#!/usr/bin/env python3
"""Probe P4 USB and DISPLAY even when its C6 still has factory firmware. Never flashes.

The display test is synthetic: it sends no wireless packets and delivers no gifts.
"""
import argparse
import json
from pathlib import Path
import struct
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from pokeldn.ldn import esp32


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-radio", action="store_true", help="fail unless C6 radio answers HELLO")
    parser.add_argument("--duration", type=float, default=0, help="seconds of additional DISPLAY load")
    args = parser.parse_args()
    link = serial.Serial(baudrate=args.baud, timeout=0.05, write_timeout=2)
    link.dtr = link.rts = False
    link.port = args.port
    link.open()
    reader = esp32.FrameReader()
    report = {"port": args.port, "initial_baud": args.baud, "checks": []}

    def request(command, payload=b"", timeout=3):
        link.write(b"\x00" + esp32.encode_frame(command, payload))
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            for kind, data in reader.feed(link.read(link.in_waiting or 1)):
                if kind == esp32.MSG_RESULT and len(data) == 5 and data[0] == command:
                    return {"result": struct.unpack("<i", data[1:])[0]}
                if command == esp32.CMD_HELLO and kind == esp32.MSG_INFO:
                    return {"info": esp32.Info.parse(data).text}
                if command == esp32.CMD_STATUS and kind == esp32.MSG_STATUS:
                    return {"status": data.decode(errors="replace")}
        return {"timeout": True}

    def check(name, command, payload=b"", expected=0):
        reply = request(command, payload)
        good = reply.get("result") == expected if expected is not None else "info" in reply
        row = {"check": name, "passed": good, "rejected_frames": reader.rejected, **reply}
        report["checks"].append(row)
        print(json.dumps(row), flush=True)
        return good

    try:
        # Opening a CH340 port can reset the P4. Wait for the passive monitor,
        # independently of C6 readiness, before counting any USB test failure.
        started = time.monotonic()
        while True:
            status = request(esp32.CMD_STATUS, timeout=1)
            if "status" in status:
                break
            if time.monotonic() - started > 12:
                raise RuntimeError("P4 did not answer STATUS after reset")
        report["status"] = status
        report["startup_rejected_frames"] = reader.rejected
        reader.rejected = 0
        report["radio"] = request(esp32.CMD_HELLO)
        print(json.dumps({"check": "radio_hello", "ready": "info" in report["radio"], **report["radio"]}), flush=True)
        if args.require_radio:
            report["checks"].append({"check": "radio_required", "passed": "info" in report["radio"]})
        display = esp32.display_show_payload("gift", 0, "Prueba USB", "PokeChanger")
        check("display", esp32.CMD_DISPLAY, display)
        # The malformed command must be rejected without losing synchronization.
        check("malformed_display_rejected", esp32.CMD_DISPLAY, b"", expected=0x102)
        passed = sum(check(f"display_{i}", esp32.CMD_DISPLAY, display) for i in range(10))
        report["display_repeated"] = {"passed": passed, "total": 10}
        if check("baud_460800", esp32.CMD_BAUD, struct.pack("<I", 460800)):
            link.baudrate = 460800
            time.sleep(0.2)
            check("display_at_460800", esp32.CMD_DISPLAY, display)
            if check("baud_restore", esp32.CMD_BAUD, struct.pack("<I", args.baud)):
                link.baudrate = args.baud
                time.sleep(0.2)
                check("display_after_restore", esp32.CMD_DISPLAY, display)
        if args.duration > 0:
            started = time.monotonic()
            good = bad = 0
            sprite = esp32.display_sprite_payload("gift", [])
            while time.monotonic() - started < args.duration:
                if request(esp32.CMD_DISPLAY, sprite).get("result") == 0:
                    good += 1
                else:
                    bad += 1
                time.sleep(0.005)
            load = {"seconds": round(time.monotonic() - started, 2), "ok": good, "failed": bad}
            report["checks"].append({"check": "display_load", "passed": bad == 0 and good > 0, **load})
            print(json.dumps(load), flush=True)
        report["final_status"] = request(esp32.CMD_STATUS)
        report["rejected_frames"] = reader.rejected
    finally:
        link.close()
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n")
    return 0 if all(c["passed"] for c in report["checks"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
