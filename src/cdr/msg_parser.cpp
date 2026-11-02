#include "cdr/msg_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

#include <filesystem>

namespace rrd_convert {
namespace {

std::string trim(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  return text.substr(begin, end - begin);
}

std::string strip_comment(const std::string& line) {
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '"') {
      quoted = !quoted;
    } else if (!quoted && line[i] == '#') {
      return line.substr(0, i);
    }
  }
  return line;
}

bool is_constant_line(const std::string& line) {
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '=' && (i == 0 || line[i - 1] != '<')) {
      return true;
    }
  }
  return false;
}

bool is_identifier(const std::string& name) {
  if (name.empty()) {
    return false;
  }
  const unsigned char first = static_cast<unsigned char>(name[0]);
  if (std::isalpha(first) == 0 && name[0] != '_') {
    return false;
  }
  for (std::size_t i = 1; i < name.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(name[i]);
    if (std::isalnum(ch) == 0 && name[i] != '_') {
      return false;
    }
  }
  return true;
}

bool parse_u32(const std::string& text, uint32_t* out) {
  if (text.empty() || text[0] == '+' || text[0] == '-') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  const unsigned long value = std::strtoul(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0' || errno == ERANGE ||
      value > 0xFFFFFFFFUL) {
    return false;
  }
  *out = static_cast<uint32_t>(value);
  return true;
}

bool canonicalize_type_name(const std::string& name, std::string* out,
                            std::string* error) {
  const std::string trimmed = trim(name);
  const std::size_t first = trimmed.find('/');
  if (trimmed.empty() || first == std::string::npos || first == 0 ||
      first + 1 >= trimmed.size()) {
    *error = "invalid message type: " + trimmed;
    return false;
  }
  const std::size_t second = trimmed.find('/', first + 1);
  if (second == std::string::npos) {
    *out = trimmed.substr(0, first) + "/msg/" + trimmed.substr(first + 1);
    return true;
  }
  const std::string middle = trimmed.substr(first + 1, second - first - 1);
  if (middle == "msg" && second + 1 < trimmed.size() &&
      trimmed.find('/', second + 1) == std::string::npos) {
    *out = trimmed;
    return true;
  }
  *error = "invalid message type: " + trimmed;
  return false;
}

bool qualify_message_type(const std::string& token, const std::string& package,
                          std::string* out, std::string* error) {
  if (token.find('/') == std::string::npos) {
    if (package.empty() || !is_identifier(token)) {
      *error = "unqualified message type: " + token;
      return false;
    }
    *out = package + "/msg/" + token;
    return true;
  }
  return canonicalize_type_name(token, out, error);
}

void set_primitive(const std::string& name, FieldType* out) {
  if (name == "bool") {
    out->kind = FieldKind::kBool;
  } else if (name == "int8") {
    out->kind = FieldKind::kInt8;
  } else if (name == "byte" || name == "char" || name == "uint8") {
    out->kind = FieldKind::kUint8;
  } else if (name == "int16") {
    out->kind = FieldKind::kInt16;
  } else if (name == "uint16") {
    out->kind = FieldKind::kUint16;
  } else if (name == "int32") {
    out->kind = FieldKind::kInt32;
  } else if (name == "uint32") {
    out->kind = FieldKind::kUint32;
  } else if (name == "int64") {
    out->kind = FieldKind::kInt64;
  } else if (name == "uint64") {
    out->kind = FieldKind::kUint64;
  } else if (name == "float32") {
    out->kind = FieldKind::kFloat32;
  } else if (name == "float64") {
    out->kind = FieldKind::kFloat64;
  } else if (name == "string") {
    out->kind = FieldKind::kString;
  } else if (name == "wstring") {
    out->kind = FieldKind::kWstring;
  } else {
    out->kind = FieldKind::kMessage;
  }
}

bool parse_type_token(const std::string& token, const std::string& package,
                      FieldType* out, std::string* error) {
  *out = FieldType{};
  std::string base = token;
  if (!token.empty() && token.back() == ']') {
    const std::size_t open = token.rfind('[');
    if (open == std::string::npos) {
      *error = "invalid array bound";
      return false;
    }
    const std::string inside = token.substr(open + 1, token.size() - open - 2);
    base = token.substr(0, open);
    if (inside.empty()) {
      out->form = ArrayForm::kSequence;
    } else if (inside.compare(0, 2, "<=") == 0) {
      out->form = ArrayForm::kBounded;
      if (!parse_u32(inside.substr(2), &out->length)) {
        *error = "invalid array bound";
        return false;
      }
    } else {
      out->form = ArrayForm::kFixed;
      if (!parse_u32(inside, &out->length)) {
        *error = "invalid array bound";
        return false;
      }
    }
  }
  if (base.empty()) {
    *error = "invalid field type";
    return false;
  }
  const std::size_t bound_at = base.find("<=");
  std::string core = base;
  if (bound_at != std::string::npos) {
    core = base.substr(0, bound_at);
    if ((core != "string" && core != "wstring") ||
        !parse_u32(base.substr(bound_at + 2), &out->string_bound)) {
      *error = "invalid type bound";
      return false;
    }
  }
  set_primitive(core, out);
  if (out->kind == FieldKind::kMessage &&
      !qualify_message_type(core, package, &out->message_type, error)) {
    return false;
  }
  return true;
}

