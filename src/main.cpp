// ESP-NOW multi-node test for the ESP8266 (Arduino core).
//
// Flash the same firmware to several boards, each with a different NODE_ID
// (see platformio.ini). Every node:
//   1. broadcasts a HELLO beacon so the others learn its MAC address,
//   2. sends a unicast PING to every known peer and answers PINGs with PONG,
//   3. prints one STAT line per peer every REPORT_INTERVAL_MS.
//
// The ESP-NOW callbacks run in the SDK's system context, so they only copy
// the frame into a ring buffer. All processing and Serial output is in loop().

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <espnow.h>

#include "protocol.h"

#ifndef NODE_ID
#define NODE_ID 1
#endif
#ifndef ESPNOW_CHANNEL
#define ESPNOW_CHANNEL 1
#endif
#ifndef PING_INTERVAL_MS
#define PING_INTERVAL_MS 200
#endif
#ifndef HELLO_INTERVAL_MS
#define HELLO_INTERVAL_MS 2000
#endif
#ifndef REPORT_INTERVAL_MS
#define REPORT_INTERVAL_MS 5000
#endif
#ifndef PAYLOAD_SIZE
#define PAYLOAD_SIZE 0
#endif
#ifndef MAX_PEERS
#define MAX_PEERS 8
#endif

using namespace espnow_test;

static_assert(NODE_ID > 0 && NODE_ID < kBroadcastId, "NODE_ID must be 1..254");
static_assert(PAYLOAD_SIZE <= kMaxPayload, "PAYLOAD_SIZE too large for one ESP-NOW frame");

