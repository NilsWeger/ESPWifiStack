# Research: ESP-NOW between several ESP8266 boards

Goal: check that several ESP8266 chips can talk to each other directly over
Wi-Fi, without a router, and measure how well it works (loss, latency, range).

## 1. What ESP-NOW is

ESP-NOW is Espressif's connectionless protocol on top of the 802.11 MAC layer.
Frames are sent as *vendor-specific action frames*, so there is:

- **no access point, no association, no IP stack**: a node sends straight to a MAC address,
- **very low latency**: typically a few milliseconds round trip,
- **a MAC-layer ACK for unicast**: the send callback tells you whether the peer received the frame,
- **no ACK for broadcast** (`FF:FF:FF:FF:FF:FF`): fire and forget.

| Property | ESP8266 (ESP-NOW v1) |
|---|---|
| Max payload per frame | **250 bytes** |
| Peers (total) | **< 20**, encrypted peers included |
| Encrypted peers | **10** in Station mode, **6** in SoftAP / SoftAP+Station mode |
| Encryption | CCMP, with a 16-byte key per peer (LMK) and an optional master key (KOK) |
| Roles (ESP8266 only) | `IDLE`, `CONTROLLER`, `SLAVE`, `COMBO` |
| Channel | Sender and receiver must be on the **same Wi-Fi channel** |

ESP-IDF ≥ 5.4 added **ESP-NOW v2** on the ESP32 family, with frames of up to 1470 bytes.
v2 devices still receive v1 frames, and v1 devices (the ESP8266) receive v2 frames
only when they are ≤ 250 bytes. Mixed ESP8266/ESP32 networks therefore have to stay at ≤ 250 bytes.

## 2. The ESP8266 Arduino API (`#include <espnow.h>`)

It is a thin C wrapper around the NONOS SDK, and **different from the ESP32 API** (`esp_now.h`):

```c
int  esp_now_init(void);
int  esp_now_set_self_role(u8 role);                      // ESP8266 only
int  esp_now_add_peer(u8 *mac, u8 role, u8 channel, u8 *key, u8 key_len);
int  esp_now_register_recv_cb(void (*cb)(u8 *mac, u8 *data, u8 len));
int  esp_now_register_send_cb(void (*cb)(u8 *mac, u8 status));   // status 0 = OK
int  esp_now_send(u8 *da, u8 *data, int len);              // da = NULL -> all peers
int  esp_now_is_peer_exist(u8 *mac);
int  esp_now_get_cnt_info(u8 *all_cnt, u8 *encrypt_cnt);
```

Return values: `0` means success. The ESP32 uses `esp_err_t`, `esp_now_peer_info_t` and
different callback signatures, so code does **not** port 1:1. Libraries such as
QuickEspNow hide this difference.

