#include "sink/root_frame.hpp"

namespace rrd_convert {

std::optional<std::string> select_root_frame(
    const std::optional<std::string>& requested,
    const std::unordered_map<std::string, int>& parent_counts,
    const std::unordered_set<std::string>& child_frames) {
  if (requested.has_value()) {
    return requested;
  }
  std::optional<std::string> best;
  int best_count = 0;
  for (const auto& entry : parent_counts) {
    if (entry.first.empty() || child_frames.count(entry.first) != 0) {
      continue;
    }
    if (!best.has_value() || entry.second > best_count ||
        (entry.second == best_count && entry.first < *best)) {
      best = entry.first;
      best_count = entry.second;
    }
  }
  return best;
}

}  // namespace rrd_convert