namespace {

uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---------- receive ring buffer (single producer: callback, single consumer: loop) ----------

struct RxFrame {
  uint8_t mac[6];
  uint8_t len;
  uint32_t rxUs;
  uint8_t data[kMaxFrame];
};

constexpr uint8_t kRxSlots = 8;  // power of two
RxFrame rxQueue[kRxSlots];
volatile uint8_t rxHead = 0;
volatile uint8_t rxTail = 0;
volatile uint32_t rxOverflow = 0;

// ---------- peers ----------

struct Peer {
  bool used = false;
  uint8_t id = 0;
  uint8_t mac[6] = {};
  uint32_t pingSeq = 0;   // next PING seq we send to this peer
  uint32_t pingsSent = 0;
  uint32_t sendFail = 0;  // esp_now_send returned an error
  uint32_t noAck = 0;     // send callback reported no MAC-layer ACK
  uint32_t pongsRecv = 0;
  RttStats rtt;           // reset every report interval
  SeqTracker pingRx;      // PINGs this peer sent to us
};

Peer peers[MAX_PEERS];

volatile uint32_t sendCbOk = 0;
volatile uint32_t sendCbFail = 0;

uint32_t badFrames = 0;
uint32_t lastPingMs = 0;
uint32_t lastHelloMs = 0;
uint32_t lastReportMs = 0;
uint32_t helloSeq = 0;

// ---------- ESP-NOW callbacks (keep them short) ----------

void onRecv(uint8_t* mac, uint8_t* data, uint8_t len) {
  const uint32_t now = micros();
  const uint8_t head = rxHead;
  const uint8_t nextHead = (head + 1) & (kRxSlots - 1);
  if (nextHead == rxTail || len > kMaxFrame) {
    ++rxOverflow;
    return;
  }
  RxFrame& f = rxQueue[head];
  memcpy(f.mac, mac, 6);
  memcpy(f.data, data, len);
  f.len = len;
  f.rxUs = now;
  rxHead = nextHead;
}

Peer* findPeerByMac(const uint8_t* mac);

void onSent(uint8_t* mac, uint8_t status) {
  // status 0 = frame was ACKed at MAC level (unicast) or sent (broadcast).
  // On the ESP8266 the SDK runs callbacks between loop() iterations, never
  // in the middle of it, so reading the peer table here is safe.
  if (status == 0) {
    ++sendCbOk;
    return;
  }
  ++sendCbFail;
  if (Peer* p = findPeerByMac(mac)) ++p->noAck;
}

// ---------- helpers ----------

void printMac(const uint8_t* mac) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

Peer* findPeerById(uint8_t id) {
  for (auto& p : peers) {
    if (p.used && p.id == id) return &p;
  }
  return nullptr;
}

Peer* findPeerByMac(const uint8_t* mac) {
  for (auto& p : peers) {
    if (p.used && memcmp(p.mac, mac, 6) == 0) return &p;
  }
  return nullptr;
}

Peer* addPeer(uint8_t id, const uint8_t* mac) {
  for (auto& p : peers) {
    if (p.used) continue;
    uint8_t macCopy[6];
    memcpy(macCopy, mac, 6);
    if (!esp_now_is_peer_exist(macCopy) &&
        esp_now_add_peer(macCopy, ESP_NOW_ROLE_COMBO, ESPNOW_CHANNEL, nullptr, 0) != 0) {
      Serial.println(F("# ERROR esp_now_add_peer failed"));
      return nullptr;
    }
    p = Peer();
    p.used = true;
    p.id = id;
    memcpy(p.mac, mac, 6);
    Serial.printf("# PEER node=%u mac=", id);
    printMac(mac);
    Serial.println();
    return &p;
  }
  Serial.println(F("# ERROR peer table full, raise MAX_PEERS"));
  return nullptr;
}

bool sendFrame(uint8_t* mac, MsgType type, uint8_t dst, uint32_t seq, uint32_t tUs,
               uint16_t payloadLen) {
  uint8_t buf[kMaxFrame];
  const size_t len = encode(type, NODE_ID, dst, seq, tUs, payloadLen, buf);
  if (len == 0) return false;
  return esp_now_send(mac, buf, static_cast<int>(len)) == 0;
}

// ---------- frame handling ----------

void handleFrame(const RxFrame& f) {
  Header h;
  const DecodeError err = decode(f.data, f.len, h);
  if (err != DecodeError::Ok) {
    ++badFrames;
    Serial.printf("# BAD frame from ");
    printMac(f.mac);
    Serial.printf(" len=%u err=%s\n", f.len, toString(err));
    return;
  }
  if (h.src == NODE_ID) {
    Serial.println(F("# WARN another node uses the same NODE_ID"));
    return;
  }
  if (h.dst != NODE_ID && h.dst != kBroadcastId) return;

  Peer* peer = findPeerById(h.src);
  if (!peer) peer = addPeer(h.src, f.mac);
  if (!peer) return;

  switch (static_cast<MsgType>(h.type)) {
    case MsgType::Hello:
      break;
    case MsgType::Ping:
      peer->pingRx.onSeq(h.seq);
      // Echo seq and the sender's timestamp so it can compute the RTT.
      sendFrame(peer->mac, MsgType::Pong, h.src, h.seq, h.tUs, h.payloadLen);
      break;
    case MsgType::Pong:
      ++peer->pongsRecv;
      peer->rtt.add(f.rxUs - h.tUs);
      break;
    default:
      ++badFrames;
      break;
  }
}

void sendPings() {
  for (auto& p : peers) {
    if (!p.used) continue;
    if (sendFrame(p.mac, MsgType::Ping, p.id, p.pingSeq, micros(), PAYLOAD_SIZE)) {
      ++p.pingsSent;
    } else {
      ++p.sendFail;
    }
    ++p.pingSeq;
  }
}

void report(uint32_t nowMs) {
  for (auto& p : peers) {
    if (!p.used) continue;
    const uint32_t loss = lossPermille(p.pingsSent, p.pongsRecv);
    // STAT,ms,self,peer,pings_sent,send_fail,no_ack,pongs,loss_pct,rtt_min_us,rtt_avg_us,rtt_max_us,
    //      pings_rx,pings_rx_lost,pings_rx_ooo
    Serial.printf("STAT,%lu,%u,%u,%lu,%lu,%lu,%lu,%lu.%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
                  static_cast<unsigned long>(nowMs), NODE_ID, p.id,
                  static_cast<unsigned long>(p.pingsSent), static_cast<unsigned long>(p.sendFail),
                  static_cast<unsigned long>(p.noAck),
                  static_cast<unsigned long>(p.pongsRecv), static_cast<unsigned long>(loss / 10),
                  static_cast<unsigned long>(loss % 10),
                  static_cast<unsigned long>(p.rtt.minOrZero()),
                  static_cast<unsigned long>(p.rtt.avgUs()),
                  static_cast<unsigned long>(p.rtt.maxUs),
                  static_cast<unsigned long>(p.pingRx.received),
                  static_cast<unsigned long>(p.pingRx.lost),
                  static_cast<unsigned long>(p.pingRx.outOfOrder));
    p.rtt.reset();
  }
  Serial.printf("# INFO heap=%u rx_overflow=%lu bad_frames=%lu send_cb_ok=%lu send_cb_fail=%lu\n",
                ESP.getFreeHeap(), static_cast<unsigned long>(rxOverflow),
                static_cast<unsigned long>(badFrames), static_cast<unsigned long>(sendCbOk),
                static_cast<unsigned long>(sendCbFail));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.printf("# BOOT node=%u channel=%u payload=%u ping_ms=%u mac=%s\n", NODE_ID,
                ESPNOW_CHANNEL, PAYLOAD_SIZE, PING_INTERVAL_MS, WiFi.macAddress().c_str());

  // ESP-NOW needs the radio on but not connected to an AP; all nodes must
  // share the same channel.
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(ESPNOW_CHANNEL);

  if (esp_now_init() != 0) {
    Serial.println(F("# ERROR esp_now_init failed, restarting"));
    delay(1000);
    ESP.restart();
  }
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);
  esp_now_add_peer(kBroadcastMac, ESP_NOW_ROLE_COMBO, ESPNOW_CHANNEL, nullptr, 0);

  Serial.println(
      F("# STAT,ms,self,peer,pings_sent,send_fail,no_ack,pongs,loss_pct,rtt_min_us,rtt_avg_us,"
        "rtt_max_us,pings_rx,pings_rx_lost,pings_rx_ooo"));
}

void loop() {
  while (rxTail != rxHead) {
    handleFrame(rxQueue[rxTail]);
    rxTail = (rxTail + 1) & (kRxSlots - 1);
  }

  const uint32_t nowMs = millis();
  if (nowMs - lastHelloMs >= HELLO_INTERVAL_MS) {
    lastHelloMs = nowMs;
    sendFrame(kBroadcastMac, MsgType::Hello, kBroadcastId, helloSeq++, micros(), 0);
  }
  if (nowMs - lastPingMs >= PING_INTERVAL_MS) {
    lastPingMs = nowMs;
    sendPings();
  }
  if (nowMs - lastReportMs >= REPORT_INTERVAL_MS) {
    lastReportMs = nowMs;
    report(nowMs);
  }
}
