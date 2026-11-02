#include "bag_fixture.hpp"
#include "cdr/cdr_reader.hpp"
#include "format/ros2_db3_adapter.hpp"

#include <gtest/gtest.h>

#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace rrd_convert {
namespace {

int g_bag_seq = 0;

class BagDir : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("rrd-db3-" + std::to_string(::getpid()) + "-" +
             std::to_string(++g_bag_seq));
    std::filesystem::create_directories(root_);
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  std::filesystem::path root_;
};

std::vector<InputRecord> ReadAll(Ros2Db3Adapter* adapter, std::string* error) {
  std::vector<InputRecord> records;
  for (;;) {
    InputRecord record;
    const ReadResult result = adapter->next(&record);
    if (result.code == ReadCode::kDone) {
      return records;
    }
    if (result.code == ReadCode::kError) {
      *error = result.reason;
      return records;
    }
    records.push_back(std::move(record));
  }
}

TEST(Ros2Db3Adapter, MatchesBagFilesDirectoriesAndCompressedNames) {
  EXPECT_TRUE(Ros2Db3Adapter::matches("/tmp/run.db3"));
  EXPECT_TRUE(Ros2Db3Adapter::matches("/tmp/run.db3.zstd"));
  EXPECT_FALSE(Ros2Db3Adapter::matches("/tmp/run.mcap"));
  EXPECT_TRUE(Ros2Db3Adapter::matches("/"));
}

TEST_F(BagDir, ReadsOneFileInTimestampOrder) {
  const auto path = root_ / "one.db3";
  const std::string error = write_ros2_db3(
      path.string(),
      {{"/cam", "sensor_msgs/msg/Image", "cdr", 30, {1}},
       {"/cam", "sensor_msgs/msg/Image", "cdr", 10, {2}},
       {"/lidar", "sensor_msgs/msg/PointCloud2", "cdr", 20, {3}}},
      {}, false);
  ASSERT_TRUE(error.empty()) << error;

  Ros2Db3Adapter adapter;
  const Status status = adapter.open(path.string(), {});
  ASSERT_TRUE(status.ok()) << status.message();
  ASSERT_EQ(adapter.topics().size(), 2u);
  EXPECT_EQ(adapter.topics()[0].name, "/cam");
  EXPECT_EQ(adapter.topics()[0].type_name, "sensor_msgs/msg/Image");
  EXPECT_FALSE(adapter.message_definition("sensor_msgs/msg/Image").has_value());

  std::string read_error;
  const std::vector<InputRecord> records = ReadAll(&adapter, &read_error);
  ASSERT_TRUE(read_error.empty()) << read_error;
  ASSERT_EQ(records.size(), 3u);
  EXPECT_EQ(records[0].log_time_ns, 10);
  EXPECT_EQ(records[0].bytes, std::vector<uint8_t>({2}));
  EXPECT_EQ(records[1].topic, "/lidar");
  EXPECT_EQ(records[1].serialization, "cdr");
  EXPECT_EQ(records[2].log_time_ns, 30);
  EXPECT_EQ(adapter.next(nullptr).code, ReadCode::kDone);
}

TEST_F(BagDir, FiltersTopicsInTheQuery) {
  const auto path = root_ / "filtered.db3";
  ASSERT_TRUE(write_ros2_db3(path.string(),
                             {{"/a", "std_msgs/msg/String", "cdr", 1, {9}},
                              {"/b", "std_msgs/msg/String", "cdr", 2, {8}},
                              {"/c", "std_msgs/msg/String", "cdr", 3, {7}}},
                             {}, false)
                  .empty());
  TopicFilter filter;
  filter.include = {"/a", "/b"};
  filter.exclude = {"/b"};
  Ros2Db3Adapter adapter;
  ASSERT_TRUE(adapter.open(path.string(), filter).ok());
  ASSERT_EQ(adapter.topics().size(), 1u);
  EXPECT_EQ(adapter.topics()[0].name, "/a");
  std::string read_error;
  const std::vector<InputRecord> records = ReadAll(&adapter, &read_error);
  ASSERT_TRUE(read_error.empty()) << read_error;
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records[0].bytes, std::vector<uint8_t>({9}));
}

