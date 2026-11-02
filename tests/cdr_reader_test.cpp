#include "cdr/cdr_reader.hpp"
#include "cdr_writer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace rrd_convert {
namespace {

TEST(CdrReader, RejectsATruncatedEncapsulation) {
  const uint8_t bytes[] = {0x00, 0x01, 0x00};
  const CdrReader reader(bytes, sizeof(bytes));
  EXPECT_FALSE(reader.ok());
  EXPECT_NE(reader.error().find("truncated"), std::string::npos);
}

TEST(CdrReader, RejectsAnUnknownEncapsulation) {
  const uint8_t bytes[] = {0x00, 0x11, 0x00, 0x00};
  const CdrReader reader(bytes, sizeof(bytes));
  EXPECT_FALSE(reader.ok());
  EXPECT_NE(reader.error().find("unknown cdr encapsulation"), std::string::npos);
}

TEST(CdrReader, ReadsLittleEndianIntegersWithAlignment) {
  const uint8_t bytes[] = {
      0x00, 0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01,
  };
  CdrReader reader(bytes, sizeof(bytes));
  uint32_t value = 0;
  ASSERT_TRUE(reader.ok());
  ASSERT_TRUE(reader.read_u32(&value));
  EXPECT_EQ(value, 0x01020304u);
  EXPECT_EQ(reader.offset(), 8u);
}

TEST(CdrReader, PadsBeforeAFourByteValue) {
  // uint8, then three padding bytes, then uint32. 4-byte alignment is the
  // same from the header or from the first payload byte, so the uint32
  // begins at offset 8.
  const uint8_t bytes[] = {
      0x00, 0x01, 0x00, 0x00, 0xab, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
  };
  CdrReader reader(bytes, sizeof(bytes));
  uint8_t small = 0;
  uint32_t wide = 0;
  ASSERT_TRUE(reader.read_u8(&small));
  ASSERT_TRUE(reader.read_u32(&wide));
  EXPECT_EQ(small, 0xabu);
  EXPECT_EQ(wide, 7u);
  EXPECT_EQ(reader.offset(), 12u);
}

TEST(CdrReader, ReadsAnEightByteValueAtTheFirstPayloadByte) {
  const uint8_t bytes[] = {
      0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  CdrReader reader(bytes, sizeof(bytes));
  uint64_t value = 0;
  ASSERT_TRUE(reader.read_u64(&value));
  EXPECT_EQ(value, 1u);
  EXPECT_EQ(reader.offset(), 12u);
}

TEST(CdrReader, PadsAnEightByteValueAfterAByte) {
  // Payload position 1 pads to position 8, which is buffer offset 12.
  const uint8_t bytes[] = {
      0x00, 0x01, 0x00, 0x00, 0xab, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  CdrReader reader(bytes, sizeof(bytes));
  uint8_t small = 0;
  uint64_t value = 0;
  ASSERT_TRUE(reader.read_u8(&small));
  ASSERT_TRUE(reader.read_u64(&value));
  EXPECT_EQ(small, 0xabu);
  EXPECT_EQ(value, 1u);
  EXPECT_EQ(reader.offset(), 20u);
}

TEST(CdrReader, ReadsASignedIntegerAndFloat) {
  const uint8_t bytes[] = {
      0x00, 0x01, 0x00, 0x00, 0xfe, 0xff, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,
  };
  CdrReader reader(bytes, sizeof(bytes));
  int16_t narrow = 0;
  float wide = 0.f;
  ASSERT_TRUE(reader.read_i16(&narrow));
  ASSERT_TRUE(reader.read_f32(&wide));
  EXPECT_EQ(narrow, -2);
  EXPECT_FLOAT_EQ(wide, 1.f);
}

TEST(CdrReader, ReadsADoubleAtTheFirstPayloadByte) {
  const uint8_t bytes[] = {
      0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
  };
  CdrReader reader(bytes, sizeof(bytes));
  double value = 0.0;
  ASSERT_TRUE(reader.read_f64(&value));
  EXPECT_DOUBLE_EQ(value, 1.0);
  EXPECT_EQ(reader.offset(), 12u);
}

TEST(CdrReader, ReadsBigEndianValues) {
  const uint8_t bytes[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04};
  CdrReader reader(bytes, sizeof(bytes));
  uint32_t value = 0;
  ASSERT_TRUE(reader.read_u32(&value));
  EXPECT_EQ(value, 0x01020304u);
}

TEST(CdrReader, ReadsXcdr1StringsIncludingAnEmptyOne) {
  const uint8_t text[] = {
      0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 'h', 'i', 0x00,
  };
  CdrReader reader(text, sizeof(text));
  std::string value;
  ASSERT_TRUE(reader.read_string(&value));
  EXPECT_EQ(value, "hi");

  const uint8_t empty[] = {0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
  CdrReader empty_reader(empty, sizeof(empty));
  ASSERT_TRUE(empty_reader.read_string(&value));
  EXPECT_EQ(value, "");
}

TEST(CdrReader, AlignsAStringLength) {
  const uint8_t bytes[] = {
      0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
      0x02, 0x00, 0x00, 0x00, 'a',  0x00,
  };
  CdrReader reader(bytes, sizeof(bytes));
  uint8_t flag = 0;
  std::string value;
  ASSERT_TRUE(reader.read_u8(&flag));
  ASSERT_TRUE(reader.read_string(&value));
  EXPECT_EQ(flag, 1u);
  EXPECT_EQ(value, "a");
}

TEST(CdrReader, RejectsAStringThatIsNotNullTerminated) {
  const uint8_t bytes[] = {
      0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 'a', 'b',
  };
  CdrReader reader(bytes, sizeof(bytes));
  std::string value;
  EXPECT_FALSE(reader.read_string(&value));
  EXPECT_NE(reader.error().find("null-terminated"), std::string::npos);
}

TEST(CdrReader, RejectsAZeroStringLength) {
  const uint8_t bytes[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  CdrReader reader(bytes, sizeof(bytes));
  std::string value;
  EXPECT_FALSE(reader.read_string(&value));
  EXPECT_NE(reader.error().find("string length"), std::string::npos);
}

TEST(CdrReader, ReadsASequenceThenItsElements) {
  const uint8_t bytes[] = {
      0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00,
  };
  CdrReader reader(bytes, sizeof(bytes));
  uint32_t count = 0;
  uint16_t first = 0;
  uint16_t second = 0;
  ASSERT_TRUE(reader.read_sequence_length(&count));
  ASSERT_EQ(count, 2u);
  ASSERT_TRUE(reader.read_u16(&first));
  ASSERT_TRUE(reader.read_u16(&second));
  EXPECT_EQ(first, 1u);
  EXPECT_EQ(second, 2u);
}

TEST(CdrReader, AlignsASequenceLengthAndItsElements) {
  const uint8_t bytes[] = {
      0x00, 0x01, 0x00, 0x00, 0xaa, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
  };
  CdrReader reader(bytes, sizeof(bytes));
  uint8_t lead = 0;
  uint32_t count = 0;
  uint32_t element = 0;
  ASSERT_TRUE(reader.read_u8(&lead));
  ASSERT_TRUE(reader.read_sequence_length(&count));
  ASSERT_TRUE(reader.read_u32(&element));
  EXPECT_EQ(lead, 0xaau);
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(element, 0x12345678u);
}

TEST(CdrReader, BooleanMustBeZeroOrOne) {
  const uint8_t bytes[] = {0x00, 0x01, 0x00, 0x00, 0x01, 0x02};
  CdrReader reader(bytes, sizeof(bytes));
  bool value = false;
  ASSERT_TRUE(reader.read_bool(&value));
  EXPECT_TRUE(value);
  EXPECT_FALSE(reader.read_bool(&value));
  EXPECT_NE(reader.error().find("boolean"), std::string::npos);
  uint8_t ignored = 0;
  EXPECT_FALSE(reader.read_u8(&ignored));
}

TEST(CdrReader, TruncatedValueStaysFailed) {
  const uint8_t bytes[] = {0x00, 0x01, 0x00, 0x00, 0x01, 0x00};
  CdrReader reader(bytes, sizeof(bytes));
  uint32_t value = 0;
  EXPECT_FALSE(reader.read_u32(&value));
  EXPECT_NE(reader.error().find("truncated"), std::string::npos);
  EXPECT_FALSE(reader.read_u8(reinterpret_cast<uint8_t*>(&value)));
}

TEST(CdrWriter, MatchesThePlainCdrLayout) {
  CdrWriter writer;
  writer.write_u8(0xab);
  writer.write_u32(7);
  writer.write_string("a");
  const std::vector<uint8_t> expected = {
      0x00, 0x01, 0x00, 0x00, 0xab, 0x00, 0x00, 0x00, 0x07, 0x00,
      0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 'a',  0x00,
  };
  EXPECT_EQ(writer.bytes(), expected);
}

TEST(CdrReader, RoundTripsMixedValues) {
  CdrWriter writer(false);
  writer.write_i32(-5);
  writer.write_f64(1.5);
  writer.write_string("");
  writer.write_sequence_length(2);
  writer.write_u8(9);
  writer.write_u8(8);
  writer.write_i8(-3);
  writer.write_u16(0x1234);

  CdrReader reader(writer.bytes().data(), writer.bytes().size());
  int32_t whole = 0;
  double wide = 0.0;
  std::string text;
  uint32_t count = 0;
  uint8_t first = 0;
  uint8_t second = 0;
  int8_t narrow = 0;
  uint16_t half = 0;
  ASSERT_TRUE(reader.read_i32(&whole));
  ASSERT_TRUE(reader.read_f64(&wide));
  ASSERT_TRUE(reader.read_string(&text));
  ASSERT_TRUE(reader.read_sequence_length(&count));
  ASSERT_TRUE(reader.read_u8(&first));
  ASSERT_TRUE(reader.read_u8(&second));
  ASSERT_TRUE(reader.read_i8(&narrow));
  ASSERT_TRUE(reader.read_u16(&half));
  EXPECT_EQ(whole, -5);
  EXPECT_DOUBLE_EQ(wide, 1.5);
  EXPECT_EQ(text, "");
  EXPECT_EQ(count, 2u);
  EXPECT_EQ(first, 9u);
  EXPECT_EQ(second, 8u);
  EXPECT_EQ(narrow, -3);
  EXPECT_EQ(half, 0x1234u);
  EXPECT_TRUE(reader.ok());
}

}  // namespace
}  // namespace rrd_convert
