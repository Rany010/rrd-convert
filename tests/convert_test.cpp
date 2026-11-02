#include "app/run_cli.hpp"
#include "bag_fixture.hpp"
#include "cdr_writer.hpp"
#include "core/decoder.hpp"
#include "decode/read_dynamic.hpp"

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

int g_convert_seq = 0;

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

class ConvertDir : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("rrd-convert-loop-" + std::to_string(::getpid()) + "-" +
             std::to_string(++g_convert_seq));
    std::filesystem::create_directories(root_);
    bag_ = root_ / "input.db3";
    output_ = root_ / "out.rrd";
    summary_ = root_ / "summary.json";
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  std::filesystem::path root_;
  std::filesystem::path bag_;
  std::filesystem::path output_;
  std::filesystem::path summary_;
};

class TextDecoder : public Decoder {
 public:
  explicit TextDecoder(std::string type_name) : type_name_(std::move(type_name)) {}

  std::string type_name() const override { return type_name_; }

  DecodeResult decode(const InputRecord& record,
                      DecodeContext& context) override {
    if (!record.bytes.empty() && record.bytes[0] == 0xff) {
      return DecodeResult::Error("bad cdr");
    }
    const std::optional<std::string> definition =
        context.message_definition(record.type_name);
    TextPayload payload;
    payload.kind = TextPayload::Kind::kDocument;
    payload.text = definition.has_value() ? *definition : "decoded";
    VisualPrimitive primitive;
    primitive.topic = record.topic;
    primitive.log_time_ns = record.log_time_ns;
    primitive.payload = payload;
    return DecodeResult::Ok({std::move(primitive)});
  }

 private:
  std::string type_name_;
};

class ValueDecoder : public Decoder {
 public:
  std::string type_name() const override { return "demo_msgs/msg/Sample"; }

  DecodeResult decode(const InputRecord& record,
                      DecodeContext& context) override {
    DynamicValue value;
    std::string error;
    if (!read_dynamic_record(context, record, &value, &error)) {
      return DecodeResult::Error(error);
    }
    const DynamicValue* field = find_path(value, "value", &error);
    if (field == nullptr || field->kind != DynamicValue::Kind::kUint) {
      return DecodeResult::Error(error.empty() ? "missing value" : error);
    }
    TextPayload payload;
    payload.kind = TextPayload::Kind::kDocument;
    payload.text = std::to_string(field->uint_value);
    VisualPrimitive primitive;
    primitive.topic = record.topic;
    primitive.log_time_ns = record.log_time_ns;
    primitive.payload = std::move(payload);
    return DecodeResult::Ok({std::move(primitive)});
  }
};

class FileSink : public Sink {
 public:
  std::string name() const override { return "file"; }

  Status open(const std::string& path, const SinkOptions& options) override {
    output_.open(path, std::ios::out | std::ios::trunc);
    if (!output_) {
      return Status::Error(path, "failed to open output");
    }
    output_ << "root="
            << (options.root_frame.has_value() ? *options.root_frame : "")
            << "\n";
    return Status::Ok();
  }

  Status write(const VisualPrimitive& primitive) override {
    if (fail_writes_) {
      return Status::Error("", "sink rejected primitive");
    }
    const auto* text = std::get_if<TextPayload>(&primitive.payload);
    output_ << primitive.topic << " "
            << (text != nullptr ? text->text : "") << "\n";
    return Status::Ok();
  }

  Status close() override {
    output_.close();
    if (fail_close_) {
      return Status::Error("", "failed to close output");
    }
    return Status::Ok();
  }

  void set_fail_writes(bool fail) { fail_writes_ = fail; }
  void set_fail_close(bool fail) { fail_close_ = fail; }

 private:
  std::ofstream output_;
  bool fail_writes_ = false;
  bool fail_close_ = false;
};

class OpenFailureSink : public Sink {
 public:
  std::string name() const override { return "closed"; }
  Status open(const std::string& path, const SinkOptions&) override {
    return Status::Error(path, "failed to open output");
  }
  Status write(const VisualPrimitive&) override { return Status::Ok(); }
  Status close() override { return Status::Ok(); }
};

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

