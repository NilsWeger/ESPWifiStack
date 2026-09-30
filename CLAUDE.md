# CLAUDE.md

ESP-NOW multi-node test for ESP8266 boards (Arduino core, PlatformIO).

## Commands
- Unit tests: `pio test -e native` (fallback without PlatformIO registry access: `./tools/host_test.sh`)
- Build all nodes: `pio run -e node1 -e node2 -e node3`
- Flash (local only, needs USB): `pio run -e node1 -t upload --upload-port <port>`
- Monitor all boards: `python tools/multi_monitor.py --csv run.csv`

## Layout
- `include/protocol.h`: wire format, CRC, stats helpers. Must stay free of Arduino includes (it is compiled on the host for tests).
- `src/main.cpp`: firmware. The ESP-NOW callbacks only enqueue; all work and Serial output happen in `loop()`.
- `test/test_protocol/`: Unity tests for `protocol.h`.
- `tools/multi_monitor.py`: host serial aggregator; the `STAT_FIELDS` must match the firmware's STAT line.
- `docs/`: research, test plan, dev setup.

## Conventions
- ESP8266 ESP-NOW API (`espnow.h`), not the ESP32 `esp_now.h`. Max frame 250 bytes.
- When the STAT line format changes, update `main.cpp`, `multi_monitor.py`, README and the docs together.
- Formatting: `.clang-format` (Google, 100 cols).
- The cloud cannot flash hardware; hardware results come from the user.