bool parse_field_line(const std::string& package, const std::string& raw,
                      Field* field, bool* skip, std::string* error) {
  *skip = false;
  const std::string stripped = trim(strip_comment(raw));
  if (stripped.empty() || is_constant_line(stripped)) {
    *skip = true;
    return true;
  }
  std::istringstream tokens(stripped);
  std::string type_token;
  std::string name;
  std::string extra;
  tokens >> type_token >> name >> extra;
  if (type_token.empty() || name.empty() || !extra.empty() ||
      !is_identifier(name)) {
    *error = "invalid field line: " + stripped;
    return false;
  }
  if (!parse_type_token(type_token, package, &field->type, error)) {
    return false;
  }
  field->name = name;
  return true;
}

bool parse_msg_body(const std::string& package, const std::string& body,
                    std::vector<Field>* fields, std::string* error) {
  std::istringstream lines(body);
  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    Field field;
    bool skip = false;
    if (!parse_field_line(package, line, &field, &skip, error)) {
      return false;
    }
    if (!skip) {
      fields->push_back(std::move(field));
    }
  }
  return true;
}

struct Section {
  std::string type_name;
  std::string body;
};

bool is_separator(const std::string& line) {
  if (line.size() < 10) {
    return false;
  }
  for (const char ch : line) {
    if (ch != '=') {
      return false;
    }
  }
  return true;
}

bool split_ros2msg(const std::string& root_type, const std::string& text,
                   std::vector<Section>* sections, std::string* error) {
  std::string normalized = text;
  if (normalized.size() >= 3 &&
      static_cast<unsigned char>(normalized[0]) == 0xEF &&
      static_cast<unsigned char>(normalized[1]) == 0xBB &&
      static_cast<unsigned char>(normalized[2]) == 0xBF) {
    normalized.erase(0, 3);
  }
  Section current;
  current.type_name = root_type;
  bool expect_msg = false;
  std::istringstream stream(normalized);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (expect_msg) {
      const std::string header = trim(line);
      if (header.empty()) {
        continue;
      }
      if (header.compare(0, 4, "MSG:") != 0) {
        *error = "expected MSG: after the definition separator";
        return false;
      }
      std::string canonical;
      if (!canonicalize_type_name(trim(header.substr(4)), &canonical, error)) {
        return false;
      }
      current.type_name = std::move(canonical);
      current.body.clear();
      expect_msg = false;
      continue;
    }
    if (is_separator(trim(line))) {
      sections->push_back(current);
      current = Section{};
      expect_msg = true;
      continue;
    }
    current.body.append(line);
    current.body.push_back('\n');
  }
  if (expect_msg) {
    *error = "expected MSG: after the definition separator";
    return false;
  }
  sections->push_back(std::move(current));
  return true;
}

std::string type_token(const Field& field) {
  std::string token;
  switch (field.type.kind) {
    case FieldKind::kBool:
      token = "bool";
      break;
    case FieldKind::kInt8:
      token = "int8";
      break;
    case FieldKind::kUint8:
      token = "uint8";
      break;
    case FieldKind::kInt16:
      token = "int16";
      break;
    case FieldKind::kUint16:
      token = "uint16";
      break;
    case FieldKind::kInt32:
      token = "int32";
      break;
    case FieldKind::kUint32:
      token = "uint32";
      break;
    case FieldKind::kInt64:
      token = "int64";
      break;
    case FieldKind::kUint64:
      token = "uint64";
      break;
    case FieldKind::kFloat32:
      token = "float32";
      break;
    case FieldKind::kFloat64:
      token = "float64";
      break;
    case FieldKind::kString:
      token = "string";
      break;
    case FieldKind::kWstring:
      token = "wstring";
      break;
    case FieldKind::kMessage:
      token = field.type.message_type;
      break;
  }
  if (field.type.string_bound > 0) {
    token += "<=" + std::to_string(field.type.string_bound);
  }
  switch (field.type.form) {
    case ArrayForm::kScalar:
      break;
    case ArrayForm::kFixed:
      token += "[" + std::to_string(field.type.length) + "]";
      break;
    case ArrayForm::kBounded:
      token += "[<=" + std::to_string(field.type.length) + "]";
      break;
    case ArrayForm::kSequence:
      token += "[]";
      break;
  }
  return token + " " + field.name;
}

