#ifndef RRD_CONVERT_SRC_DECODE_ANNEXB_HPP_
#define RRD_CONVERT_SRC_DECODE_ANNEXB_HPP_

#include <cstddef>
#include <cstdint>
#include <string>

namespace rrd_convert {
namespace annexb {

// True when the Annex B buffer contains an H.264 IDR or an H.265 IRAP NAL.
inline bool is_keyframe(const uint8_t* data, size_t size, bool h265) {
  size_t index = 0;
  while (index + 3 < size) {
    size_t header = 0;
    if (data[index] == 0 && data[index + 1] == 0 && data[index + 2] == 1) {
      header = index + 3;
    } else if (index + 4 < size && data[index] == 0 && data[index + 1] == 0 &&
               data[index + 2] == 0 && data[index + 3] == 1) {
      header = index + 4;
    } else {
      ++index;
      continue;
    }
    if (header >= size) {
      break;
    }
    const uint8_t nal = data[header];
    if (!h265) {
      if ((nal & 0x1f) == 5) {
        return true;
      }
    } else {
      const uint8_t nal_type = static_cast<uint8_t>((nal >> 1) & 0x3f);
      if (nal_type >= 16 && nal_type <= 21) {
        return true;
      }
    }
    index = header;
  }
  return false;
}

inline std::string lower_copy(std::string value) {
  for (char& character : value) {
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character - 'A' + 'a');
    }
  }
  return value;
}

inline bool contains(const std::string& value, const char* token) {
  return value.find(token) != std::string::npos;
}

}  // namespace annexb
}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_DECODE_ANNEXB_HPP_
