#include "cdr_writer.hpp"

#include <cstring>
#include <string>

namespace rrd_convert {

CdrWriter::CdrWriter(bool little_endian) : little_endian_(little_endian) {
  bytes_.push_back(0x00);
  bytes_.push_back(little_endian_ ? 0x01 : 0x00);
  bytes_.push_back(0x00);
  bytes_.push_back(0x00);
}

void CdrWriter::align_to(size_t alignment) {
  // Same origin as CdrReader: the first byte after the encapsulation header.
  constexpr size_t kOrigin = 4;
  const size_t pos = bytes_.size() - kOrigin;
  const size_t aligned = (pos + alignment - 1) & ~(alignment - 1);
  bytes_.resize(kOrigin + aligned, 0);
}

void CdrWriter::write_raw(size_t alignment, const uint8_t* data, size_t count) {
  align_to(alignment);
  if (little_endian_) {
    bytes_.insert(bytes_.end(), data, data + count);
    return;
  }
  for (size_t i = count; i > 0; --i) {
    bytes_.push_back(data[i - 1]);
  }
}

void CdrWriter::write_bool(bool value) { write_u8(value ? 1 : 0); }

void CdrWriter::write_u8(uint8_t value) { write_raw(1, &value, 1); }

void CdrWriter::write_i8(int8_t value) {
  write_u8(static_cast<uint8_t>(value));
}

void CdrWriter::write_u16(uint16_t value) {
  const uint8_t raw[2] = {
      static_cast<uint8_t>(value),
      static_cast<uint8_t>(value >> 8),
  };
  write_raw(2, raw, 2);
}

void CdrWriter::write_u32(uint32_t value) {
  const uint8_t raw[4] = {
      static_cast<uint8_t>(value),
      static_cast<uint8_t>(value >> 8),
      static_cast<uint8_t>(value >> 16),
      static_cast<uint8_t>(value >> 24),
  };
  write_raw(4, raw, 4);
}

void CdrWriter::write_i32(int32_t value) {
  write_u32(static_cast<uint32_t>(value));
}

void CdrWriter::write_u64(uint64_t value) {
  uint8_t raw[8];
  for (int i = 0; i < 8; ++i) {
    raw[i] = static_cast<uint8_t>(value >> (8 * i));
  }
  write_raw(8, raw, 8);
}

void CdrWriter::write_f32(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  write_u32(bits);
}

void CdrWriter::write_f64(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  write_u64(bits);
}

void CdrWriter::write_string(const std::string& value) {
  write_u32(static_cast<uint32_t>(value.size() + 1));
  bytes_.insert(bytes_.end(), value.begin(), value.end());
  bytes_.push_back(0);
}

void CdrWriter::write_sequence_length(uint32_t length) { write_u32(length); }

void CdrWriter::write_bytes(const uint8_t* data, size_t count) {
  bytes_.insert(bytes_.end(), data, data + count);
}

const std::vector<uint8_t>& CdrWriter::bytes() const { return bytes_; }

}  // namespace rrd_convert
