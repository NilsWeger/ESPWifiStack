// Wire format and bookkeeping for the ESP-NOW multi-node test.
//
// This header has no Arduino dependency on purpose: it is compiled into the
// firmware and into the host unit tests (`pio test -e native`).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace espnow_test {

constexpr uint16_t kMagic = 0xE5A1;
constexpr uint8_t kVersion = 1;
constexpr uint8_t kBroadcastId = 0xFF;

// ESP-NOW on the ESP8266 carries at most 250 bytes of user data per frame.
constexpr size_t kMaxFrame = 250;

enum class MsgType : uint8_t {
  Hello = 1,  // broadcast discovery beacon
  Ping = 2,   // unicast, answered with Pong
  Pong = 3,   // echoes seq and t_us of the Ping
};

struct __attribute__((packed)) Header {
  uint16_t magic;
  uint8_t version;
  uint8_t type;         // MsgType
  uint8_t src;          // sender node id
  uint8_t dst;          // receiver node id or kBroadcastId
  uint16_t payloadLen;  // bytes following the header
  uint32_t seq;         // per-sender, per-peer sequence number
  uint32_t tUs;         // sender timestamp (micros) for RTT
  uint16_t crc;         // CRC16 over header (crc = 0) + payload
};

static_assert(sizeof(Header) == 18, "Header must stay 18 bytes on the wire");

constexpr size_t kMaxPayload = kMaxFrame - sizeof(Header);

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). "123456789" -> 0x29B1.
inline uint16_t crc16(const uint8_t* data, size_t len, uint16_t crc = 0xFFFF) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; ++b) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

// Deterministic filler so the receiver can verify payload integrity.
inline uint8_t payloadByte(uint32_t seq, size_t i) {
  return static_cast<uint8_t>((seq + i * 31u) & 0xFF);
}

// Builds a frame into `out` (at least sizeof(Header) + payloadLen bytes).
// Returns the frame length, or 0 if payloadLen is too large.
inline size_t encode(MsgType type, uint8_t src, uint8_t dst, uint32_t seq,
                     uint32_t tUs, uint16_t payloadLen, uint8_t* out) {
  if (payloadLen > kMaxPayload) return 0;
  Header h;
  h.magic = kMagic;
  h.version = kVersion;
  h.type = static_cast<uint8_t>(type);
  h.src = src;
  h.dst = dst;
  h.payloadLen = payloadLen;
  h.seq = seq;
  h.tUs = tUs;
  h.crc = 0;
  memcpy(out, &h, sizeof(h));
  for (size_t i = 0; i < payloadLen; ++i) out[sizeof(h) + i] = payloadByte(seq, i);
  const size_t len = sizeof(h) + payloadLen;
  const uint16_t crc = crc16(out, len);
  memcpy(out + offsetof(Header, crc), &crc, sizeof(crc));
  return len;
}

enum class DecodeError : uint8_t { Ok, TooShort, BadMagic, BadVersion, BadLength, BadCrc };

// Validates a received frame and copies its header into `h`.
// Works on a local copy, so `data` is left untouched.
inline DecodeError decode(const uint8_t* data, size_t len, Header& h) {
  if (len < sizeof(Header)) return DecodeError::TooShort;
  memcpy(&h, data, sizeof(h));
  if (h.magic != kMagic) return DecodeError::BadMagic;
  if (h.version != kVersion) return DecodeError::BadVersion;
  if (sizeof(Header) + h.payloadLen != len) return DecodeError::BadLength;

  const uint16_t zero = 0;
  uint16_t crc = crc16(data, offsetof(Header, crc));
  crc = crc16(reinterpret_cast<const uint8_t*>(&zero), sizeof(zero), crc);
  crc = crc16(data + sizeof(Header), h.payloadLen, crc);
  if (crc != h.crc) return DecodeError::BadCrc;
  return DecodeError::Ok;
}

inline const char* toString(DecodeError e) {
  switch (e) {
    case DecodeError::Ok: return "ok";
    case DecodeError::TooShort: return "too-short";
    case DecodeError::BadMagic: return "bad-magic";
    case DecodeError::BadVersion: return "bad-version";
    case DecodeError::BadLength: return "bad-length";
    case DecodeError::BadCrc: return "bad-crc";
  }
  return "?";
}

// Tracks incoming sequence numbers from one sender to count gaps.
// A late frame (seq below the expected one) is counted as out-of-order and
// takes back one previously counted loss.
struct SeqTracker {
  bool started = false;
  uint32_t next = 0;
  uint32_t received = 0;
  uint32_t lost = 0;
  uint32_t outOfOrder = 0;

  void onSeq(uint32_t seq) {
    ++received;
    if (!started) {
      started = true;
      next = seq + 1;
      return;
    }
    const int32_t delta = static_cast<int32_t>(seq - next);
    if (delta >= 0) {
      lost += static_cast<uint32_t>(delta);
      next = seq + 1;
    } else {
      ++outOfOrder;
      if (lost > 0) --lost;
    }
  }
};

// Round-trip time statistics in microseconds.
struct RttStats {
  uint32_t count = 0;
  uint32_t minUs = UINT32_MAX;
  uint32_t maxUs = 0;
  uint64_t sumUs = 0;

  void add(uint32_t us) {
    ++count;
    if (us < minUs) minUs = us;
    if (us > maxUs) maxUs = us;
    sumUs += us;
  }
  uint32_t avgUs() const { return count ? static_cast<uint32_t>(sumUs / count) : 0; }
  uint32_t minOrZero() const { return count ? minUs : 0; }
  void reset() { *this = RttStats(); }
};

// Loss in per-mille (0..1000) so it prints without floating point.
inline uint32_t lossPermille(uint32_t sent, uint32_t answered) {
  if (sent == 0 || answered >= sent) return 0;
  return static_cast<uint32_t>((static_cast<uint64_t>(sent - answered) * 1000u) / sent);
}

}  // namespace espnow_test
