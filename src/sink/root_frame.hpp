#ifndef RRD_CONVERT_SRC_SINK_ROOT_FRAME_HPP_
#define RRD_CONVERT_SRC_SINK_ROOT_FRAME_HPP_

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace rrd_convert {

// `--root-frame` wins. Otherwise the parent frame that is never a child and
// appears most often wins. Equal counts use the lexicographically smaller name.
std::optional<std::string> select_root_frame(
    const std::optional<std::string>& requested,
    const std::unordered_map<std::string, int>& parent_counts,
    const std::unordered_set<std::string>& child_frames);

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_SINK_ROOT_FRAME_HPP_
