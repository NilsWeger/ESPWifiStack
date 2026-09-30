# Development setup: cloud + local, editor, tools

## 1. Having the repository in the cloud and locally

**Recommendation: GitHub is the single source of truth.** Everything else is a clone of it.

```
                 ┌──────────── GitHub: NilsWeger/ESPWifiStack ────────────┐
                 │  branches, PRs, GitHub Actions (build + unit tests)    │
                 └───────▲───────────────────────▲────────────────────────┘
                   push/pull                push/pull
                         │                       │
   Local PC (VS Code + PlatformIO)         Cloud (Claude Code on the web,
   - edit, build                            GitHub Codespaces)
   - flash the boards over USB              - edit, build, unit tests
   - serial monitor, multi_monitor.py       - no USB → no flashing
```

| Where | What it's good for | What it can't do |
|---|---|---|
| **Local**: VS Code + PlatformIO | Everything, including **flashing and serial monitoring** | Nothing missing |
| **Claude Code on the web** | Writing code, refactoring, reviews, unit tests | USB access to the boards |
| **GitHub Actions** (`.github/workflows/build.yml`) | Builds every push for all nodes, runs the tests, provides `firmware.bin` artifacts | USB access |
| **GitHub Codespaces / Dev Container** (`.devcontainer/`) | The same toolchain in the browser or Docker | USB (except with a local Dev Container and USB passthrough on Linux) |

Daily workflow:

1. Work in the cloud (Claude) **on a branch** and push.
2. Locally: `git pull` → `pio run -e node1 -t upload` … → test with the boards.
3. Commit the measurement results or fixes and push, so both sides stay in sync.

Tips:
- Never work on the same branch in the cloud and locally at the same time without pulling first.
- Don't commit `.pio/` (the build output); `.gitignore` already takes care of that.
- The cloud session installs PlatformIO through `.claude/hooks/session-start.sh`.
  **Note:** the ESP8266 toolchain comes from the PlatformIO registry. If the cloud
  environment's network policy blocks `api.registry.platformio.org` /
  `dl.registry.platformio.org`, only `tools/host_test.sh` works there. To fix that,
  add those domains in the environment's network settings. GitHub Actions is not affected.

## 2. Is VS Code + PlatformIO the best choice?

For the **ESP8266 with the Arduino core: yes.**

| Tool | ESP8266 | Verdict |
|---|---|---|
| **VS Code + PlatformIO** | ✅ official `espressif8266` platform (Arduino core 3.1.x) | **Best option.** Multiple envs (one per node), unit tests, CI, exception decoder |
| Arduino IDE 2.x | ✅ | Fine for beginners, but no build envs per node, no unit tests, weak CI |
| ESP-IDF VS Code extension | ❌ ESP8266 not supported (only the deprecated ESP8266_RTOS_SDK) | Only for the ESP32 family |
| pioarduino (PlatformIO fork) | ❌ ESP32 only | Relevant only if you switch to the ESP32 |
| CLion + PlatformIO plugin | ✅ | Good, but paid; no advantage for this project |
| Arduino CLI + Makefile | ✅ | Good for scripting, but more manual work than PlatformIO |

If the project later moves to the ESP32 (ESP-NOW v2, more RAM), consider pioarduino or ESP-IDF.

## 3. Recommended VS Code settings (already in the repo)

`.vscode/extensions.json` (VS Code suggests these when you open the folder):

- **PlatformIO IDE**: build, upload, monitor, tests
- **C/C++** (Microsoft): IntelliSense, fed by PlatformIO
- **EditorConfig**: consistent indentation
- **GitLens**: history and blame
- **Python**: for `tools/multi_monitor.py`
- **Claude Code**: Claude directly in VS Code, on the same repo as the web sessions

`.vscode/settings.json`:

- Format on save with `.clang-format` (Google style, 100 columns)
- `.pio/` hidden from search and the file watcher, which keeps VS Code fast
- `platformio-ide.autoRebuildAutocompleteIndex: false`: no re-index on every save. Run
  *PlatformIO: Rebuild IntelliSense Index* manually after changing `platformio.ini`

`platformio.ini`:

- `monitor_filters = esp8266_exception_decoder, time`: stack traces become readable and every line gets a timestamp
- `upload_speed = 921600`: fast flashing. If uploads fail, use 460800 or 115200
- `board_build.f_cpu = 160000000L`: the ESP8266 at 160 MHz
- `node1_debug` env: a debug build with core Wi-Fi debug output

### Working with several boards

- `pio device list` shows all ports.
- Put a sticker with the node number on each board, and set `upload_port` / `monitor_port` per env in `platformio_local.ini` (copy it from `platformio_local.ini.example`; it is git-ignored, so each PC keeps its own ports).
- **Linux:** stable names through udev rules (e.g. `/dev/esp-node1` by serial number), plus `sudo usermod -aG dialout $USER`.
- **Windows:** fix the COM port per board in Device Manager. You need the CH340 or CP210x driver.
- **macOS:** the ports are `/dev/cu.usbserial-*` or `/dev/cu.wchusbserial*`.
- Watch all nodes at once: `python tools/multi_monitor.py --csv run.csv`.
- Power: a powered USB hub. Weak USB ports cause brownouts during Wi-Fi TX.

## 4. Skills and tools

### Claude Code skills that help here

| Skill / command | Use |
|---|---|
| `session-start-hook` | Already applied: installs PlatformIO in cloud sessions |
| `/code-review` | Review the firmware changes before flashing |
| `/simplify` | Cleans up code after a feature is done |
| `/security-review` | When you add encryption keys or OTA |
| `/init` | Updates `CLAUDE.md` when the project grows |
| `deep-research` | For deeper research questions (e.g. ESP-NOW vs. Wi-Fi mesh) |

### Hardware and debug tools

| Tool | Why |
|---|---|
| 3–5 identical boards (Wemos D1 mini / NodeMCU) | Enough nodes for the many-to-many tests |
| Powered USB hub | Stable power and all nodes on one PC |
| `tools/multi_monitor.py` | All serial outputs in one terminal, plus CSV |
| Spreadsheet / pandas | Evaluate the CSV (loss over distance, RTT histogram) |
| Wi-Fi analyser app | Find a free channel before the test |
| Wireshark + a monitor-mode Wi-Fi adapter | See the ESP-NOW frames on the air (action frames, Espressif OUI `18:FE:34`) |
| Logic analyser (e.g. Saleae clone + PulseView) | Timing measurements with GPIO toggles, if needed |
