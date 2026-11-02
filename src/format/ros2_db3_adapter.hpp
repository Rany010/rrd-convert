#ifndef RRD_CONVERT_SRC_FORMAT_ROS2_DB3_ADAPTER_HPP_
#define RRD_CONVERT_SRC_FORMAT_ROS2_DB3_ADAPTER_HPP_

#include "core/format_adapter.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rrd_convert {

// Reads one ROS 2 SQLite bag file, or every .db3 split in a bag directory.
class Ros2Db3Adapter : public FormatAdapter {
 public:
  Ros2Db3Adapter();
  ~Ros2Db3Adapter() override;

  // A .db3 file, a .zstd file, or a directory. Compressed inputs fail in open.
  static bool matches(const std::string& path);

  Status open(const std::string& path, const TopicFilter& filter) override;
  std::vector<TopicInfo> topics() const override;
  std::optional<std::string> message_definition(
      const std::string& type_name) const override;
  std::optional<std::string> message_definition_encoding(
      const std::string& type_name) const override;
  ReadResult next(InputRecord* record) override;

 private:
  struct Shard;
  struct DefinitionSet;

  void close();
  Status open_file(const std::string& path, const TopicFilter& filter);
  Status open_directory(const std::string& path, const TopicFilter& filter);

  std::string input_path_;
  std::vector<TopicInfo> topics_;
  std::unordered_map<std::string, TopicInfo> topics_by_name_;
  std::unique_ptr<DefinitionSet> definitions_;
  std::vector<std::unique_ptr<Shard>> shards_;
  std::string pending_error_;
  bool opened_ = false;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_FORMAT_ROS2_DB3_ADAPTER_HPP_
