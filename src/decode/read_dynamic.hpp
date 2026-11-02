#ifndef RRD_CONVERT_SRC_DECODE_READ_DYNAMIC_HPP_
#define RRD_CONVERT_SRC_DECODE_READ_DYNAMIC_HPP_

#include "cdr/dynamic_message.hpp"
#include "core/decoder.hpp"
#include "core/input_record.hpp"

#include <string>

namespace rrd_convert {

// Reads a custom message from the definition supplied by the convert loop.
// Builtin decoders keep their handwritten readers and do not call this.
inline bool read_dynamic_record(const DecodeContext& context,
                                const InputRecord& record, DynamicValue* value,
                                std::string* error) {
  const DefinitionSource source = context.definition_source(record.type_name);
  if (source.kind == DefinitionSource::Kind::kUnsupportedEncoding ||
      source.kind == DefinitionSource::Kind::kInvalid) {
    if (error != nullptr) {
      *error = source.error;
    }
    return false;
  }
  const std::string encoding =
      source.kind == DefinitionSource::Kind::kRos2Msg ? "ros2msg" : "";
  const uint8_t* data = record.bytes.empty() ? nullptr : record.bytes.data();
  return read_defined_message(record.type_name, encoding, source.text,
                              context.message_paths(), data, record.bytes.size(),
                              value, error);
}

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_DECODE_READ_DYNAMIC_HPP_
