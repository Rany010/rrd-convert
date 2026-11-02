#ifndef RRD_CONVERT_SRC_DECODE_ROS_CDR_HPP_
#define RRD_CONVERT_SRC_DECODE_ROS_CDR_HPP_

#include "cdr/cdr_reader.hpp"

#include <cstdint>
#include <string>

namespace rrd_convert {
namespace ros_cdr {

// ROS 2 builtin_interfaces/Time, then std_msgs/Header's frame_id.
inline bool read_header(CdrReader* reader, int64_t* stamp_ns,
                        std::string* frame_id) {
  int32_t sec = 0;
  uint32_t nanosec = 0;
  if (!reader->read_i32(&sec) || !reader->read_u32(&nanosec) ||
      !reader->read_string(frame_id)) {
    return false;
  }
  *stamp_ns = static_cast<int64_t>(sec) * 1000000000LL +
              static_cast<int64_t>(nanosec);
  return true;
}

inline bool require_cdr(const std::string& serialization, std::string* error) {
  if (serialization == "cdr") {
    return true;
  }
  *error = "unsupported serialization";
  return false;
}

}  // namespace ros_cdr
}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_DECODE_ROS_CDR_HPP_
