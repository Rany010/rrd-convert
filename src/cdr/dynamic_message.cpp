#include "cdr/dynamic_message.hpp"

#include "cdr/cdr_reader.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace rrd_convert {
namespace {

void append_utf8(std::string* out, uint32_t code) {
  if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
    code = 0xFFFD;
  }
  if (code < 0x80) {
    out->push_back(static_cast<char>(code));
    return;
  }
  if (code < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (code >> 6)));
    out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    return;
  }
  if (code < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (code >> 12)));
    out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    return;
  }
  out->push_back(static_cast<char>(0xF0 | (code >> 18)));
  out->push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
  out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
  out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
}

bool reader_failed(const CdrReader& reader, std::string* error) {
  *error = reader.error().empty() ? "truncated cdr value" : reader.error();
  return false;
}

bool read_value(CdrReader* reader, const MessageCatalog& catalog,
                const FieldType& type, std::vector<std::string>* stack,
                DynamicValue* out, std::string* error);

bool read_scalar(CdrReader* reader, const MessageCatalog& catalog,
                 const FieldType& type, std::vector<std::string>* stack,
                 DynamicValue* out, std::string* error) {
  switch (type.kind) {
    case FieldKind::kBool: {
      bool value = false;
      if (!reader->read_bool(&value)) {
        return reader_failed(*reader, error);
      }
      out->kind = DynamicValue::Kind::kBool;
      out->bool_value = value;
      return true;
    }
    case FieldKind::kInt8: {
      int8_t value = 0;
      if (!reader->read_i8(&value)) {
        return reader_failed(*reader, error);
      }
      out->kind = DynamicValue::Kind::kInt;
      out->int_value = value;
      return true;
    }
    case FieldKind::kUint8: {
      uint8_t value = 0;
      if (!reader->read_u8(&value)) {
        return reader_failed(*reader, error);
      }
      out->kind = DynamicValue::Kind::kUint;
      out->uint_value = value;
      return true;
    }
    case FieldKind::kInt16: {
      int16_t value = 0;
      if (!reader->read_i16(&value)) {
        return reader_failed(*reader, error);
      }
      out->kind = DynamicValue::Kind::kInt;
      out->int_value = value;
      return true;
    }
    case FieldKind::kUint16: {
      uint16_t value = 0;
      if (!reader->read_u16(&value)) {
        return reader_failed(*reader, error);
      }
      out->kind = DynamicValue::Kind::kUint;
      out->uint_value = value;
      return true;
    }
    case FieldKind::kInt32: {
      int32_t value = 0;
      if (!reader->read_i32(&value)) {
        return reader_failed(*reader, error);
      }
      out->kind = DynamicValue::Kind::kInt;
      out->int_value = value;
      return true;
    }
    case FieldKind::kUint32: {
      uint32_t value = 0;
      if (!reader->read_u32(&value)) {
        return reader_failed(*reader, error);
      }
      out->kind = DynamicValue::Kind::kUint;
      out->uint_value = value;
      return true;
    }
    case FieldKind::kInt64: {
      int64_t value = 0;
      if (!reader->read_i64(&value)) {
        return reader_failed(*reader, error);
      }
      out->kind = DynamicValue::Kind::kInt;
      out->int_value = value;
      return true;
    }
    case FieldKind::kUint64: {
      uint64_t value = 0;
      if (!reader->read_u64(&value)) {
        return reader_failed(*reader, error);
      }
      out->kind = DynamicValue::Kind::kUint;
      out->uint_value = value;
      return true;
    }
    case FieldKind::kFloat32: {
      float value = 0;
      if (!reader->read_f32(&value)) {
        return reader_failed(*reader, error);
      }
      out->kind = DynamicValue::Kind::kFloat;
      out->float_value = value;
      return true;
    }
    case FieldKind::kFloat64: {
      double value = 0;
      if (!reader->read_f64(&value)) {
        return reader_failed(*reader, error);
      }
      out->kind = DynamicValue::Kind::kFloat;
      out->float_value = value;
      return true;
    }
    case FieldKind::kString: {
      std::string value;
      if (!reader->read_string(&value)) {
        return reader_failed(*reader, error);
      }
      if (type.string_bound > 0 &&
          value.size() > static_cast<std::size_t>(type.string_bound)) {
        *error = "bounded string exceeds its maximum length";
        return false;
      }
      out->kind = DynamicValue::Kind::kString;
      out->string_value = std::move(value);
      return true;
    }
    case FieldKind::kWstring: {
      // ROS 2 writes a count of code points, then one uint32 each, with no NUL.
      uint32_t count = 0;
      if (!reader->read_sequence_length(&count)) {
        return reader_failed(*reader, error);
      }
      if (type.string_bound > 0 && count > type.string_bound) {
        *error = "bounded string exceeds its maximum length";
        return false;
      }
      std::string value;
      for (uint32_t i = 0; i < count; ++i) {
        uint32_t code = 0;
        if (!reader->read_u32(&code)) {
          return reader_failed(*reader, error);
        }
        append_utf8(&value, code);
      }
      out->kind = DynamicValue::Kind::kString;
      out->string_value = std::move(value);
      return true;
    }
    case FieldKind::kMessage: {
      const auto found = catalog.types.find(type.message_type);
      if (found == catalog.types.end()) {
        *error = "unknown message type: " + type.message_type;
        return false;
      }
      for (const std::string& parent : *stack) {
        if (parent == type.message_type) {
          *error = "recursive message type: " + type.message_type;
          return false;
        }
      }
      stack->push_back(type.message_type);
      out->kind = DynamicValue::Kind::kStruct;
      for (const Field& field : found->second.fields) {
        DynamicValue child;
        if (!read_value(reader, catalog, field.type, stack, &child, error)) {
          stack->pop_back();
          return false;
        }
        out->fields.emplace_back(field.name, std::move(child));
      }
      stack->pop_back();
      return true;
    }
  }
  *error = "unsupported field type";
  return false;
}

