#include "app/run_cli.hpp"
#include "core/format_adapter.hpp"
#include "core/registry.hpp"
#include "core/sink.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace rrd_convert {
namespace {

class ArgList {
 public:
  explicit ArgList(std::initializer_list<std::string> args) : storage_(args) {
    for (std::string& arg : storage_) {
      argv_.push_back(arg.data());
    }
  }

  int argc() const { return static_cast<int>(argv_.size()); }
  char** argv() { return argv_.data(); }

 private:
  std::vector<std::string> storage_;
  std::vector<char*> argv_;
};

class TempPaths : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto dir = std::filesystem::temp_directory_path();
    const std::string suffix = std::to_string(::getpid());
    input_ = dir / ("rrd-convert-input-" + suffix + ".db3");
    output_ = dir / ("rrd-convert-output-" + suffix + ".rrd");
    std::ofstream(input_) << "x";
    std::filesystem::remove(output_);
    std::filesystem::remove(std::filesystem::path(output_.string() + ".tmp"));
  }

  void TearDown() override {
    std::filesystem::remove(input_);
    std::filesystem::remove(output_);
    std::filesystem::remove(std::filesystem::path(output_.string() + ".tmp"));
  }

  std::filesystem::path input_;
  std::filesystem::path output_;
};

struct Probe {
  bool adapter_opened = false;
  bool sink_opened = false;
};

class ProbeAdapter : public FormatAdapter {
 public:
  explicit ProbeAdapter(std::shared_ptr<Probe> probe) : probe_(std::move(probe)) {}

  Status open(const std::string& path, const TopicFilter& filter) override {
    (void)path;
    (void)filter;
    probe_->adapter_opened = true;
    return Status::Ok();
  }
  std::vector<TopicInfo> topics() const override { return {}; }
  std::optional<std::string> message_definition(
      const std::string& type_name) const override {
    (void)type_name;
    return std::nullopt;
  }
  ReadResult next(InputRecord* record) override {
    (void)record;
    return ReadResult::Done();
  }

 private:
  std::shared_ptr<Probe> probe_;
};

class ProbeSink : public Sink {
 public:
  explicit ProbeSink(std::shared_ptr<Probe> probe) : probe_(std::move(probe)) {}

  std::string name() const override { return "probe"; }
  Status open(const std::string& path, const SinkOptions& options) override {
    (void)path;
    (void)options;
    probe_->sink_opened = true;
    return Status::Ok();
  }
  Status write(const VisualPrimitive& primitive) override {
    (void)primitive;
    return Status::Ok();
  }
  Status close() override { return Status::Ok(); }

 private:
  std::shared_ptr<Probe> probe_;
};

TEST(ParseArgs, MissingCommandIsUsage) {
  ArgList args{"rrd-convert"};
  const ArgParseResult parsed = parse_args(args.argc(), args.argv());
  EXPECT_FALSE(parsed.ok);
  EXPECT_EQ(parsed.exit_code, kExitUsage);
  EXPECT_NE(parsed.message.find("missing command"), std::string::npos);
  EXPECT_NE(parsed.message.find("usage:"), std::string::npos);
}

TEST(ParseArgs, UnknownCommandIsUsage) {
  ArgList args{"rrd-convert", "inspect"};
  const ArgParseResult parsed = parse_args(args.argc(), args.argv());
  EXPECT_FALSE(parsed.ok);
  EXPECT_EQ(parsed.exit_code, kExitUsage);
  EXPECT_NE(parsed.message.find("unknown command: inspect"), std::string::npos);
}

TEST(ParseArgs, HelpExitsSuccessfully) {
  ArgList args{"rrd-convert", "--help"};
  const ArgParseResult parsed = parse_args(args.argc(), args.argv());
  EXPECT_FALSE(parsed.ok);
  EXPECT_EQ(parsed.exit_code, kExitOk);
  EXPECT_NE(parsed.message.find("usage:"), std::string::npos);
}

TEST(ParseArgs, ConvertRequiresInputAndOutput) {
  ArgList no_input{"rrd-convert", "convert"};
  const ArgParseResult missing_input = parse_args(no_input.argc(), no_input.argv());
  EXPECT_EQ(missing_input.exit_code, kExitUsage);
  EXPECT_NE(missing_input.message.find("missing input path"), std::string::npos);

  ArgList no_output{"rrd-convert", "convert", "in.db3"};
  const ArgParseResult missing_output =
      parse_args(no_output.argc(), no_output.argv());
  EXPECT_EQ(missing_output.exit_code, kExitUsage);
  EXPECT_NE(missing_output.message.find("missing -o"), std::string::npos);
}

TEST(ParseArgs, RejectsMissingValuesUnknownFlagsAndDuplicates) {
  ArgList missing_value{"rrd-convert", "convert", "in.db3", "-o"};
  EXPECT_NE(parse_args(missing_value.argc(), missing_value.argv())
                .message.find("missing value for -o"),
            std::string::npos);

  ArgList flag_as_value{"rrd-convert", "convert", "in.db3", "-o", "--topics"};
  EXPECT_NE(parse_args(flag_as_value.argc(), flag_as_value.argv())
                .message.find("missing value for -o"),
            std::string::npos);

  ArgList unknown{"rrd-convert", "convert", "in.db3", "-o", "out.rrd", "--nope"};
  EXPECT_NE(parse_args(unknown.argc(), unknown.argv())
                .message.find("unknown option: --nope"),
            std::string::npos);

  ArgList duplicate{"rrd-convert", "convert", "in.db3", "-o", "a.rrd",
                    "-o", "b.rrd"};
  EXPECT_NE(parse_args(duplicate.argc(), duplicate.argv())
                .message.find("duplicate option: -o"),
            std::string::npos);

  ArgList empty_topic{"rrd-convert", "convert", "in.db3", "-o", "out.rrd",
                      "--topics", "a,,b"};
  EXPECT_NE(parse_args(empty_topic.argc(), empty_topic.argv())
                .message.find("empty topic name"),
            std::string::npos);
}

