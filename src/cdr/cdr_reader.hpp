#ifndef RRD_CONVERT_SRC_CDR_CDR_READER_HPP_
#define RRD_CONVERT_SRC_CDR_CDR_READER_HPP_

#include <cstddef>
#include <cstdint>
#include <string>

namespace rrd_convert {

// Plain CDR (XCDR1) reader. data must stay alive and starts with the 4-byte
// encapsulation header. Alignment is measured from the first byte after that
// header, matching ROS 2 bags. Alignments of 4 bytes or less are unchanged
// from measuring at the header itself.
class CdrReader {
 public:
  CdrReader(const uint8_t* data, size_t size);

  bool ok() const;
  const std::string& error() const;
  size_t offset() const;

  bool read_bool(bool* out);
  bool read_i8(int8_t* out);
  bool read_u8(uint8_t* out);
  bool read_i16(int16_t* out);
  bool read_u16(uint16_t* out);
  bool read_i32(int32_t* out);
  bool read_u32(uint32_t* out);
  bool read_i64(int64_t* out);
  bool read_u64(uint64_t* out);
  bool read_f32(float* out);
  bool read_f64(double* out);
  bool read_string(std::string* out);
  bool read_sequence_length(uint32_t* out);
  // Advances without aligning. Fails if the buffer ends first.
  bool skip(size_t count);

 private:
  bool fail(const std::string& error);
  bool align_to(size_t alignment);
  bool read_raw(size_t alignment, size_t count, uint8_t* out);

  const uint8_t* data_;
  size_t size_;
  size_t offset_;
  bool little_endian_;
  std::string error_;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_CDR_CDR_READER_HPP_