bool read_value(CdrReader* reader, const MessageCatalog& catalog,
                const FieldType& type, std::vector<std::string>* stack,
                DynamicValue* out, std::string* error) {
  if (type.form == ArrayForm::kScalar) {
    return read_scalar(reader, catalog, type, stack, out, error);
  }
  uint32_t count = type.length;
  if (type.form == ArrayForm::kSequence || type.form == ArrayForm::kBounded) {
    if (!reader->read_sequence_length(&count)) {
      return reader_failed(*reader, error);
    }
    if (type.form == ArrayForm::kBounded && count > type.length) {
      *error = "bounded sequence exceeds its maximum length";
      return false;
    }
  }
  FieldType element = type;
  element.form = ArrayForm::kScalar;
  element.length = 0;
  out->kind = DynamicValue::Kind::kArray;
  out->elements.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    DynamicValue child;
    if (!read_scalar(reader, catalog, element, stack, &child, error)) {
      return false;
    }
    out->elements.push_back(std::move(child));
  }
  return true;
}

bool parse_index(const std::string& path, std::size_t* index, uint64_t* value,
                 std::string* error) {
  if (*index >= path.size() || path[*index] != '[') {
    *error = "invalid field path";
    return false;
  }
  const std::size_t start = *index + 1;
  const std::size_t end = path.find(']', start);
  if (end == std::string::npos || end == start) {
    *error = "invalid field path";
    return false;
  }
  const std::string text = path.substr(start, end - start);
  if (text[0] == '-' || text[0] == '+') {
    *error = "invalid field path";
    return false;
  }
  errno = 0;
  char* stop = nullptr;
  const unsigned long long parsed = std::strtoull(text.c_str(), &stop, 10);
  if (stop == text.c_str() || *stop != '\0' || errno == ERANGE) {
    *error = "invalid field path";
    return false;
  }
  *value = parsed;
  *index = end + 1;
  return true;
}

}  // namespace

const DynamicValue* DynamicValue::field(const std::string& name) const {
  for (const auto& item : fields) {
    if (item.first == name) {
      return &item.second;
    }
  }
  return nullptr;
}

