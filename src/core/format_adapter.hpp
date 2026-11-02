#ifndef RRD_CONVERT_SRC_CORE_FORMAT_ADAPTER_HPP_
#define RRD_CONVERT_SRC_CORE_FORMAT_ADAPTER_HPP_

#include "core/input_record.hpp"
#include "core/status.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rrd_convert {

struct TopicInfo {
  std::string name;
  std::string type_name;
  std::string serialization;
};

// Exact topic names. An empty include list allows every topic, and exclude
// wins when a name is in both lists.
struct TopicFilter {
  std::vector<std::string> include;
  std::vector<std::string> exclude;

  bool allows(const std::string& topic) const;
};

enum class ReadCode { kRecord, kDone, kError };

struct ReadResult {
  ReadCode code = ReadCode::kDone;
  std::string reason;

  static ReadResult Record() { return ReadResult{ReadCode::kRecord, {}}; }
  static ReadResult Done() { return ReadResult{ReadCode::kDone, {}}; }
  static ReadResult Error(std::string reason) {
    return ReadResult{ReadCode::kError, std::move(reason)};
  }
};

class FormatAdapter {
 public:
  virtual ~FormatAdapter() = default;
  FormatAdapter(const FormatAdapter&) = delete;
  FormatAdapter& operator=(const FormatAdapter&) = delete;

  virtual Status open(const std::string& path, const TopicFilter& filter) = 0;
  virtual std::vector<TopicInfo> topics() const = 0;
  // ros2msg text only. Adapters that also store other encodings leave this
  // empty and report the encoding from message_definition_encoding().
  virtual std::optional<std::string> message_definition(
      const std::string& type_name) const = 0;

  // Encoding of the stored definition. The default treats any returned text
  // as ros2msg. Override this when another encoding can be the only one stored.
  virtual std::optional<std::string> message_definition_encoding(
      const std::string& type_name) const {
    if (message_definition(type_name).has_value()) {
      return std::string("ros2msg");
    }
    return std::nullopt;
  }

  virtual ReadResult next(InputRecord* record) = 0;

 protected:
  FormatAdapter() = default;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_CORE_FORMAT_ADAPTER_HPP_