TEST(ParseArgs, AcceptsFiltersRootFrameMsgPathsAndSummary) {
  ArgList args{"rrd-convert", "convert", "-o", "out.rrd",
               "--topics", " /cam, /lidar ",
               "--exclude-topics", "/tf",
               "--root-frame", "map",
               "--msg-path", "msgs/a",
               "--msg-path", "msgs/b",
               "--summary-json", "summary.json",
               "bag_dir"};
  const ArgParseResult parsed = parse_args(args.argc(), args.argv());
  ASSERT_TRUE(parsed.ok);
  EXPECT_EQ(parsed.exit_code, kExitOk);
  EXPECT_EQ(parsed.options.input_path, "bag_dir");
  EXPECT_EQ(parsed.options.output_path, "out.rrd");
  ASSERT_EQ(parsed.options.topic_filter.include.size(), 2u);
  EXPECT_EQ(parsed.options.topic_filter.include[0], "/cam");
  EXPECT_EQ(parsed.options.topic_filter.include[1], "/lidar");
  ASSERT_EQ(parsed.options.topic_filter.exclude.size(), 1u);
  EXPECT_EQ(parsed.options.topic_filter.exclude[0], "/tf");
  ASSERT_TRUE(parsed.options.root_frame.has_value());
  EXPECT_EQ(*parsed.options.root_frame, "map");
  ASSERT_EQ(parsed.options.msg_paths.size(), 2u);
  EXPECT_EQ(parsed.options.msg_paths[0], "msgs/a");
  EXPECT_EQ(parsed.options.msg_paths[1], "msgs/b");
  ASSERT_TRUE(parsed.options.summary_json_path.has_value());
  EXPECT_EQ(*parsed.options.summary_json_path, "summary.json");
}

TEST_F(TempPaths, MissingInputReturnsIoError) {
  std::filesystem::remove(input_);
  ArgList args{"rrd-convert", "convert", input_.string(), "-o", output_.string()};
  std::ostringstream out;
  std::ostringstream err;
  const int code = run_cli(args.argc(), args.argv(), {}, &out, &err);
  EXPECT_EQ(code, kExitIo);
  EXPECT_NE(err.str().find("input not found: " + input_.string()),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(output_));
}

TEST_F(TempPaths, ExistingInputWithoutAdapterReturnsIoError) {
  const auto other =
      input_.parent_path() / (input_.stem().string() + ".mcap");
  std::ofstream(other) << "x";
  ArgList args{"rrd-convert", "convert", other.string(), "-o", output_.string()};
  std::ostringstream out;
  std::ostringstream err;
  const int code = run_cli(args.argc(), args.argv(), {}, &out, &err);
  EXPECT_EQ(code, kExitIo);
  EXPECT_NE(err.str().find("no format adapter matches input: " + other.string()),
            std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(output_));
  std::filesystem::remove(other);
}

TEST_F(TempPaths, InvalidDb3FailsBeforeOpeningTheSink) {
  auto probe = std::make_shared<Probe>();
  RegisterFn extra = [probe](Registry& registry) {
    registry.add_adapter(
        "probe", [](const std::string&) { return true; },
        [probe] { return std::make_unique<ProbeAdapter>(probe); });
    registry.add_sink(
        "probe", ".rrd",
        [probe] { return std::make_unique<ProbeSink>(probe); });
  };
  ArgList args{"rrd-convert", "convert", input_.string(), "-o", output_.string()};
  std::ostringstream out;
  std::ostringstream err;
  const int code = run_cli(args.argc(), args.argv(), extra, &out, &err);
  EXPECT_EQ(code, kExitIo);
  EXPECT_NE(err.str().find("unrecognized rosbag2 table structure"),
            std::string::npos);
  EXPECT_FALSE(probe->adapter_opened);
  EXPECT_FALSE(probe->sink_opened);
  EXPECT_FALSE(std::filesystem::exists(output_));
  EXPECT_FALSE(std::filesystem::exists(output_.string() + ".tmp"));
}

TEST(RunCli, UsageGoesToTheErrorStream) {
  ArgList args{"rrd-convert"};
  std::ostringstream out;
  std::ostringstream err;
  const int code = run_cli(args.argc(), args.argv(), {}, &out, &err);
  EXPECT_EQ(code, kExitUsage);
  EXPECT_TRUE(out.str().empty());
  EXPECT_NE(err.str().find("usage:"), std::string::npos);
}

TEST(RunCli, HelpGoesToTheOutputStream) {
  ArgList args{"rrd-convert", "-h"};
  std::ostringstream out;
  std::ostringstream err;
  const int code = run_cli(args.argc(), args.argv(), {}, &out, &err);
  EXPECT_EQ(code, kExitOk);
  EXPECT_TRUE(err.str().empty());
  EXPECT_NE(out.str().find("usage:"), std::string::npos);
}

}  // namespace
}  // namespace rrd_convert