TEST_F(BagDir, MergesSplitsByTimestampAndKeepsDefinitions) {
  ASSERT_TRUE(write_ros2_db3(
                  (root_ / "bag_1.db3").string(),
                  {{"/cam", "sensor_msgs/msg/Image", "cdr", 20, {2}},
                   {"/lidar", "sensor_msgs/msg/PointCloud2", "cdr", 40, {4}}},
                  {{"sensor_msgs/msg/Image", "ros2idl", "struct Image {}"}},
                  true)
                  .empty());
  ASSERT_TRUE(write_ros2_db3(
                  (root_ / "bag_0.db3").string(),
                  {{"/cam", "sensor_msgs/msg/Image", "cdr", 5, {1}},
                   {"/lidar", "sensor_msgs/msg/PointCloud2", "cdr", 5, {3}}},
                  {{"sensor_msgs/msg/Image", "ros2msg", "uint8[] data\n"},
                   {"sensor_msgs/msg/Image", "ros2idl", "struct Image {}"}},
                  true)
                  .empty());

  Ros2Db3Adapter adapter;
  const Status status = adapter.open(root_.string(), {});
  ASSERT_TRUE(status.ok()) << status.message();
  std::string read_error;
  const std::vector<InputRecord> records = ReadAll(&adapter, &read_error);
  ASSERT_TRUE(read_error.empty()) << read_error;
  ASSERT_EQ(records.size(), 4u);
  EXPECT_EQ(records[0].bytes, std::vector<uint8_t>({1}));
  EXPECT_EQ(records[1].bytes, std::vector<uint8_t>({3}));
  EXPECT_EQ(records[2].bytes, std::vector<uint8_t>({2}));
  EXPECT_EQ(records[3].bytes, std::vector<uint8_t>({4}));
  ASSERT_TRUE(adapter.message_definition("sensor_msgs/msg/Image").has_value());
  EXPECT_EQ(*adapter.message_definition("sensor_msgs/msg/Image"), "uint8[] data\n");
  ASSERT_TRUE(adapter.message_definition_encoding("sensor_msgs/msg/Image").has_value());
  EXPECT_EQ(*adapter.message_definition_encoding("sensor_msgs/msg/Image"), "ros2msg");
}

TEST_F(BagDir, RejectsCompressionAndAMissingDatabase) {
  ASSERT_TRUE(write_ros2_db3((root_ / "bag_0.db3").string(), {}, {}, false).empty());
  {
    std::ofstream(root_ / "bag_0.db3.zstd") << "compressed";
    Ros2Db3Adapter adapter;
    const Status status = adapter.open(root_.string(), {});
    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.message().find("compressed bags are not supported"),
              std::string::npos);
    EXPECT_NE(status.message().find(root_.string()), std::string::npos);
    std::filesystem::remove(root_ / "bag_0.db3.zstd");
  }
  {
    std::ofstream(root_ / "metadata.yaml") << "compression_format: \"zstd\"\n";
    Ros2Db3Adapter adapter;
    const Status status = adapter.open(root_.string(), {});
    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.message().find("compression_format is zstd"),
              std::string::npos);
  }
  {
    std::ofstream(root_ / "metadata.yaml") << "compression_format: \"\"\n";
    Ros2Db3Adapter adapter;
    EXPECT_TRUE(adapter.open(root_.string(), {}).ok());
  }
  {
    const auto file = root_ / "direct.db3";
    ASSERT_TRUE(write_ros2_db3(file.string(), {}, {}, false).empty());
    std::ofstream(root_ / "ignored.zstd") << "x";
    Ros2Db3Adapter adapter;
    EXPECT_TRUE(adapter.open(file.string(), {}).ok());
  }
  {
    std::filesystem::remove(root_ / "bag_0.db3");
    std::filesystem::remove(root_ / "direct.db3");
    std::filesystem::remove(root_ / "ignored.zstd");
    Ros2Db3Adapter adapter;
    const Status status = adapter.open(root_.string(), {});
    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.message().find("no .db3 files"), std::string::npos);
  }
}