std::string format_spec(const MessageSpec& spec) {
  std::string text;
  for (const Field& field : spec.fields) {
    text += type_token(field);
    text.push_back('\n');
  }
  return text;
}

std::string package_of(const std::string& type_name) {
  const std::size_t slash = type_name.find('/');
  if (slash == std::string::npos) {
    return {};
  }
  return type_name.substr(0, slash);
}

struct LocatedFile {
  enum class Code { kFound, kMissing, kInvalid };
  Code code = Code::kMissing;
  std::filesystem::path path;
};

LocatedFile find_msg_file(const std::vector<std::string>& msg_paths,
                          const std::string& type_name, std::string* error) {
  LocatedFile located;
  const std::size_t first = type_name.find('/');
  const std::size_t second =
      first == std::string::npos ? std::string::npos : type_name.find('/', first + 1);
  if (first == std::string::npos || second == std::string::npos ||
      type_name.substr(first + 1, second - first - 1) != "msg" ||
      type_name.find('/', second + 1) != std::string::npos) {
    located.code = LocatedFile::Code::kInvalid;
    *error = "invalid message type: " + type_name;
    return located;
  }
  const std::string package = type_name.substr(0, first);
  const std::string name = type_name.substr(second + 1);
  for (const std::string& root : msg_paths) {
    const std::filesystem::path path =
        std::filesystem::path(root) / package / "msg" / (name + ".msg");
    std::error_code exists_error;
    if (std::filesystem::is_regular_file(path, exists_error)) {
      located.code = LocatedFile::Code::kFound;
      located.path = path;
      return located;
    }
  }
  *error = "message definition not found: " + type_name;
  return located;
}

bool read_file(const std::filesystem::path& path, std::string* body,
               std::string* error) {
  std::ifstream input(path, std::ios::in | std::ios::binary);
  if (!input) {
    *error = "failed to read message file: " + path.string();
    return false;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (input.bad()) {
    *error = "failed to read message file: " + path.string();
    return false;
  }
  *body = buffer.str();
  return true;
}

MessageLoadCode load_type(const std::vector<std::string>& msg_paths,
                          const std::string& type_name, bool required,
                          std::vector<std::string>* stack, MessageCatalog* catalog,
                          std::string* error) {
  if (std::find(stack->begin(), stack->end(), type_name) != stack->end()) {
    *error = "recursive message type: " + type_name;
    return MessageLoadCode::kError;
  }
  if (catalog->types.find(type_name) != catalog->types.end()) {
    return MessageLoadCode::kOk;
  }
  const LocatedFile located = find_msg_file(msg_paths, type_name, error);
  if (located.code == LocatedFile::Code::kInvalid) {
    return MessageLoadCode::kError;
  }
  if (located.code == LocatedFile::Code::kMissing) {
    return required ? MessageLoadCode::kError : MessageLoadCode::kNotFound;
  }
  std::string body;
  if (!read_file(located.path, &body, error)) {
    return MessageLoadCode::kError;
  }
  MessageSpec spec;
  spec.type_name = type_name;
  if (!parse_msg_body(package_of(type_name), body, &spec.fields, error)) {
    *error = "failed to parse " + located.path.string() + ": " + *error;
    return MessageLoadCode::kError;
  }
  stack->push_back(type_name);
  for (const Field& field : spec.fields) {
    if (field.type.kind != FieldKind::kMessage) {
      continue;
    }
    const MessageLoadCode loaded =
        load_type(msg_paths, field.type.message_type, true, stack, catalog, error);
    if (loaded != MessageLoadCode::kOk) {
      stack->pop_back();
      return loaded;
    }
  }
  stack->pop_back();
  catalog->types.emplace(type_name, std::move(spec));
  return MessageLoadCode::kOk;
}

}  // namespace

bool canonical_message_type(const std::string& name, std::string* out,
                            std::string* error) {
  return canonicalize_type_name(name, out, error);
}

