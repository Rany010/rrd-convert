#ifndef RRD_CONVERT_SRC_APP_CONVERT_APP_HPP_
#define RRD_CONVERT_SRC_APP_CONVERT_APP_HPP_

#include "core/format_adapter.hpp"
#include "core/registry.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rrd_convert {

constexpr int kExitOk = 0;
constexpr int kExitIo = 1;
constexpr int kExitUsage = 2;

struct ConvertOptions {
  std::string input_path;
  std::string output_path;
  TopicFilter topic_filter;
  std::optional<std::string> root_frame;
  std::vector<std::string> msg_paths;
  std::optional<std::string> summary_json_path;
};

struct ConvertResult {
  int exit_code = kExitIo;
  std::string error;
  std::string summary;
};

void register_builtins(Registry* registry);

// Opens the matching adapter and sink, then reads every record. The output
// is written to "<output>.tmp" and renamed only after the sink closes.
ConvertResult run_convert(const ConvertOptions& options, Registry* registry);

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_APP_CONVERT_APP_HPP_