TEST_F(ConvertDir, SkipsMessagesWithoutADecoderAndRenamesTheOutput) {
  ASSERT_TRUE(write_ros2_db3(bag_.string(),
                             {{"/cam", "demo_msgs/msg/Sample", "cdr", 1, {1}}},
                             {}, false)
                  .empty());
  auto sink_factory = [](Registry& registry) {
    registry.add_sink("file", ".rrd", [] { return std::make_unique<FileSink>(); });
  };
  ArgList args{"rrd-convert", "convert", bag_.string(), "-o", output_.string(),
               "--root-frame", "map"};
  std::ostringstream err;
  const int code = run_cli(args.argc(), args.argv(), sink_factory, nullptr, &err);
  EXPECT_EQ(code, kExitOk);
  EXPECT_NE(err.str().find("no decoder"), std::string::npos);
  EXPECT_NE(err.str().find("total ok=0 skipped=1 failed=0\n"), std::string::npos);
  EXPECT_EQ(ReadFile(output_), "root=map\n");
  EXPECT_FALSE(std::filesystem::exists(output_.string() + ".tmp"));
}

TEST_F(ConvertDir, WritesDecodedPrimitivesAndSummaryJson) {
  ASSERT_TRUE(write_ros2_db3(
                  bag_.string(),
                  {{"/chatter", "std_msgs/msg/String", "cdr", 1, {1}},
                   {"/other", "std_msgs/msg/String", "cdr", 2, {2}}},
                  {{"std_msgs/msg/String", "ros2msg", "string data\n"}}, true)
                  .empty());
  auto extra = [](Registry& registry) {
    registry.add_decoder("std_msgs/msg/String", [] {
      return std::make_unique<TextDecoder>("std_msgs/msg/String");
    });
    registry.add_sink("file", ".rrd", [] { return std::make_unique<FileSink>(); });
  };
  ArgList args{"rrd-convert", "convert", bag_.string(), "-o", output_.string(),
               "--topics", "/chatter", "--summary-json", summary_.string(),
               "--root-frame", "map"};
  std::ostringstream err;
  const int code = run_cli(args.argc(), args.argv(), extra, nullptr, &err);
  EXPECT_EQ(code, kExitOk) << err.str();
  EXPECT_EQ(ReadFile(output_), "root=map\n/chatter string data\n\n");
  EXPECT_NE(summary_.string().empty(), true);
  const std::string json = ReadFile(summary_);
  EXPECT_NE(json.find("\"ok\":1"), std::string::npos);
  EXPECT_NE(json.find("\"skipped\":0"), std::string::npos);
  EXPECT_EQ(json.find("/other"), std::string::npos);
  EXPECT_FALSE(std::filesystem::exists(output_.string() + ".tmp"));
}

TEST_F(ConvertDir, ContinuesAfterADecodeError) {
  ASSERT_TRUE(
      write_ros2_db3(bag_.string(),
                     {{"/chatter", "std_msgs/msg/String", "cdr", 1, {0xff}},
                      {"/chatter", "std_msgs/msg/String", "cdr", 2, {1}}},
                     {}, false)
          .empty());
  auto extra = [](Registry& registry) {
    registry.add_decoder("std_msgs/msg/String", [] {
      return std::make_unique<TextDecoder>("std_msgs/msg/String");
    });
    registry.add_sink("file", ".rrd", [] { return std::make_unique<FileSink>(); });
  };
  ArgList args{"rrd-convert", "convert", bag_.string(), "-o", output_.string()};
  std::ostringstream err;
  const int code = run_cli(args.argc(), args.argv(), extra, nullptr, &err);
  EXPECT_EQ(code, kExitOk) << err.str();
  EXPECT_NE(err.str().find("bad cdr"), std::string::npos);
  EXPECT_NE(err.str().find("total ok=1 skipped=0 failed=1\n"), std::string::npos);
  EXPECT_NE(ReadFile(output_).find("/chatter decoded\n"), std::string::npos);
}

TEST_F(ConvertDir, CountsARejectedPrimitiveAndStillFinishes) {
  ASSERT_TRUE(write_ros2_db3(bag_.string(),
                             {{"/chatter", "std_msgs/msg/String", "cdr", 1, {1}}},
                             {}, false)
                  .empty());
  auto extra = [](Registry& registry) {
    registry.add_decoder("std_msgs/msg/String", [] {
      return std::make_unique<TextDecoder>("std_msgs/msg/String");
    });
    registry.add_sink("file", ".rrd", [] {
      auto sink = std::make_unique<FileSink>();
      sink->set_fail_writes(true);
      return sink;
    });
  };
  ArgList args{"rrd-convert", "convert", bag_.string(), "-o", output_.string()};
  std::ostringstream err;
  const int code = run_cli(args.argc(), args.argv(), extra, nullptr, &err);
  EXPECT_EQ(code, kExitOk) << err.str();
  EXPECT_NE(err.str().find("sink rejected primitive"), std::string::npos);
  EXPECT_NE(err.str().find("total ok=0 skipped=0 failed=1\n"), std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(output_));
  EXPECT_FALSE(std::filesystem::exists(output_.string() + ".tmp"));
}

