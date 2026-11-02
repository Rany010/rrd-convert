#include "core/conversion_summary.hpp"
#include "core/decoder.hpp"
#include "core/format_adapter.hpp"
#include "core/registry.hpp"
#include "core/status.hpp"
#include "core/visual_primitive.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace rrd_convert {
namespace {

class TagDecoder : public Decoder {
 public:
  TagDecoder(std::shared_ptr<std::string> tag, std::string value)
      : tag_(std::move(tag)), value_(std::move(value)) {}

  std::string type_name() const override { return "demo/msg/Value"; }

  DecodeResult decode(const InputRecord& record,
                      DecodeContext& context) override {
    (void)record;
    (void)context;
    *tag_ = value_;
    return DecodeResult::Skip("unused");
  }

 private:
  std::shared_ptr<std::string> tag_;
  std::string value_;
};

class CountingAdapter : public FormatAdapter {
 public:
  Status open(const std::string& path, const TopicFilter& filter) override {
    (void)path;
    (void)filter;
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
};

TEST(Status, ErrorMessageIncludesPathAndReason) {
  const Status status = Status::Error("bag.db3", "unsupported compression");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.path(), "bag.db3");
  EXPECT_EQ(status.reason(), "unsupported compression");
  EXPECT_EQ(status.message(), "bag.db3: unsupported compression");
}

TEST(Status, OkMessageIsEmpty) {
  const Status status = Status::Ok();
  EXPECT_TRUE(status.ok());
  EXPECT_TRUE(status.path().empty());
  EXPECT_EQ(status.message(), "");
}

TEST(CaptureTime, ZeroStampIsAbsent) {
  VisualPrimitive primitive;
  set_capture_time(&primitive, 0);
  EXPECT_FALSE(primitive.capture_time_ns.has_value());

  set_capture_time(&primitive, 5);
  ASSERT_TRUE(primitive.capture_time_ns.has_value());
  EXPECT_EQ(*primitive.capture_time_ns, 5);

  set_capture_time(&primitive, 0);
  EXPECT_FALSE(primitive.capture_time_ns.has_value());
}

TEST(TopicFilter, IncludeIsExactAndExcludeWins) {
  TopicFilter filter;
  filter.include = {"/cam", "/lidar"};
  filter.exclude = {"/lidar"};

  EXPECT_TRUE(filter.allows("/cam"));
  EXPECT_FALSE(filter.allows("/lidar"));
  EXPECT_FALSE(filter.allows("/other"));
  EXPECT_FALSE(filter.allows("/cam/image"));
}

TEST(TopicFilter, EmptyIncludeAllowsEverythingExceptExcludes) {
  TopicFilter filter;
  filter.exclude = {"/tf"};
  EXPECT_TRUE(filter.allows("/cam"));
  EXPECT_FALSE(filter.allows("/tf"));
}

TEST(DecodeResult, SkipAndErrorKeepTheReason) {
  const DecodeResult skipped = DecodeResult::Skip("unsupported image encoding");
  EXPECT_EQ(skipped.kind, DecodeResult::Kind::kSkip);
  EXPECT_EQ(skipped.reason, "unsupported image encoding");
  EXPECT_TRUE(skipped.primitives.empty());

  const DecodeResult failed = DecodeResult::Error("unknown cdr encapsulation");
  EXPECT_EQ(failed.kind, DecodeResult::Kind::kError);
  EXPECT_EQ(failed.reason, "unknown cdr encapsulation");
}

TEST(Registry, FirstMatchingAdapterIsCreatedEachTime) {
  Registry registry;
  int db3_calls = 0;
  int late_calls = 0;
  registry.add_adapter(
      "db3",
      [](const std::string& path) {
        return path.size() >= 4 &&
               path.compare(path.size() - 4, 4, ".db3") == 0;
      },
      [&db3_calls] {
        ++db3_calls;
        return std::make_unique<CountingAdapter>();
      });
  registry.add_adapter(
      "db3-late",
      [](const std::string& path) {
        return path.size() >= 4 &&
               path.compare(path.size() - 4, 4, ".db3") == 0;
      },
      [&late_calls] {
        ++late_calls;
        return std::make_unique<CountingAdapter>();
      });

  EXPECT_EQ(registry.create_adapter("clip.mcap"), nullptr);
  auto first = registry.create_adapter("clip.db3");
  auto second = registry.create_adapter("clip.db3");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first.get(), second.get());
  EXPECT_EQ(db3_calls, 2);
  EXPECT_EQ(late_calls, 0);
}

