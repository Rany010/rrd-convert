#include "app/convert_app.hpp"

#include "cdr/msg_parser.hpp"
#include "core/conversion_summary.hpp"
#include "core/decoder.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace rrd_convert {
namespace {

constexpr char kNoDecoder[] = "no decoder";

DefinitionSource to_definition_source(const ResolvedDefinition& resolved) {
  DefinitionSource source;
  source.encoding = resolved.encoding;
  source.text = resolved.text;
  source.error = resolved.error;
  switch (resolved.kind) {
    case ResolvedDefinition::Kind::kNone:
      source.kind = DefinitionSource::Kind::kNone;
      break;
    case ResolvedDefinition::Kind::kRos2Msg:
      source.kind = DefinitionSource::Kind::kRos2Msg;
      break;
    case ResolvedDefinition::Kind::kUnsupportedEncoding:
      source.kind = DefinitionSource::Kind::kUnsupportedEncoding;
      break;
    case ResolvedDefinition::Kind::kInvalid:
      source.kind = DefinitionSource::Kind::kInvalid;
      break;
  }
  return source;
}

class AdapterDecodeContext : public DecodeContext {
 public:
  AdapterDecodeContext(const FormatAdapter& adapter,
                       std::vector<std::string> msg_paths)
      : adapter_(adapter), msg_paths_(std::move(msg_paths)) {}

  std::optional<std::string> message_definition(
      const std::string& type_name) const override {
    const DefinitionSource source = definition_source(type_name);
    if (source.kind != DefinitionSource::Kind::kRos2Msg) {
      return std::nullopt;
    }
    return source.text;
  }

  DefinitionSource definition_source(const std::string& type_name) const override {
    const auto cached = cache_.find(type_name);
    if (cached != cache_.end()) {
      return cached->second;
    }
    const DefinitionSource source = to_definition_source(resolve_message_definition(
        type_name, adapter_.message_definition(type_name),
        adapter_.message_definition_encoding(type_name), msg_paths_));
    cache_.emplace(type_name, source);
    return source;
  }

  const std::vector<std::string>& message_paths() const override {
    return msg_paths_;
  }

 private:
  const FormatAdapter& adapter_;
  std::vector<std::string> msg_paths_;
  mutable std::unordered_map<std::string, DefinitionSource> cache_;
};

void remove_file(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

std::string write_summary_json(const std::string& path,
                               const ConversionSummary& summary) {
  std::ofstream output(path, std::ios::out | std::ios::trunc);
  if (!output) {
    return "failed to write summary: " + path;
  }
  output << summary.to_json();
  if (!output) {
    remove_file(path);
    return "failed to write summary: " + path;
  }
  return {};
}

void record_writes(const InputRecord& record, const DecodeResult& decoded,
                   Sink* sink, ConversionSummary* summary) {
  if (decoded.primitives.empty()) {
    summary->record(record.topic, record.type_name, SummaryOutcome::kOk, "");
    return;
  }
  for (const VisualPrimitive& primitive : decoded.primitives) {
    const Status written = sink->write(primitive);
    if (!written.ok()) {
      const std::string reason =
          written.reason().empty() ? "sink write failed" : written.reason();
      summary->record(record.topic, record.type_name, SummaryOutcome::kError,
                      reason);
      continue;
    }
    summary->record(record.topic, record.type_name, SummaryOutcome::kOk, "");
  }
}

}  // namespace

ConvertResult run_convert(const ConvertOptions& options, Registry* registry) {
  ConvertResult result;
  result.exit_code = kExitIo;

  std::error_code error;
  const bool exists = std::filesystem::exists(options.input_path, error);
  if (error) {
    result.error = "failed to stat input: " + options.input_path + ": " +
                   error.message();
    return result;
  }
  if (!exists) {
    result.error = "input not found: " + options.input_path;
    return result;
  }

  std::unique_ptr<FormatAdapter> adapter =
      registry->create_adapter(options.input_path);
  if (adapter == nullptr) {
    result.error = "no format adapter matches input: " + options.input_path;
    return result;
  }
  std::unique_ptr<Sink> sink =
      registry->create_sink_for_output(options.output_path);
  if (sink == nullptr) {
    result.error = "no sink matches output: " + options.output_path;
    return result;
  }

  const Status opened = adapter->open(options.input_path, options.topic_filter);
  if (!opened.ok()) {
    result.error = opened.message();
    return result;
  }

  const std::filesystem::path final_path(options.output_path);
  const std::filesystem::path temp_path = final_path.string() + ".tmp";
  std::error_code remove_error;
  if (std::filesystem::exists(temp_path, remove_error) && !remove_error) {
    std::filesystem::remove(temp_path, remove_error);
    if (remove_error) {
      result.error = "failed to remove temporary output: " + temp_path.string() +
                     ": " + remove_error.message();
      return result;
    }
  }

  SinkOptions sink_options;
  sink_options.root_frame = options.root_frame;
  const Status sink_opened = sink->open(temp_path.string(), sink_options);
  if (!sink_opened.ok()) {
    remove_file(temp_path);
    result.error = sink_opened.message();
    return result;
  }

  ConversionSummary summary;
  AdapterDecodeContext context(*adapter, options.msg_paths);
  std::unordered_map<std::string, std::unique_ptr<Decoder>> decoders;
  for (;;) {
    InputRecord record;
    const ReadResult read = adapter->next(&record);
    if (read.code == ReadCode::kDone) {
      break;
    }
    if (read.code == ReadCode::kError) {
      summary.record(record.topic, record.type_name, SummaryOutcome::kError,
                     read.reason);
      continue;
    }

    auto found = decoders.find(record.type_name);
    if (found == decoders.end()) {
      std::unique_ptr<Decoder> created =
          registry->create_decoder(record.type_name);
      found = decoders.emplace(record.type_name, std::move(created)).first;
    }
    if (found->second == nullptr) {
      summary.record(record.topic, record.type_name, SummaryOutcome::kSkip,
                     kNoDecoder);
      continue;
    }

    const DecodeResult decoded = found->second->decode(record, context);
    if (decoded.kind == DecodeResult::Kind::kSkip) {
      summary.record(record.topic, record.type_name, SummaryOutcome::kSkip,
                     decoded.reason);
      continue;
    }
    if (decoded.kind == DecodeResult::Kind::kError) {
      summary.record(record.topic, record.type_name, SummaryOutcome::kError,
                     decoded.reason);
      continue;
    }
    record_writes(record, decoded, sink.get(), &summary);
  }

  const Status closed = sink->close();
  result.summary = summary.to_text();
  if (!closed.ok()) {
    remove_file(temp_path);
    result.error = closed.message();
    return result;
  }

  if (options.summary_json_path.has_value()) {
    const std::string json_error =
        write_summary_json(*options.summary_json_path, summary);
    if (!json_error.empty()) {
      remove_file(temp_path);
      result.error = json_error;
      return result;
    }
  }

  std::error_code rename_error;
  std::filesystem::rename(temp_path, final_path, rename_error);
  if (rename_error) {
    remove_file(temp_path);
    if (options.summary_json_path.has_value()) {
      remove_file(*options.summary_json_path);
    }
    result.error = "failed to rename output: " + final_path.string() + ": " +
                   rename_error.message();
    return result;
  }

  result.exit_code = kExitOk;
  return result;
}

}  // namespace rrd_convert
