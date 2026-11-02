#ifndef RRD_CONVERT_SRC_CORE_REGISTRY_HPP_
#define RRD_CONVERT_SRC_CORE_REGISTRY_HPP_

#include "core/decoder.hpp"
#include "core/format_adapter.hpp"
#include "core/sink.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rrd_convert {

using AdapterMatch = std::function<bool(const std::string& path)>;
using AdapterFactory = std::function<std::unique_ptr<FormatAdapter>()>;
using DecoderFactory = std::function<std::unique_ptr<Decoder>()>;
using SinkFactory = std::function<std::unique_ptr<Sink>()>;

// Factories, not instances. Each conversion creates its own objects.
class Registry {
 public:
  void add_adapter(std::string format_id, AdapterMatch matches,
                   AdapterFactory factory);
  void add_decoder(std::string type_name, DecoderFactory factory);
  void add_sink(std::string name, std::string extension, SinkFactory factory);

  // The first registered adapter whose match function accepts the path.
  std::unique_ptr<FormatAdapter> create_adapter(const std::string& path) const;
  // The last factory registered for this type name.
  std::unique_ptr<Decoder> create_decoder(const std::string& type_name) const;
  // The sink whose extension, including the dot, is a suffix of path.
  // A later registration for the same extension replaces the earlier one.
  std::unique_ptr<Sink> create_sink_for_output(const std::string& path) const;

 private:
  struct AdapterEntry {
    std::string format_id;
    AdapterMatch matches;
    AdapterFactory factory;
  };
  struct DecoderEntry {
    std::string type_name;
    DecoderFactory factory;
  };
  struct SinkEntry {
    std::string name;
    std::string extension;
    SinkFactory factory;
  };

  std::vector<AdapterEntry> adapters_;
  std::vector<DecoderEntry> decoders_;
  std::vector<SinkEntry> sinks_;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_CORE_REGISTRY_HPP_