Sources: the [`espnow.h` in esp8266/Arduino](https://github.com/esp8266/Arduino/blob/master/tools/sdk/include/espnow.h)
and the [ESP8266_RTOS_SDK `esp_now.h`](https://github.com/espressif/ESP8266_RTOS_SDK/blob/master/components/esp8266/include/esp_now.h).

## 3. Pitfalls (and how this repo handles them)

| Pitfall | Effect | In this repo |
|---|---|---|
| Nodes on different channels | Nothing arrives | `ESPNOW_CHANNEL` is shared by all nodes and set with `wifi_set_channel()` |
| Wi-Fi not in STA mode / still connected | `esp_now_init` fails or the channel hops | `WiFi.mode(WIFI_STA); WiFi.disconnect();` |
| STA connected to a router at the same time | The router picks the channel, and it can change | Not used. If you need it, put all nodes on the router's channel |
| Heavy work in the callbacks (`Serial`, `delay`, `malloc`) | Watchdog resets, lost frames | The callbacks only copy into a ring buffer, and `loop()` processes it |
| Sending faster than the air/ACK allows | Send failures, `no_ack` goes up | Configurable `PING_INTERVAL_MS`, and `send_fail`/`no_ack` are counted |
| Peer not added before a unicast send | `esp_now_send` returns an error | Peers are added automatically when their HELLO broadcast arrives |
| Duplicate `NODE_ID` | Statistics get mixed up | The firmware prints `# WARN another node uses the same NODE_ID` |
| `WiFi.persistent(true)` (the default) | The flash is written on every boot | `WiFi.persistent(false)` |
| Sender and receiver disagree on the frame layout | Garbage data | A packed header with magic, version, length and CRC16 |

## 4. Libraries

| Option | ESP8266 | ESP32 | Comment |
|---|---|---|---|
| Raw `espnow.h` (used here) | ✅ | ❌ (different API) | Full control and visibility into every counter, which is best for testing the stack itself |
| [QuickEspNow](https://github.com/gmag11/QuickEspNow) | ✅ | ✅ | One API for both chips, with a send queue |
| [WifiEspNow](https://github.com/yoursunny/WifiEspNow) | ✅ | ✅ | A simple C++ wrapper |
| painlessMesh | ✅ | ✅ | Not ESP-NOW: a mesh over a normal Wi-Fi AP/STA with TCP, and heavier |

Recommendation: keep the raw API for the tests, because it hides nothing. Once the link
behaviour is understood, a wrapper like QuickEspNow is useful for mixed ESP8266/ESP32 applications.

## 5. Test plan

The firmware in `src/main.cpp` measures the basics. The rest is done by changing
build flags in `platformio.ini` and the physical setup.

| # | Test | How | Pass criterion (starting point) |
|---|---|---|---|
| T1 | Discovery | Power on 2–3 nodes | Each node prints `# PEER` for every other node within ~2 s |
| T2 | Bidirectional ping/pong | Default settings, boards 1 m apart | `loss_pct` < 1 %, `rtt_avg_us` of a few ms |
| T3 | Payload size | `-DPAYLOAD_SIZE=0 / 100 / 232` (232 = max) | RTT grows with size, loss stays low |
| T4 | Load | `-DPING_INTERVAL_MS=50 / 20 / 10` | Find the point where `no_ack` / `rx_overflow` start rising |
| T5 | Many nodes | 3, then 5+ nodes, all pinging each other | Loss per link stays low, and no node resets |
| T6 | Range / obstacles | Move one board away, through walls | Record the distance where loss exceeds 5 % |
| T7 | Channel | Change `ESPNOW_CHANNEL` (1, 6, 11) near busy Wi-Fi | Compare loss and RTT |
| T8 | Robustness | Reset or unplug a node during the test | The others keep running, and the node re-joins after boot |
| T9 | Encryption (next step) | Add peers with a 16-byte key | Same results, and a node with a wrong key receives nothing |
| T10 | Long run | Run for hours with `--csv` | No resets, and `heap` stays stable |

Output format (one line per peer, every `REPORT_INTERVAL_MS`):

```
STAT,ms,self,peer,pings_sent,send_fail,no_ack,pongs,loss_pct,rtt_min_us,rtt_avg_us,rtt_max_us,pings_rx,pings_rx_lost,pings_rx_ooo
```

- `loss_pct` is pings without a pong (cumulative). The one ping still in flight can show up as a small loss.
- `rtt_*` covers only the last report interval.
- `pings_rx_lost` counts gaps in the PING sequence numbers received from that peer, which is the reverse direction.

## 6. Limits of testing without hardware

- There is no usable simulator for **multi-node ESP-NOW on the ESP8266**. Wokwi focuses on the ESP32 family, and QEMU has no ESP8266 radio.
- What *can* run in the cloud/CI: compiling the firmware and the protocol unit tests (`pio test -e native`).
- The radio behaviour (loss, RTT, range) can only be measured with real boards, 2 at minimum and ideally 3–5.

## 7. Sources

- [ESP-NOW user guide / FAQ (Espressif)](https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/esp-now.html)
- [ESP-IDF ESP-NOW API (v1/v2 compatibility)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html)
- [esp8266/Arduino core](https://github.com/esp8266/Arduino) and [PlatformIO espressif8266 platform](https://docs.platformio.org/en/latest/platforms/espressif8266.html)
- [Random Nerd Tutorials: ESP-NOW with the ESP8266](https://randomnerdtutorials.com/esp-now-esp8266-nodemcu-arduino-ide/)
- [ESP-IDF issue on encrypted peer limits](https://github.com/espressif/esp-idf/issues/11697)
