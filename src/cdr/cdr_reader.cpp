#include "cdr/cdr_reader.hpp"

#include <cstring>
#include <string>

namespace rrd_convert {

CdrReader::CdrReader(const uint8_t* data, size_t size)
    : data_(data), size_(size), offset_(0), little_endian_(true) {
  if (data_ == nullptr) {
    error_ = "cdr buffer is null";
    return;
  }
  if (size_ < 4) {
    error_ = "cdr encapsulation is truncated";
    return;
  }
  if (data_[0] != 0x00 || (data_[1] != 0x00 && data_[1] != 0x01)) {
    error_ = "unknown cdr encapsulation";
    return;
  }
  little_endian_ = data_[1] == 0x01;
  offset_ = 4;
}

bool CdrReader::ok() const { return error_.empty(); }

const std::string& CdrReader::error() const { return error_; }

size_t CdrReader::offset() const { return offset_; }

bool CdrReader::fail(const std::string& error) {
  if (error_.empty()) {
    error_ = error;
  }
  return false;
}

bool CdrReader::align_to(size_t alignment) {
  if (!ok()) {
    return false;
  }
  // ROS 2 bags align from the first payload byte. An 8-byte field can
  // therefore start at an offset that is 4 modulo 8.
  constexpr size_t kOrigin = 4;
  if (offset_ < kOrigin || alignment == 0) {
    return fail("cdr truncated aligning to " + std::to_string(alignment));
  }
  const size_t pos = offset_ - kOrigin;
  const size_t aligned = (pos + alignment - 1) & ~(alignment - 1);
  const size_t next = kOrigin + aligned;
  if (next > size_) {
    return fail("cdr truncated aligning to " + std::to_string(alignment));
  }
  offset_ = next;
  return true;
}

bool CdrReader::read_raw(size_t alignment, size_t count, uint8_t* out) {
  if (!align_to(alignment)) {
    return false;
  }
  if (count > size_ - offset_) {
    return fail("cdr truncated");
  }
  if (little_endian_) {
    std::memcpy(out, data_ + offset_, count);
  } else {
    for (size_t i = 0; i < count; ++i) {
      out[i] = data_[offset_ + count - 1 - i];
    }
  }
  offset_ += count;
  return true;
}

bool CdrReader::read_u8(uint8_t* out) { return read_raw(1, 1, out); }

bool CdrReader::read_i8(int8_t* out) {
  uint8_t value = 0;
  if (!read_u8(&value)) {
    return false;
  }
  *out = static_cast<int8_t>(value);
  return true;
}

bool CdrReader::read_bool(bool* out) {
  uint8_t value = 0;
  if (!read_u8(&value)) {
    return false;
  }
  if (value > 1) {
    return fail("invalid cdr boolean");
  }
  *out = value != 0;
  return true;
}

bool CdrReader::read_u16(uint16_t* out) {
  uint8_t raw[2];
  if (!read_raw(2, 2, raw)) {
    return false;
  }
  *out = static_cast<uint16_t>(raw[0] | (raw[1] << 8));
  return true;
}

bool CdrReader::read_i16(int16_t* out) {
  uint16_t value = 0;
  if (!read_u16(&value)) {
    return false;
  }
  *out = static_cast<int16_t>(value);
  return true;
}

bool CdrReader::read_u32(uint32_t* out) {
  uint8_t raw[4];
  if (!read_raw(4, 4, raw)) {
    return false;
  }
  *out = static_cast<uint32_t>(raw[0]) | (static_cast<uint32_t>(raw[1]) << 8) |
         (static_cast<uint32_t>(raw[2]) << 16) |
         (static_cast<uint32_t>(raw[3]) << 24);
  return true;
}

bool CdrReader::read_i32(int32_t* out) {
  uint32_t value = 0;
  if (!read_u32(&value)) {
    return false;
  }
  *out = static_cast<int32_t>(value);
  return true;
}

bool CdrReader::read_u64(uint64_t* out) {
  uint8_t raw[8];
  if (!read_raw(8, 8, raw)) {
    return false;
  }
  uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<uint64_t>(raw[i]) << (8 * i);
  }
  *out = value;
  return true;
}

bool CdrReader::read_i64(int64_t* out) {
  uint64_t value = 0;
  if (!read_u64(&value)) {
    return false;
  }
  *out = static_cast<int64_t>(value);
  return true;
}

bool CdrReader::read_f32(float* out) {
  uint32_t bits = 0;
  if (!read_u32(&bits)) {
    return false;
  }
  std::memcpy(out, &bits, sizeof(*out));
  return true;
}

bool CdrReader::read_f64(double* out) {
  uint64_t bits = 0;
  if (!read_u64(&bits)) {
    return false;
  }
  std::memcpy(out, &bits, sizeof(*out));
  return true;
}

bool CdrReader::read_string(std::string* out) {
  uint32_t length = 0;
  if (!read_u32(&length)) {
    return false;
  }
  if (length == 0) {
    return fail("cdr string length is zero");
  }
  if (length > size_ - offset_) {
    return fail("cdr truncated reading string");
  }
  if (data_[offset_ + length - 1] != 0) {
    return fail("cdr string is not null-terminated");
  }
  out->assign(reinterpret_cast<const char*>(data_ + offset_), length - 1);
  offset_ += length;
  return true;
}

bool CdrReader::read_sequence_length(uint32_t* out) { return read_u32(out); }

bool CdrReader::skip(size_t count) {
  if (!ok()) {
    return false;
  }
  if (count > size_ - offset_) {
    return fail("cdr truncated skipping bytes");
  }
  offset_ += count;
  return true;
}

}  // namespace rrd_convert
