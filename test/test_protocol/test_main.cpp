// Unit tests for include/protocol.h. Run on the host with `pio test -e native`
// (they also run on a board with `pio test -e node1`).
#include <unity.h>

#include "protocol.h"

using namespace espnow_test;

void setUp() {}
void tearDown() {}

void test_crc16_reference_vector() {
  const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16(data, sizeof(data)));
}

void test_roundtrip_without_payload() {
  uint8_t buf[kMaxFrame];
  const size_t len = encode(MsgType::Ping, 3, 7, 42, 123456, 0, buf);
  TEST_ASSERT_EQUAL(sizeof(Header), len);

  Header h;
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeError::Ok), static_cast<int>(decode(buf, len, h)));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MsgType::Ping), h.type);
  TEST_ASSERT_EQUAL(3, h.src);
  TEST_ASSERT_EQUAL(7, h.dst);
  TEST_ASSERT_EQUAL_UINT32(42, h.seq);
  TEST_ASSERT_EQUAL_UINT32(123456, h.tUs);
}

void test_roundtrip_with_max_payload() {
  uint8_t buf[kMaxFrame];
  const size_t len = encode(MsgType::Pong, 1, 2, 9, 0, kMaxPayload, buf);
  TEST_ASSERT_EQUAL(kMaxFrame, len);
  Header h;
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeError::Ok), static_cast<int>(decode(buf, len, h)));
  TEST_ASSERT_EQUAL(kMaxPayload, h.payloadLen);
}

void test_encode_rejects_oversized_payload() {
  uint8_t buf[kMaxFrame + 1];
  TEST_ASSERT_EQUAL(0, encode(MsgType::Ping, 1, 2, 0, 0, kMaxPayload + 1, buf));
}

void test_decode_detects_errors() {
  uint8_t buf[kMaxFrame];
  const size_t len = encode(MsgType::Ping, 1, 2, 5, 0, 16, buf);
  Header h;

  TEST_ASSERT_EQUAL(static_cast<int>(DecodeError::TooShort),
                    static_cast<int>(decode(buf, sizeof(Header) - 1, h)));
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeError::BadLength),
                    static_cast<int>(decode(buf, len - 1, h)));

  buf[len - 1] ^= 0x01;  // flip a payload bit
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeError::BadCrc), static_cast<int>(decode(buf, len, h)));
  buf[len - 1] ^= 0x01;

  buf[0] ^= 0xFF;  // break the magic
  TEST_ASSERT_EQUAL(static_cast<int>(DecodeError::BadMagic), static_cast<int>(decode(buf, len, h)));
}

void test_seq_tracker_counts_gaps_and_reordering() {
  SeqTracker t;
  t.onSeq(10);
  t.onSeq(11);
  t.onSeq(14);  // 12 and 13 missing
  TEST_ASSERT_EQUAL_UINT32(3, t.received);
  TEST_ASSERT_EQUAL_UINT32(2, t.lost);

  t.onSeq(12);  // late arrival
  TEST_ASSERT_EQUAL_UINT32(1, t.lost);
  TEST_ASSERT_EQUAL_UINT32(1, t.outOfOrder);
}

void test_seq_tracker_handles_wraparound() {
  SeqTracker t;
  t.onSeq(0xFFFFFFFE);
  t.onSeq(0xFFFFFFFF);
  t.onSeq(0);
  TEST_ASSERT_EQUAL_UINT32(0, t.lost);
  TEST_ASSERT_EQUAL_UINT32(0, t.outOfOrder);
}

void test_rtt_stats() {
  RttStats r;
  TEST_ASSERT_EQUAL_UINT32(0, r.avgUs());
  TEST_ASSERT_EQUAL_UINT32(0, r.minOrZero());
  r.add(1000);
  r.add(3000);
  TEST_ASSERT_EQUAL_UINT32(1000, r.minUs);
  TEST_ASSERT_EQUAL_UINT32(3000, r.maxUs);
  TEST_ASSERT_EQUAL_UINT32(2000, r.avgUs());
  r.reset();
  TEST_ASSERT_EQUAL_UINT32(0, r.count);
}

void test_loss_permille() {
  TEST_ASSERT_EQUAL_UINT32(0, lossPermille(0, 0));
  TEST_ASSERT_EQUAL_UINT32(0, lossPermille(100, 100));
  TEST_ASSERT_EQUAL_UINT32(25, lossPermille(200, 195));
  TEST_ASSERT_EQUAL_UINT32(1000, lossPermille(10, 0));
}

int runTests() {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_reference_vector);
  RUN_TEST(test_roundtrip_without_payload);
  RUN_TEST(test_roundtrip_with_max_payload);
  RUN_TEST(test_encode_rejects_oversized_payload);
  RUN_TEST(test_decode_detects_errors);
  RUN_TEST(test_seq_tracker_counts_gaps_and_reordering);
  RUN_TEST(test_seq_tracker_handles_wraparound);
  RUN_TEST(test_rtt_stats);
  RUN_TEST(test_loss_permille);
  return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
  delay(2000);  // give the serial monitor time to attach
  runTests();
}
void loop() {}
#else
int main() { return runTests(); }
#endif