TEST(Registry, LaterDecoderRegistrationReplacesTheFactory) {
  Registry registry;
  auto tag = std::make_shared<std::string>();
  registry.add_decoder("demo/msg/Value", [tag] {
    return std::make_unique<TagDecoder>(tag, "first");
  });
  registry.add_decoder("demo/msg/Value", [tag] {
    return std::make_unique<TagDecoder>(tag, "second");
  });

  EmptyDecodeContext context;
  InputRecord record;
  auto decoder = registry.create_decoder("demo/msg/Value");
  ASSERT_NE(decoder, nullptr);
  EXPECT_EQ(registry.create_decoder("missing/msg/Type"), nullptr);
  decoder->decode(record, context);
  EXPECT_EQ(*tag, "second");
}

TEST(Registry, SinkIsSelectedByOutputExtension) {
  Registry registry;
  auto tag = std::make_shared<std::string>();
  registry.add_sink("rerun", ".rrd", [tag] {
    *tag = "rerun";
    class NamedSink : public Sink {
     public:
      std::string name() const override { return "rerun"; }
      Status open(const std::string&, const SinkOptions&) override {
        return Status::Ok();
      }
      Status write(const VisualPrimitive&) override { return Status::Ok(); }
      Status close() override { return Status::Ok(); }
    };
    return std::make_unique<NamedSink>();
  });

  EXPECT_EQ(registry.create_sink_for_output("out.mcap"), nullptr);
  EXPECT_EQ(registry.create_sink_for_output("out.rrd.bak"), nullptr);
  auto sink = registry.create_sink_for_output("dir/out.rrd");
  ASSERT_NE(sink, nullptr);
  EXPECT_EQ(sink->name(), "rerun");
  EXPECT_EQ(*tag, "rerun");
}

TEST(ConversionSummary, GroupsByTopicTypeAndReason) {
  ConversionSummary summary;
  summary.record("/cam", "sensor_msgs/msg/Image", SummaryOutcome::kOk, "");
  summary.record("/cam", "sensor_msgs/msg/Image", SummaryOutcome::kSkip,
                 "unsupported image encoding");
  summary.record("/cam", "sensor_msgs/msg/Image", SummaryOutcome::kSkip,
                 "unsupported image encoding");
  summary.record("a\"b", "demo/msg/Value", SummaryOutcome::kError, "bad\nbyte");

  EXPECT_EQ(summary.ok_count(), 1);
  EXPECT_EQ(summary.skip_count(), 2);
  EXPECT_EQ(summary.error_count(), 1);
  ASSERT_EQ(summary.rows().size(), 3u);
  EXPECT_EQ(summary.rows()[1].skip_count, 2);

  EXPECT_EQ(
      summary.to_json(),
      "{\"ok\":1,\"skipped\":2,\"failed\":1,\"rows\":["
      "{\"topic\":\"/cam\",\"type\":\"sensor_msgs/msg/Image\",\"reason\":\"\","
      "\"ok\":1,\"skipped\":0,\"failed\":0},"
      "{\"topic\":\"/cam\",\"type\":\"sensor_msgs/msg/Image\","
      "\"reason\":\"unsupported image encoding\","
      "\"ok\":0,\"skipped\":2,\"failed\":0},"
      "{\"topic\":\"a\\\"b\",\"type\":\"demo/msg/Value\","
      "\"reason\":\"bad\\nbyte\",\"ok\":0,\"skipped\":0,\"failed\":1}]}");

  const std::string text = summary.to_text();
  EXPECT_NE(text.find("topic=/cam type=sensor_msgs/msg/Image ok=1 "
                      "skipped=0 failed=0\n"),
            std::string::npos);
  EXPECT_NE(text.find("reason=unsupported image encoding"), std::string::npos);
  EXPECT_NE(text.find("total ok=1 skipped=2 failed=1\n"), std::string::npos);
}

}  // namespace
}  // namespace rrd_convert
