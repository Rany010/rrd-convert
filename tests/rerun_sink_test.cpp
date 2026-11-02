#include "sink/rerun_sink.hpp"
#include "sink/root_frame.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rrd_convert {
namespace {

int g_sink_seq = 0;

TEST(RootFrame, UsesTheRequestedFrame) {
  const std::unordered_map<std::string, int> parents{{"map", 1}};
  const std::unordered_set<std::string> children{"base"};
  EXPECT_EQ(select_root_frame(std::string("odom"), parents, children), "odom");
}

TEST(RootFrame, PicksTheMostCommonParentThatIsNeverAChild) {
  const std::unordered_map<std::string, int> parents{{"map", 1}, {"base", 4}};
  const std::unordered_set<std::string> children{"base", "lidar"};
  EXPECT_EQ(select_root_frame(std::nullopt, parents, children), "map");
}

TEST(RootFrame, BreaksTiesByName) {
  const std::unordered_map<std::string, int> parents{{"world", 2}, {"map", 2}};
  const std::unordered_set<std::string> children;
  EXPECT_EQ(select_root_frame(std::nullopt, parents, children), "map");
}

TEST(RerunSink, WritesAnRrdForImagePointsAndTransforms) {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("rrd-sink-" + std::to_string(::getpid()) + "-" +
       std::to_string(++g_sink_seq));
  std::filesystem::create_directories(root);
  const std::filesystem::path output = root / "out.rrd";

  RerunSink sink;
  ASSERT_TRUE(sink.open(output.string(), SinkOptions{}).ok());

  VisualPrimitive image;
  image.topic = "/cam";
  image.frame_id = "camera";
  image.log_time_ns = 1000;
  image.capture_time_ns = 900;
  ImagePayload pixels;
  pixels.width = 1;
  pixels.height = 1;
  pixels.encoding = ImageEncoding::kRgb8;
  pixels.stride = 3;
  pixels.pixels = {9, 8, 7};
  image.payload = pixels;
  EXPECT_TRUE(sink.write(image).ok());

  VisualPrimitive cloud;
  cloud.topic = "/lidar";
  cloud.frame_id = "lidar";
  cloud.log_time_ns = 1000;
  Points3DPayload points;
  points.positions.push_back(Vec3f{1.f, 2.f, 3.f});
  cloud.payload = points;
  EXPECT_TRUE(sink.write(cloud).ok());

  VisualPrimitive tf;
  tf.topic = "/tf";
  tf.log_time_ns = 1000;
  TransformPayload base;
  base.parent_frame = "map";
  base.child_frame = "base";
  base.rotation.w = 1.f;
  tf.payload = base;
  EXPECT_TRUE(sink.write(tf).ok());
  TransformPayload lidar;
  lidar.parent_frame = "base";
  lidar.child_frame = "lidar";
  lidar.rotation.w = 1.f;
  tf.payload = lidar;
  EXPECT_TRUE(sink.write(tf).ok());

  VisualPrimitive text;
  text.topic = "/notes";
  text.log_time_ns = 1000;
  text.payload = TextPayload{};
  const Status rejected = sink.write(text);
  EXPECT_FALSE(rejected.ok());
  EXPECT_EQ(rejected.reason(), "unsupported primitive");

  ASSERT_TRUE(sink.close().ok());
  std::ifstream input(output, std::ios::binary);
  char magic[4] = {};
  input.read(magic, 4);
  EXPECT_EQ(std::string(magic, 4), "RRF2");
  EXPECT_GT(std::filesystem::file_size(output), 4u);
  std::filesystem::remove_all(root);
}

TEST(RerunSink, WritesEncodedVideoShapesAndClears) {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("rrd-sink-" + std::to_string(::getpid()) + "-" +
       std::to_string(++g_sink_seq));
  std::filesystem::create_directories(root);
  const std::filesystem::path output = root / "out.rrd";

  RerunSink sink;
  ASSERT_TRUE(sink.open(output.string(), SinkOptions{}).ok());

  VisualPrimitive encoded;
  encoded.topic = "/cam/compressed";
  encoded.frame_id = "camera";
  encoded.log_time_ns = 10;
  EncodedImagePayload jpeg;
  jpeg.bytes = {0xff, 0xd8, 0xff, 0xd9};
  encoded.payload = jpeg;
  EXPECT_TRUE(sink.write(encoded).ok());

  VisualPrimitive video;
  video.topic = "/cam/video";
  video.frame_id = "camera";
  video.log_time_ns = 20;
  VideoFramePayload frame;
  frame.codec = VideoFramePayload::Codec::kH264;
  frame.keyframe = true;
  frame.annex_b = {0x00, 0x00, 0x00, 0x01, 0x65};
  video.payload = frame;
  EXPECT_TRUE(sink.write(video).ok());
  frame.keyframe = false;
  frame.annex_b = {0x00, 0x00, 0x00, 0x01, 0x41};
  video.log_time_ns = 30;
  video.payload = frame;
  EXPECT_TRUE(sink.write(video).ok());

  VisualPrimitive marker;
  marker.topic = "/markers";
  marker.instance = "obstacles/3";
  marker.frame_id = "map";
  marker.log_time_ns = 40;
  Shapes3DPayload shapes;
  Shape3D box;
  box.kind = ShapeKind::kBox;
  box.center = Vec3f{1.f, 2.f, 3.f};
  box.half_sizes = Vec3f{0.5f, 0.5f, 0.5f};
  shapes.shapes.push_back(box);
  marker.payload = shapes;
  EXPECT_TRUE(sink.write(marker).ok());

  VisualPrimitive clear;
  clear.topic = "/markers";
  clear.log_time_ns = 50;
  clear.payload = ClearPayload{true};
  EXPECT_TRUE(sink.write(clear).ok());

  ASSERT_TRUE(sink.close().ok());
  std::ifstream input(output, std::ios::binary);
  char magic[4] = {};
  input.read(magic, 4);
  EXPECT_EQ(std::string(magic, 4), "RRF2");
  std::filesystem::remove_all(root);
}

}  // namespace
}  // namespace rrd_convert
