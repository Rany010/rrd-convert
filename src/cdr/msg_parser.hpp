#ifndef RRD_CONVERT_SRC_CDR_MSG_PARSER_HPP_
#define RRD_CONVERT_SRC_CDR_MSG_PARSER_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rrd_convert {

// ROS 2 field kinds. byte and char are both stored as uint8.
enum class FieldKind {
  kBool,
  kInt8,
  kUint8,
  kInt16,
  kUint16,
  kInt32,
  kUint32,
  kInt64,
  kUint64,
  kFloat32,
  kFloat64,
  kString,
  kWstring,
  kMessage
};

enum class ArrayForm { kScalar, kFixed, kBounded, kSequence };

struct FieldType {
  FieldKind kind = FieldKind::kBool;
  // Canonical pkg/msg/Type when kind is kMessage.
  std::string message_type;
  ArrayForm form = ArrayForm::kScalar;
  // Fixed length, or the maximum length of a bounded array.
  uint32_t length = 0;
  // Maximum characters of a bounded string or wstring. Zero means unbounded.
  uint32_t string_bound = 0;
};

struct Field {
  std::string name;
  FieldType type;
};

struct MessageSpec {
  std::string type_name;
  std::vector<Field> fields;
};

struct MessageCatalog {
  std::unordered_map<std::string, MessageSpec> types;
};

enum class MessageLoadCode { kOk, kNotFound, kError };

struct ResolvedDefinition {
  enum class Kind { kNone, kRos2Msg, kUnsupportedEncoding, kInvalid };

  Kind kind = Kind::kNone;
  std::string encoding;
  std::string text;
  std::string error;
};

// "pkg/Type" and "pkg/msg/Type" both become "pkg/msg/Type".
bool canonical_message_type(const std::string& name, std::string* out,
                            std::string* error);

// Parses a ros2msg definition. The first section is root_type. Later sections
// are introduced by a line of '=' and "MSG: pkg/msg/Type". Constants are
// skipped. Names of the form pkg/Type are stored as pkg/msg/Type.
bool parse_ros2msg(const std::string& root_type, const std::string& text,
                   MessageCatalog* catalog, std::string* error);

// Loads root_type and its dependencies from <root>/<package>/msg/<Type>.msg.
MessageLoadCode load_message_files(const std::vector<std::string>& msg_paths,
                                   const std::string& root_type,
                                   MessageCatalog* catalog, std::string* error);

// Fills message types that the catalog references but does not define, when a
// matching file exists. A missing file is left unresolved. A broken file fails.
bool import_missing_messages(const std::vector<std::string>& msg_paths,
                             MessageCatalog* catalog, std::string* error);

// Writes the catalog back as ros2msg text, root type first.
std::string export_ros2msg(const MessageCatalog& catalog,
                           const std::string& root_type);

// Bag ros2msg wins. Otherwise the .msg directories are searched. ros2idl is
// reported only when no usable .msg file exists.
ResolvedDefinition resolve_message_definition(
    const std::string& type_name, const std::optional<std::string>& bag_text,
    const std::optional<std::string>& bag_encoding,
    const std::vector<std::string>& msg_paths);

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_CDR_MSG_PARSER_HPP_
