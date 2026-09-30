#!/usr/bin/env python3
"""Watch the serial output of several ESP8266 nodes in one terminal.

Each line gets a host timestamp and a label for its port. STAT lines from
the firmware can also be written to one CSV file for later analysis.

Examples:
    python tools/multi_monitor.py                      # all USB serial ports found
    python tools/multi_monitor.py -p /dev/ttyUSB0 -p /dev/ttyUSB1
    python tools/multi_monitor.py -p COM3=A -p COM4=B --csv run1.csv
    python tools/multi_monitor.py --list
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import queue
import sys
import threading
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    sys.exit("pyserial is missing: pip install -r tools/requirements.txt")

STAT_FIELDS = [
    "ms", "self", "peer", "pings_sent", "send_fail", "no_ack", "pongs", "loss_pct",
    "rtt_min_us", "rtt_avg_us", "rtt_max_us", "pings_rx", "pings_rx_lost", "pings_rx_ooo",
]

# USB-UART bridges used on common ESP8266 boards (CH340, CP210x, FTDI).
KNOWN_VIDS = {0x1A86, 0x10C4, 0x0403}

COLORS = ["\033[36m", "\033[33m", "\033[35m", "\033[32m", "\033[34m", "\033[31m"]
RESET = "\033[0m"


def find_ports() -> list[str]:
    return [p.device for p in list_ports.comports() if p.vid in KNOWN_VIDS]


def parse_port_arg(arg: str) -> tuple[str, str | None]:
    # "COM3=A" -> ("COM3", "A"); "/dev/ttyUSB0" -> ("/dev/ttyUSB0", None)
    if "=" in arg:
        port, label = arg.split("=", 1)
        return port, label
    return arg, None


def reader(port: str, label: str, baud: int, out: queue.Queue, stop: threading.Event) -> None:
    while not stop.is_set():
        try:
            with serial.Serial(port, baud, timeout=0.5) as ser:
                out.put((label, f"# monitor: opened {port}"))
                while not stop.is_set():
                    raw = ser.readline()
                    if raw:
                        out.put((label, raw.decode("utf-8", errors="replace").rstrip()))
        except serial.SerialException as exc:
            out.put((label, f"# monitor: {port} unavailable ({exc}), retrying"))
            stop.wait(2.0)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", action="append", default=[], help="PORT or PORT=LABEL, repeatable")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("--csv", help="append STAT lines to this CSV file")
    ap.add_argument("--stat-only", action="store_true", help="print only STAT lines")
    ap.add_argument("--no-color", action="store_true")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    args = ap.parse_args()

    if args.list:
        for p in list_ports.comports():
            vid = f"{p.vid:04X}" if p.vid is not None else "----"
            print(f"{p.device:24} vid={vid} {p.description}")
        return 0

    ports = [parse_port_arg(a) for a in args.port] or [(p, None) for p in find_ports()]
    if not ports:
        print("No ESP serial ports found. Use --list and pass them with -p.", file=sys.stderr)
        return 1

    color = sys.stdout.isatty() and not args.no_color
    labels = {}
    for i, (port, label) in enumerate(ports):
        labels[label or port] = COLORS[i % len(COLORS)] if color else ""

    csv_file = None
    csv_writer = None
    if args.csv:
        csv_file = open(args.csv, "a", newline="")
        csv_writer = csv.writer(csv_file)
        if csv_file.tell() == 0:
            csv_writer.writerow(["host_time", "port"] + STAT_FIELDS)

    lines: queue.Queue = queue.Queue()
    stop = threading.Event()
    for port, label in ports:
        t = threading.Thread(target=reader, args=(port, label or port, args.baud, lines, stop), daemon=True)
        t.start()

    width = max(len(l) for l in labels)
    try:
        while True:
            try:
                label, line = lines.get(timeout=0.5)
            except queue.Empty:
                continue
            now = dt.datetime.now()
            is_stat = line.startswith("STAT,")
            if is_stat and csv_writer:
                fields = line.split(",")[1:]
                if len(fields) == len(STAT_FIELDS):
                    csv_writer.writerow([now.isoformat(timespec="milliseconds"), label] + fields)
                    csv_file.flush()
            if args.stat_only and not is_stat:
                continue
            c = labels[label]
            print(f"{now:%H:%M:%S.%f}"[:-3] + f" {c}{label:<{width}}{RESET if c else ''} | {line}")
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        time.sleep(0.6)
        if csv_file:
            csv_file.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