TEST_F(ConvertDir, OpenOrCloseFailureLeavesNoOutput) {
  ASSERT_TRUE(write_ros2_db3(bag_.string(),
                             {{"/chatter", "std_msgs/msg/String", "cdr", 1, {1}}},
                             {}, false)
                  .empty());
  {
    auto extra = [](Registry& registry) {
      registry.add_sink("closed", ".rrd",
                        [] { return std::make_unique<OpenFailureSink>(); });
    };
    ArgList args{"rrd-convert", "convert", bag_.string(), "-o", output_.string()};
    std::ostringstream err;
    const int code = run_cli(args.argc(), args.argv(), extra, nullptr, &err);
    EXPECT_EQ(code, kExitIo);
    EXPECT_NE(err.str().find("failed to open output"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(output_));
    EXPECT_FALSE(std::filesystem::exists(output_.string() + ".tmp"));
  }
  {
    auto extra = [](Registry& registry) {
      registry.add_sink("file", ".rrd", [] {
        auto sink = std::make_unique<FileSink>();
        sink->set_fail_close(true);
        return sink;
      });
    };
    ArgList args{"rrd-convert", "convert", bag_.string(), "-o", output_.string(),
                 "--summary-json", summary_.string()};
    std::ostringstream err;
    const int code = run_cli(args.argc(), args.argv(), extra, nullptr, &err);
    EXPECT_EQ(code, kExitIo);
    EXPECT_NE(err.str().find("failed to close output"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(output_));
    EXPECT_FALSE(std::filesystem::exists(output_.string() + ".tmp"));
    EXPECT_FALSE(std::filesystem::exists(summary_));
  }
}

TEST_F(ConvertDir, ReadsACustomFieldFromMsgPath) {
  CdrWriter writer;
  writer.write_u32(7);
  ASSERT_TRUE(write_ros2_db3(bag_.string(),
                             {{"/sample", "demo_msgs/msg/Sample", "cdr", 1,
                               writer.bytes()}},
                             {}, false)
                  .empty());
  const std::filesystem::path msg_dir = root_ / "msgs" / "demo_msgs" / "msg";
  std::filesystem::create_directories(msg_dir);
  std::ofstream(msg_dir / "Sample.msg") << "uint32 value\n";

  auto extra = [](Registry& registry) {
    registry.add_decoder("demo_msgs/msg/Sample",
                         [] { return std::make_unique<ValueDecoder>(); });
    registry.add_sink("file", ".rrd", [] { return std::make_unique<FileSink>(); });
  };
  ArgList args{"rrd-convert", "convert", bag_.string(), "-o", output_.string(),
               "--msg-path", (root_ / "msgs").string()};
  std::ostringstream err;
  const int code = run_cli(args.argc(), args.argv(), extra, nullptr, &err);
  EXPECT_EQ(code, kExitOk) << err.str();
  EXPECT_NE(ReadFile(output_).find("/sample 7\n"), std::string::npos);
  EXPECT_NE(err.str().find("total ok=1 skipped=0 failed=0\n"), std::string::npos);
}

TEST_F(ConvertDir, CountsAnIdlDefinitionAsADecodeError) {
  CdrWriter writer;
  writer.write_u32(7);
  ASSERT_TRUE(write_ros2_db3(
                  bag_.string(),
                  {{"/sample", "demo_msgs/msg/Sample", "cdr", 1, writer.bytes()}},
                  {{"demo_msgs/msg/Sample", "ros2idl",
                    "struct Sample { uint32 value; };"}},
                  true)
                  .empty());
  auto extra = [](Registry& registry) {
    registry.add_decoder("demo_msgs/msg/Sample",
                         [] { return std::make_unique<ValueDecoder>(); });
    registry.add_sink("file", ".rrd", [] { return std::make_unique<FileSink>(); });
  };
  ArgList args{"rrd-convert", "convert", bag_.string(), "-o", output_.string()};
  std::ostringstream err;
  const int code = run_cli(args.argc(), args.argv(), extra, nullptr, &err);
  EXPECT_EQ(code, kExitOk) << err.str();
  EXPECT_NE(err.str().find("unsupported message definition encoding: ros2idl"),
            std::string::npos);
  EXPECT_NE(err.str().find("total ok=0 skipped=0 failed=1\n"), std::string::npos);
}

}  // namespace
}  // namespace rrd_convert