TEST_F(BagDir, ReportsAnUnrecognizedOrCorruptFile) {
  const auto broken = root_ / "broken.db3";
  {
    std::ofstream(broken) << "this is not sqlite";
    Ros2Db3Adapter adapter;
    const Status status = adapter.open(broken.string(), {});
    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.message().find(broken.string()), std::string::npos);
    EXPECT_NE(status.message().find("sqlite"), std::string::npos);
  }
  {
    std::filesystem::remove(broken);
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(broken.string().c_str(), &db), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(db, "CREATE TABLE topics(id INTEGER);", nullptr,
                           nullptr, nullptr),
              SQLITE_OK);
    sqlite3_close(db);
    Ros2Db3Adapter adapter;
    const Status status = adapter.open(broken.string(), {});
    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.message().find("unrecognized rosbag2 table structure"),
              std::string::npos);
    EXPECT_NE(status.message().find(broken.string()), std::string::npos);
  }
  {
    const auto zstd = root_ / "clip.db3.zstd";
    std::ofstream(zstd) << "x";
    Ros2Db3Adapter adapter;
    const Status status = adapter.open(zstd.string(), {});
    EXPECT_FALSE(status.ok());
    EXPECT_NE(status.message().find("compressed bags are not supported"),
              std::string::npos);
  }
}

TEST_F(BagDir, ReadsASampleFixture) {
  const auto path = root_ / "sample.db3";
  ASSERT_TRUE(write_sample_fixture(path.string()).empty());
  Ros2Db3Adapter adapter;
  ASSERT_TRUE(adapter.open(path.string(), {}).ok());
  std::string read_error;
  const std::vector<InputRecord> records = ReadAll(&adapter, &read_error);
  ASSERT_TRUE(read_error.empty()) << read_error;
  ASSERT_EQ(records.size(), 2u);

  CdrReader chatter(records[0].bytes.data(), records[0].bytes.size());
  std::string text;
  ASSERT_TRUE(chatter.read_string(&text));
  EXPECT_EQ(text, "hello");
  EXPECT_EQ(records[0].type_name, "std_msgs/msg/String");

  CdrReader sample(records[1].bytes.data(), records[1].bytes.size());
  uint32_t value = 0;
  ASSERT_TRUE(sample.read_u32(&value));
  EXPECT_EQ(value, 7u);
  ASSERT_TRUE(adapter.message_definition("demo_msgs/msg/Sample").has_value());
  EXPECT_EQ(*adapter.message_definition("demo_msgs/msg/Sample"), "uint32 value\n");
}

TEST_F(BagDir, ReportsIdlWithoutTreatingItAsRos2Msg) {
  ASSERT_TRUE(write_ros2_db3(
                  (root_ / "bag_0.db3").string(),
                  {{"/a", "demo_msgs/msg/Sample", "cdr", 1, {1}}},
                  {{"demo_msgs/msg/Sample", "ros2idl",
                    "struct Sample { uint32 value; };"}},
                  true)
                  .empty());
  Ros2Db3Adapter adapter;
  ASSERT_TRUE(adapter.open(root_.string(), {}).ok());
  EXPECT_FALSE(adapter.message_definition("demo_msgs/msg/Sample").has_value());
  ASSERT_TRUE(
      adapter.message_definition_encoding("demo_msgs/msg/Sample").has_value());
  EXPECT_EQ(*adapter.message_definition_encoding("demo_msgs/msg/Sample"),
            "ros2idl");
}

TEST(Ros2Db3Adapter, NextBeforeOpenIsAnError) {
  Ros2Db3Adapter adapter;
  InputRecord record;
  const ReadResult result = adapter.next(&record);
  EXPECT_EQ(result.code, ReadCode::kError);
  EXPECT_NE(result.reason.find("not open"), std::string::npos);
}

}  // namespace
}  // namespace rrd_convert
