#ifndef RRD_CONVERT_TESTS_CDR_WRITER_HPP_
#define RRD_CONVERT_TESTS_CDR_WRITER_HPP_

#include <cstdint>
#include <string>
#include <vector>

namespace rrd_convert {

// Test-only plain CDR writer. Not part of the converter library.
// Alignment matches CdrReader: from the first byte after the header.
class CdrWriter {
 public:
  explicit CdrWriter(bool little_endian = true);

  void write_bool(bool value);
  void write_i8(int8_t value);
  void write_u8(uint8_t value);
  void write_u16(uint16_t value);
  void write_u32(uint32_t value);
  void write_i32(int32_t value);
  void write_u64(uint64_t value);
  void write_f32(float value);
  void write_f64(double value);
  void write_bytes(const uint8_t* data, size_t count);
  void write_string(const std::string& value);
  void write_sequence_length(uint32_t length);

  const std::vector<uint8_t>& bytes() const;

 private:
  void align_to(size_t alignment);
  void write_raw(size_t alignment, const uint8_t* data, size_t count);

  std::vector<uint8_t> bytes_;
  bool little_endian_;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_TESTS_CDR_WRITER_HPP_
