#include "core/format_adapter.hpp"

namespace rrd_convert {

bool TopicFilter::allows(const std::string& topic) const {
  if (!include.empty()) {
    bool found = false;
    for (const std::string& name : include) {
      if (name == topic) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  for (const std::string& name : exclude) {
    if (name == topic) {
      return false;
    }
  }
  return true;
}

}  // namespace rrd_convert