bool DynamicValue::number(double* out) const {
  switch (kind) {
    case Kind::kBool:
      *out = bool_value ? 1.0 : 0.0;
      return true;
    case Kind::kInt:
      *out = static_cast<double>(int_value);
      return true;
    case Kind::kUint:
      *out = static_cast<double>(uint_value);
      return true;
    case Kind::kFloat:
      *out = float_value;
      return true;
    case Kind::kAbsent:
    case Kind::kString:
    case Kind::kStruct:
    case Kind::kArray:
      return false;
  }
  return false;
}

bool read_dynamic_message(const MessageCatalog& catalog,
                          const std::string& type_name, const uint8_t* data,
                          size_t size, DynamicValue* value, std::string* error) {
  CdrReader reader(data, size);
  if (!reader.ok()) {
    return reader_failed(reader, error);
  }
  std::string canonical;
  if (!canonical_message_type(type_name, &canonical, error)) {
    return false;
  }
  if (catalog.types.find(canonical) == catalog.types.end()) {
    *error = "unknown message type: " + canonical;
    return false;
  }
  FieldType root;
  root.kind = FieldKind::kMessage;
  root.message_type = canonical;
  std::vector<std::string> stack;
  return read_value(&reader, catalog, root, &stack, value, error);
}

bool read_defined_message(const std::string& type_name,
                          const std::string& encoding, const std::string& text,
                          const std::vector<std::string>& msg_paths,
                          const uint8_t* data, size_t size, DynamicValue* value,
                          std::string* error) {
  const bool usable_text =
      !text.empty() && (encoding.empty() || encoding == "ros2msg");
  MessageCatalog catalog;
  if (usable_text) {
    if (!parse_ros2msg(type_name, text, &catalog, error)) {
      return false;
    }
    if (!import_missing_messages(msg_paths, &catalog, error)) {
      return false;
    }
  } else {
    const MessageLoadCode loaded =
        load_message_files(msg_paths, type_name, &catalog, error);
    if (loaded == MessageLoadCode::kError) {
      return false;
    }
    if (loaded != MessageLoadCode::kOk) {
      if (!encoding.empty() && encoding != "ros2msg") {
        *error = "unsupported message definition encoding: " + encoding;
      } else {
        *error = "no message definition for " + type_name;
      }
      return false;
    }
  }
  return read_dynamic_message(catalog, type_name, data, size, value, error);
}

const DynamicValue* find_path(const DynamicValue& value, const std::string& path,
                              std::string* error) {
  if (path.empty()) {
    *error = "invalid field path";
    return nullptr;
  }
  const DynamicValue* current = &value;
  std::size_t index = 0;
  while (index < path.size()) {
    if (path[index] == '[') {
      uint64_t element = 0;
      if (!parse_index(path, &index, &element, error)) {
        return nullptr;
      }
      if (current->kind != DynamicValue::Kind::kArray) {
        *error = "value is not an array";
        return nullptr;
      }
      if (element >= current->elements.size()) {
        *error = "index out of range";
        return nullptr;
      }
      current = &current->elements[static_cast<std::size_t>(element)];
    } else if (path[index] == '.') {
      *error = "invalid field path";
      return nullptr;
    } else {
      const std::size_t start = index;
      while (index < path.size() && path[index] != '.' && path[index] != '[') {
        ++index;
      }
      const std::string name = path.substr(start, index - start);
      if (name.empty() || current->kind != DynamicValue::Kind::kStruct) {
        *error = "field not found: " + name;
        return nullptr;
      }
      const DynamicValue* next = current->field(name);
      if (next == nullptr) {
        *error = "field not found: " + name;
        return nullptr;
      }
      current = next;
    }
    if (index < path.size() && path[index] == '.') {
      ++index;
      if (index == path.size()) {
        *error = "invalid field path";
        return nullptr;
      }
    } else if (index < path.size() && path[index] != '[') {
      *error = "invalid field path";
      return nullptr;
    }
  }
  return current;
}

}  // namespace rrd_convert
