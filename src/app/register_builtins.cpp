#include "app/convert_app.hpp"

#include "decode/register_decoders.hpp"
#include "format/ros2_db3_adapter.hpp"
#include "sink/rerun_sink.hpp"

namespace rrd_convert {

void register_builtins(Registry* registry) {
  registry->add_adapter(
      "ros2_db3",
      [](const std::string& path) { return Ros2Db3Adapter::matches(path); },
      [] { return std::make_unique<Ros2Db3Adapter>(); });
  register_builtin_decoders(registry);
  registry->add_sink("rerun", ".rrd",
                     [] { return std::make_unique<RerunSink>(); });
}

}  // namespace rrd_convert
