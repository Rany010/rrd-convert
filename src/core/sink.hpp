#ifndef RRD_CONVERT_SRC_CORE_SINK_HPP_
#define RRD_CONVERT_SRC_CORE_SINK_HPP_

#include "core/status.hpp"
#include "core/visual_primitive.hpp"

#include <optional>
#include <string>

namespace rrd_convert {

struct SinkOptions {
  std::optional<std::string> root_frame;
};

class Sink {
 public:
  virtual ~Sink() = default;
  Sink(const Sink&) = delete;
  Sink& operator=(const Sink&) = delete;

  virtual std::string name() const = 0;
  virtual Status open(const std::string& path, const SinkOptions& options) = 0;
  virtual Status write(const VisualPrimitive& primitive) = 0;
  virtual Status close() = 0;

 protected:
  Sink() = default;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_CORE_SINK_HPP_
