#ifndef RRD_CONVERT_SRC_CORE_INPUT_RECORD_HPP_
#define RRD_CONVERT_SRC_CORE_INPUT_RECORD_HPP_

#include <cstdint>
#include <string>
#include <vector>

namespace rrd_convert {

// One stored message, still in its original serialization.
struct InputRecord {
  std::string topic;
  std::string type_name;
  std::string serialization;
  int64_t log_time_ns = 0;
  std::vector<uint8_t> bytes;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_CORE_INPUT_RECORD_HPP_
