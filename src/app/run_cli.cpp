#include "app/run_cli.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace rrd_convert {
namespace {

constexpr char kUsage[] =
    "usage: rrd-convert convert <bag-dir|file.db3> -o <out.rrd>\n"
    "          [--topics NAME,NAME] [--exclude-topics NAME,NAME]\n"
    "          [--root-frame FRAME]\n"
    "          [--msg-path DIR]...\n"
    "          [--summary-json PATH]\n";

ArgParseResult fail(const std::string& error) {
  ArgParseResult result;
  result.ok = false;
  result.exit_code = kExitUsage;
  result.message = "error: " + error + "\n";
  result.message += kUsage;
  return result;
}

std::string trim(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
    --end;
  }
  return text.substr(begin, end - begin);
}

bool is_option(const std::string& text) {
  return !text.empty() && text[0] == '-';
}

bool split_names(const std::string& text, std::vector<std::string>* names,
                 std::string* error) {
  names->clear();
  std::string current;
  auto flush = [&]() -> bool {
    const std::string name = trim(current);
    current.clear();
    if (name.empty()) {
      *error = "empty topic name";
      return false;
    }
    names->push_back(name);
    return true;
  };
  for (char ch : text) {
    if (ch == ',') {
      if (!flush()) {
        return false;
      }
    } else {
      current.push_back(ch);
    }
  }
  return flush();
}

const char* take_value(int argc, char** argv, int* index, const char* flag,
                       ArgParseResult* error) {
  if (*index + 1 >= argc || argv[*index + 1][0] == '\0' ||
      is_option(argv[*index + 1])) {
    *error = fail(std::string("missing value for ") + flag);
    return nullptr;
  }
  ++*index;
  return argv[*index];
}

void write_message(std::ostream& stream, const std::string& message) {
  stream << message;
  if (message.empty() || message.back() != '\n') {
    stream << '\n';
  }
}

}  // namespace

ArgParseResult parse_args(int argc, char** argv) {
  if (argc < 2) {
    return fail("missing command");
  }
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      ArgParseResult result;
      result.ok = false;
      result.exit_code = kExitOk;
      result.message = kUsage;
      return result;
    }
  }
  if (std::string(argv[1]) != "convert") {
    return fail("unknown command: " + std::string(argv[1]));
  }

  ConvertOptions options;
  bool saw_output = false;
  bool saw_topics = false;
  bool saw_exclude = false;
  bool saw_root = false;
  bool saw_summary = false;
  bool saw_input = false;

  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    ArgParseResult error;
    if (arg == "-o") {
      if (saw_output) {
        return fail("duplicate option: -o");
      }
      const char* value = take_value(argc, argv, &i, "-o", &error);
      if (value == nullptr) {
        return error;
      }
      options.output_path = value;
      saw_output = true;
    } else if (arg == "--topics") {
      if (saw_topics) {
        return fail("duplicate option: --topics");
      }
      const char* value = take_value(argc, argv, &i, "--topics", &error);
      if (value == nullptr) {
        return error;
      }
      std::string split_error;
      if (!split_names(value, &options.topic_filter.include, &split_error)) {
        return fail(split_error);
      }
      saw_topics = true;
    } else if (arg == "--exclude-topics") {
      if (saw_exclude) {
        return fail("duplicate option: --exclude-topics");
      }
      const char* value =
          take_value(argc, argv, &i, "--exclude-topics", &error);
      if (value == nullptr) {
        return error;
      }
      std::string split_error;
      if (!split_names(value, &options.topic_filter.exclude, &split_error)) {
        return fail(split_error);
      }
      saw_exclude = true;
    } else if (arg == "--root-frame") {
      if (saw_root) {
        return fail("duplicate option: --root-frame");
      }
      const char* value = take_value(argc, argv, &i, "--root-frame", &error);
      if (value == nullptr) {
        return error;
      }
      options.root_frame = value;
      saw_root = true;
    } else if (arg == "--msg-path") {
      const char* value = take_value(argc, argv, &i, "--msg-path", &error);
      if (value == nullptr) {
        return error;
      }
      options.msg_paths.emplace_back(value);
    } else if (arg == "--summary-json") {
      if (saw_summary) {
        return fail("duplicate option: --summary-json");
      }
      const char* value =
          take_value(argc, argv, &i, "--summary-json", &error);
      if (value == nullptr) {
        return error;
      }
      options.summary_json_path = value;
      saw_summary = true;
    } else if (is_option(arg)) {
      return fail("unknown option: " + arg);
    } else if (saw_input) {
      return fail("unexpected argument: " + arg);
    } else {
      options.input_path = arg;
      saw_input = true;
    }
  }

  if (!saw_input) {
    return fail("missing input path");
  }
  if (!saw_output) {
    return fail("missing -o");
  }

  ArgParseResult result;
  result.ok = true;
  result.exit_code = kExitOk;
  result.options = std::move(options);
  return result;
}

int run_cli(int argc, char** argv, RegisterFn extra, std::ostream* out,
            std::ostream* err) {
  if (out == nullptr) {
    out = &std::cout;
  }
  if (err == nullptr) {
    err = &std::cerr;
  }

  const ArgParseResult parsed = parse_args(argc, argv);
  if (!parsed.ok) {
    write_message(parsed.exit_code == kExitOk ? *out : *err, parsed.message);
    return parsed.exit_code;
  }

  Registry registry;
  register_builtins(&registry);
  if (extra) {
    extra(registry);
  }
  const ConvertResult converted = run_convert(parsed.options, &registry);
  if (converted.exit_code != kExitOk && !converted.error.empty()) {
    write_message(*err, converted.error);
  }
  if (!converted.summary.empty()) {
    write_message(*err, converted.summary);
  }
  return converted.exit_code;
}

}  // namespace rrd_convert
