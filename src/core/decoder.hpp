#ifndef RRD_CONVERT_SRC_CORE_DECODER_HPP_
#define RRD_CONVERT_SRC_CORE_DECODER_HPP_

#include "core/input_record.hpp"
#include "core/visual_primitive.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rrd_convert {

// Result of looking up a message definition for the generic CDR reader.
struct DefinitionSource {
  enum class Kind { kNone, kRos2Msg, kUnsupportedEncoding, kInvalid };

  Kind kind = Kind::kNone;
  std::string encoding;
  std::string text;
  std::string error;
};

// Lookup for message definitions. Builtin decoders do not need it. A custom
// decoder calls read_dynamic_record() with this context.
class DecodeContext {
 public:
  virtual ~DecodeContext() = default;
  DecodeContext(const DecodeContext&) = delete;
  DecodeContext& operator=(const DecodeContext&) = delete;

  // ros2msg text from the bag, or from --msg-path when the bag has none.
  virtual std::optional<std::string> message_definition(
      const std::string& type_name) const = 0;

  // Distinguishes a missing definition from ros2idl and a broken .msg file.
  virtual DefinitionSource definition_source(
      const std::string& type_name) const {
    DefinitionSource source;
    std::optional<std::string> text = message_definition(type_name);
    if (!text.has_value() || text->empty()) {
      source.kind = DefinitionSource::Kind::kNone;
      source.error = "no message definition for " + type_name;
      return source;
    }
    source.kind = DefinitionSource::Kind::kRos2Msg;
    source.encoding = "ros2msg";
    source.text = std::move(*text);
    return source;
  }

  virtual const std::vector<std::string>& message_paths() const {
    static const std::vector<std::string> kEmpty;
    return kEmpty;
  }

 protected:
  DecodeContext() = default;
};

class EmptyDecodeContext : public DecodeContext {
 public:
  std::optional<std::string> message_definition(
      const std::string& type_name) const override {
    (void)type_name;
    return std::nullopt;
  }
};

struct DecodeResult {
  enum class Kind { kOk, kSkip, kError };

  Kind kind = Kind::kError;
  std::vector<VisualPrimitive> primitives;
  std::string reason;

  static DecodeResult Ok(std::vector<VisualPrimitive> primitives);
  static DecodeResult Skip(std::string reason);
  static DecodeResult Error(std::string reason);
};

class Decoder {
 public:
  virtual ~Decoder() = default;
  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;

  virtual std::string type_name() const = 0;
  virtual DecodeResult decode(const InputRecord& record,
                              DecodeContext& context) = 0;

 protected:
  Decoder() = default;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_CORE_DECODER_HPP_
