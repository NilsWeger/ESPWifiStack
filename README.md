# ESPWifiStack: ESP-NOW multi-node test (ESP8266)

Checks that several ESP8266 boards can talk to each other over **ESP-NOW**,
without a router, and measures packet loss and round-trip time per link.

- Firmware: Arduino core, PlatformIO (`src/main.cpp`, `include/protocol.h`)
- Host tools: `tools/multi_monitor.py` (all serial ports in one view, plus CSV)
- Docs: [ESP-NOW research and test plan](docs/research-espnow-esp8266.md), [development setup](docs/dev-setup.md)

## Quick start (local, with 2–3 boards)

```bash
pip install platformio pyserial

pio device list                                      # find the ports
cp platformio_local.ini.example platformio_local.ini # then set one port per node

pio run -e node1 -t upload
pio run -e node2 -t upload

python tools/multi_monitor.py -p COM5=node1 -p COM7=node2 --csv run.csv
```

`platformio_local.ini` is git-ignored, so every PC keeps its own ports.
Without it, pass the port per call: `pio run -e node1 -t upload --upload-port COM5`.

Expected output: every node prints `# PEER node=…` for the others, then one
`STAT` line per peer every 5 s:

```
STAT,ms,self,peer,pings_sent,send_fail,no_ack,pongs,loss_pct,rtt_min_us,rtt_avg_us,rtt_max_us,pings_rx,pings_rx_lost,pings_rx_ooo
STAT,15000,1,2,75,0,0,75,0.0,1650,2010,3120,75,0,0
```

Test parameters (channel, ping interval, payload size) are build flags in
`[common]` in `platformio.ini`. The board is `d1_mini`; change it to
`nodemcuv2` if needed.

## Tests

```bash
pio test -e native        # protocol unit tests on the host (needs g++; on Windows e.g. WinLibs/MinGW)
pio test -e node1         # same tests on a board, no host compiler needed
./tools/host_test.sh      # same tests without PlatformIO (g++ + Unity)
```

GitHub Actions builds all node firmwares and runs the tests on every push.