bool parse_ros2msg(const std::string& root_type, const std::string& text,
                   MessageCatalog* catalog, std::string* error) {
  std::string canonical;
  if (!canonicalize_type_name(root_type, &canonical, error)) {
    return false;
  }
  std::vector<Section> sections;
  if (!split_ros2msg(canonical, text, &sections, error)) {
    return false;
  }
  MessageCatalog parsed;
  for (const Section& section : sections) {
    if (parsed.types.find(section.type_name) != parsed.types.end()) {
      *error = "duplicate message type: " + section.type_name;
      return false;
    }
    MessageSpec spec;
    spec.type_name = section.type_name;
    if (!parse_msg_body(package_of(section.type_name), section.body, &spec.fields,
                        error)) {
      return false;
    }
    parsed.types.emplace(spec.type_name, std::move(spec));
  }
  *catalog = std::move(parsed);
  return true;
}

MessageLoadCode load_message_files(const std::vector<std::string>& msg_paths,
                                   const std::string& root_type,
                                   MessageCatalog* catalog, std::string* error) {
  std::string canonical;
  if (!canonicalize_type_name(root_type, &canonical, error)) {
    return MessageLoadCode::kError;
  }
  std::vector<std::string> stack;
  return load_type(msg_paths, canonical, false, &stack, catalog, error);
}

bool import_missing_messages(const std::vector<std::string>& msg_paths,
                             MessageCatalog* catalog, std::string* error) {
  if (msg_paths.empty()) {
    return true;
  }
  std::vector<std::string> missing;
  for (const auto& item : catalog->types) {
    for (const Field& field : item.second.fields) {
      if (field.type.kind == FieldKind::kMessage &&
          catalog->types.find(field.type.message_type) == catalog->types.end()) {
        missing.push_back(field.type.message_type);
      }
    }
  }
  for (const std::string& type_name : missing) {
    if (catalog->types.find(type_name) != catalog->types.end()) {
      continue;
    }
    const MessageLoadCode loaded =
        load_message_files(msg_paths, type_name, catalog, error);
    if (loaded == MessageLoadCode::kError) {
      return false;
    }
  }
  return true;
}

std::string export_ros2msg(const MessageCatalog& catalog,
                           const std::string& root_type) {
  std::string canonical;
  std::string ignored;
  if (!canonicalize_type_name(root_type, &canonical, &ignored)) {
    return {};
  }
  const auto root = catalog.types.find(canonical);
  if (root == catalog.types.end()) {
    return {};
  }
  std::string text = format_spec(root->second);
  std::vector<std::string> names;
  for (const auto& item : catalog.types) {
    if (item.first != canonical) {
      names.push_back(item.first);
    }
  }
  std::sort(names.begin(), names.end());
  const std::string separator(80, '=');
  for (const std::string& name : names) {
    text += separator;
    text += "\nMSG: ";
    text += name;
    text.push_back('\n');
    text += format_spec(catalog.types.at(name));
  }
  return text;
}

ResolvedDefinition resolve_message_definition(
    const std::string& type_name, const std::optional<std::string>& bag_text,
    const std::optional<std::string>& bag_encoding,
    const std::vector<std::string>& msg_paths) {
  const bool bag_is_ros2msg =
      bag_text.has_value() && !bag_text->empty() &&
      (!bag_encoding.has_value() || *bag_encoding == "ros2msg");
  if (bag_is_ros2msg) {
    ResolvedDefinition resolved;
    resolved.kind = ResolvedDefinition::Kind::kRos2Msg;
    resolved.encoding = "ros2msg";
    resolved.text = *bag_text;
    return resolved;
  }

  MessageCatalog catalog;
  std::string error;
  const MessageLoadCode loaded =
      load_message_files(msg_paths, type_name, &catalog, &error);
  if (loaded == MessageLoadCode::kOk) {
    ResolvedDefinition resolved;
    resolved.kind = ResolvedDefinition::Kind::kRos2Msg;
    resolved.encoding = "ros2msg";
    resolved.text = export_ros2msg(catalog, type_name);
    return resolved;
  }
  if (loaded == MessageLoadCode::kError) {
    ResolvedDefinition resolved;
    resolved.kind = ResolvedDefinition::Kind::kInvalid;
    resolved.error = error;
    return resolved;
  }
  if (bag_encoding.has_value() && *bag_encoding != "ros2msg") {
    ResolvedDefinition resolved;
    resolved.kind = ResolvedDefinition::Kind::kUnsupportedEncoding;
    resolved.encoding = *bag_encoding;
    resolved.error = "unsupported message definition encoding: " + *bag_encoding;
    return resolved;
  }
  ResolvedDefinition resolved;
  resolved.kind = ResolvedDefinition::Kind::kNone;
  resolved.error = "no message definition for " + type_name;
  return resolved;
}

}  // namespace rrd_convert
