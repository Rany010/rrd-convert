#ifndef RRD_CONVERT_TESTS_BAG_FIXTURE_HPP_
#define RRD_CONVERT_TESTS_BAG_FIXTURE_HPP_

#include <cstdint>
#include <string>
#include <vector>

namespace rrd_convert {

struct FixtureMessage {
  std::string topic;
  std::string type_name;
  std::string serialization = "cdr";
  int64_t log_time_ns = 0;
  std::vector<uint8_t> bytes;
};

struct FixtureDefinition {
  std::string type_name;
  std::string encoding;
  std::string text;
};

// Writes a minimal ROS 2 SQLite bag. Empty string means success.
// include_definition_table creates message_definitions even when definitions
// is empty, matching schema 4. Set it false for an older bag schema.
std::string write_ros2_db3(const std::string& path,
                           const std::vector<FixtureMessage>& messages,
                           const std::vector<FixtureDefinition>& definitions,
                           bool include_definition_table = true);

// One std_msgs/String and one custom message with a ros2msg definition.
std::string write_sample_fixture(const std::string& path);

}  // namespace rrd_convert

#endif  // RRD_CONVERT_TESTS_BAG_FIXTURE_HPP_
