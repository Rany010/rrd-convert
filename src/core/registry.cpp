#include "core/registry.hpp"

#include <utility>

namespace rrd_convert {
namespace {

bool ends_with(const std::string& value, const std::string& suffix) {
  if (suffix.empty() || value.size() < suffix.size()) {
    return false;
  }
  return value.compare(value.size() - suffix.size(), suffix.size(), suffix) ==
         0;
}

}  // namespace

void Registry::add_adapter(std::string format_id, AdapterMatch matches,
                           AdapterFactory factory) {
  adapters_.push_back(AdapterEntry{std::move(format_id), std::move(matches),
                                   std::move(factory)});
}

void Registry::add_decoder(std::string type_name, DecoderFactory factory) {
  for (DecoderEntry& entry : decoders_) {
    if (entry.type_name == type_name) {
      entry.factory = std::move(factory);
      return;
    }
  }
  decoders_.push_back(
      DecoderEntry{std::move(type_name), std::move(factory)});
}

void Registry::add_sink(std::string name, std::string extension,
                        SinkFactory factory) {
  for (SinkEntry& entry : sinks_) {
    if (entry.extension == extension) {
      entry.name = std::move(name);
      entry.factory = std::move(factory);
      return;
    }
  }
  sinks_.push_back(SinkEntry{std::move(name), std::move(extension),
                             std::move(factory)});
}

std::unique_ptr<FormatAdapter> Registry::create_adapter(
    const std::string& path) const {
  for (const AdapterEntry& entry : adapters_) {
    if (entry.matches && entry.matches(path) && entry.factory) {
      return entry.factory();
    }
  }
  return nullptr;
}

std::unique_ptr<Decoder> Registry::create_decoder(
    const std::string& type_name) const {
  for (const DecoderEntry& entry : decoders_) {
    if (entry.type_name == type_name && entry.factory) {
      return entry.factory();
    }
  }
  return nullptr;
}

std::unique_ptr<Sink> Registry::create_sink_for_output(
    const std::string& path) const {
  for (const SinkEntry& entry : sinks_) {
    if (ends_with(path, entry.extension) && entry.factory) {
      return entry.factory();
    }
  }
  return nullptr;
}

}  // namespace rrd_convert
