#ifndef RRD_CONVERT_SRC_CDR_DYNAMIC_MESSAGE_HPP_
#define RRD_CONVERT_SRC_CDR_DYNAMIC_MESSAGE_HPP_

#include "cdr/msg_parser.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rrd_convert {

// One CDR value addressed by a field path such as "pose.position.x" or
// "points[0].x". Pointers returned by field() and find_path() stay valid
// until this value is mutated or destroyed.
struct DynamicValue {
  enum class Kind { kAbsent, kBool, kInt, kUint, kFloat, kString, kStruct,
                    kArray };

  Kind kind = Kind::kAbsent;
  bool bool_value = false;
  int64_t int_value = 0;
  uint64_t uint_value = 0;
  double float_value = 0;
  std::string string_value;
  std::vector<std::pair<std::string, DynamicValue>> fields;
  std::vector<DynamicValue> elements;

  const DynamicValue* field(const std::string& name) const;
  // bool, integer, and floating values. uint64 above 2^53 is rounded.
  bool number(double* out) const;
};

// Reads one XCDR1 message. data starts with the 4-byte encapsulation header.
bool read_dynamic_message(const MessageCatalog& catalog,
                          const std::string& type_name, const uint8_t* data,
                          size_t size, DynamicValue* value, std::string* error);

// Uses ros2msg text when it is present, otherwise msg_paths. Another encoding
// fails when no .msg file can be loaded. Nested types missing from the text
// are loaded from msg_paths.
bool read_defined_message(const std::string& type_name,
                          const std::string& encoding, const std::string& text,
                          const std::vector<std::string>& msg_paths,
                          const uint8_t* data, size_t size, DynamicValue* value,
                          std::string* error);

const DynamicValue* find_path(const DynamicValue& value,
                              const std::string& path, std::string* error);

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_CDR_DYNAMIC_MESSAGE_HPP_
