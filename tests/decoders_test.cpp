#include "cdr_writer.hpp"
#include "decode/image_decoder.hpp"
#include "decode/point_cloud_decoder.hpp"
#include "decode/tf_decoder.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace rrd_convert {
namespace {

void WriteHeader(CdrWriter* writer, int32_t sec, uint32_t nanosec,
                 const std::string& frame_id) {
  writer->write_i32(sec);
  writer->write_u32(nanosec);
  writer->write_string(frame_id);
}

InputRecord Record(std::string topic, std::string type_name,
                   std::vector<uint8_t> bytes) {
  InputRecord record;
  record.topic = std::move(topic);
  record.type_name = std::move(type_name);
  record.serialization = "cdr";
  record.log_time_ns = 42;
  record.bytes = std::move(bytes);
  return record;
}

TEST(ImageDecoder, ReadsRgb8WithHeaderTimeAndStride) {
  CdrWriter writer;
  WriteHeader(&writer, 1, 2, "camera");
  writer.write_u32(1);
  writer.write_u32(2);
  writer.write_string("rgb8");
  writer.write_u8(0);
  writer.write_u32(8);
  const uint8_t pixels[] = {1, 2, 3, 9, 9, 4, 5, 6};
  writer.write_sequence_length(sizeof(pixels));
  writer.write_bytes(pixels, sizeof(pixels));

  EmptyDecodeContext context;
  const DecodeResult decoded =
      ImageDecoder().decode(Record("/cam", "sensor_msgs/msg/Image", writer.bytes()),
                            context);
  ASSERT_EQ(decoded.kind, DecodeResult::Kind::kOk);
  ASSERT_EQ(decoded.primitives.size(), 1u);
  const VisualPrimitive& primitive = decoded.primitives[0];
  EXPECT_EQ(primitive.topic, "/cam");
  EXPECT_EQ(primitive.frame_id, "camera");
  EXPECT_EQ(primitive.log_time_ns, 42);
  EXPECT_EQ(primitive.capture_time_ns, 1000000002);
  const auto* image = std::get_if<ImagePayload>(&primitive.payload);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->width, 2);
  EXPECT_EQ(image->height, 1);
  EXPECT_EQ(image->encoding, ImageEncoding::kRgb8);
  EXPECT_EQ(image->stride, 8);
  EXPECT_EQ(image->pixels, std::vector<uint8_t>(pixels, pixels + sizeof(pixels)));
  EXPECT_FALSE(is_depth_encoding(image->encoding));
}

TEST(ImageDecoder, SkipsAnUnsupportedEncodingAndKeepsDepth) {
  CdrWriter skipped;
  WriteHeader(&skipped, 0, 0, "camera");
  skipped.write_u32(1);
  skipped.write_u32(1);
  skipped.write_string("bayer_rggb8");
  skipped.write_u8(0);
  skipped.write_u32(1);
  skipped.write_sequence_length(1);
  skipped.write_u8(0);
  EmptyDecodeContext context;
  const DecodeResult unknown = ImageDecoder().decode(
      Record("/cam", "sensor_msgs/msg/Image", skipped.bytes()), context);
  EXPECT_EQ(unknown.kind, DecodeResult::Kind::kSkip);
  EXPECT_EQ(unknown.reason, "unsupported image encoding");

  CdrWriter depth;
  WriteHeader(&depth, 0, 0, "camera");
  depth.write_u32(1);
  depth.write_u32(1);
  depth.write_string("16UC1");
  depth.write_u8(0);
  depth.write_u32(2);
  const uint8_t sample[] = {0x2c, 0x01};
  depth.write_sequence_length(sizeof(sample));
  depth.write_bytes(sample, sizeof(sample));
  const DecodeResult decoded = ImageDecoder().decode(
      Record("/depth", "sensor_msgs/msg/Image", depth.bytes()), context);
  ASSERT_EQ(decoded.kind, DecodeResult::Kind::kOk);
  const auto* image = std::get_if<ImagePayload>(&decoded.primitives[0].payload);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->encoding, ImageEncoding::k16Uc1);
  EXPECT_TRUE(is_depth_encoding(image->encoding));
  EXPECT_FALSE(decoded.primitives[0].capture_time_ns.has_value());
}

TEST(ImageDecoder, RejectsAShortBufferAndUnknownEncapsulation) {
  CdrWriter writer;
  WriteHeader(&writer, 0, 0, "camera");
  writer.write_u32(2);
  writer.write_u32(2);
  writer.write_string("mono8");
  writer.write_u8(0);
  writer.write_u32(2);
  writer.write_sequence_length(1);
  writer.write_u8(0);
  EmptyDecodeContext context;
  const DecodeResult truncated = ImageDecoder().decode(
      Record("/cam", "sensor_msgs/msg/Image", writer.bytes()), context);
  EXPECT_EQ(truncated.kind, DecodeResult::Kind::kError);
  EXPECT_EQ(truncated.reason, "truncated image");

  const std::vector<uint8_t> xcdr2 = {0x00, 0x11, 0x00, 0x00};
  const DecodeResult unknown = ImageDecoder().decode(
      Record("/cam", "sensor_msgs/msg/Image", xcdr2), context);
  EXPECT_EQ(unknown.kind, DecodeResult::Kind::kError);
  EXPECT_NE(unknown.reason.find("unknown cdr encapsulation"), std::string::npos);
}

TEST(PointCloud2Decoder, ReadsPositionsAndPackedRgb) {
  CdrWriter writer;
  WriteHeader(&writer, 3, 0, "lidar");
  writer.write_u32(1);
  writer.write_u32(1);
  writer.write_sequence_length(4);
  writer.write_string("x");
  writer.write_u32(0);
  writer.write_u8(7);
  writer.write_u32(1);
  writer.write_string("y");
  writer.write_u32(4);
  writer.write_u8(7);
  writer.write_u32(1);
  writer.write_string("z");
  writer.write_u32(8);
  writer.write_u8(7);
  writer.write_u32(1);
  writer.write_string("rgb");
  writer.write_u32(12);
  writer.write_u8(7);
  writer.write_u32(1);
  writer.write_u8(0);
  writer.write_u32(16);
  writer.write_u32(16);
  uint8_t point[16];
  const float xyz[3] = {1.f, 2.f, 3.f};
  std::memcpy(point, xyz, sizeof(xyz));
  const uint32_t rgb = (0x11u << 16) | (0x22u << 8) | 0x33u;
  std::memcpy(point + 12, &rgb, sizeof(rgb));
  writer.write_sequence_length(sizeof(point));
  writer.write_bytes(point, sizeof(point));

  EmptyDecodeContext context;
  const DecodeResult decoded = PointCloud2Decoder().decode(
      Record("/points", "sensor_msgs/msg/PointCloud2", writer.bytes()), context);
  ASSERT_EQ(decoded.kind, DecodeResult::Kind::kOk) << decoded.reason;
  const auto* cloud =
      std::get_if<Points3DPayload>(&decoded.primitives[0].payload);
  ASSERT_NE(cloud, nullptr);
  ASSERT_EQ(cloud->positions.size(), 1u);
  EXPECT_FLOAT_EQ(cloud->positions[0].x, 1.f);
  EXPECT_FLOAT_EQ(cloud->positions[0].y, 2.f);
  EXPECT_FLOAT_EQ(cloud->positions[0].z, 3.f);
  ASSERT_EQ(cloud->colors.size(), 1u);
  EXPECT_EQ(cloud->colors[0].r, 0x11);
  EXPECT_EQ(cloud->colors[0].g, 0x22);
  EXPECT_EQ(cloud->colors[0].b, 0x33);
  EXPECT_EQ(cloud->colors[0].a, 255);
  EXPECT_EQ(decoded.primitives[0].capture_time_ns, 3000000000);
}

TEST(PointCloud2Decoder, MapsIntensityAcrossTheMessage) {
  CdrWriter writer;
  WriteHeader(&writer, 0, 0, "lidar");
  writer.write_u32(1);
  writer.write_u32(2);
  writer.write_sequence_length(4);
  writer.write_string("x");
  writer.write_u32(0);
  writer.write_u8(7);
  writer.write_u32(1);
  writer.write_string("y");
  writer.write_u32(4);
  writer.write_u8(7);
  writer.write_u32(1);
  writer.write_string("z");
  writer.write_u32(8);
  writer.write_u8(7);
  writer.write_u32(1);
  writer.write_string("intensity");
  writer.write_u32(12);
  writer.write_u8(7);
  writer.write_u32(1);
  writer.write_u8(0);
  writer.write_u32(16);
  writer.write_u32(32);
  uint8_t points[32] = {};
  const float first[] = {0.f, 0.f, 0.f, 0.f};
  const float second[] = {1.f, 1.f, 1.f, 10.f};
  std::memcpy(points, first, sizeof(first));
  std::memcpy(points + 16, second, sizeof(second));
  writer.write_sequence_length(sizeof(points));
  writer.write_bytes(points, sizeof(points));

  EmptyDecodeContext context;
  const DecodeResult decoded = PointCloud2Decoder().decode(
      Record("/points", "sensor_msgs/msg/PointCloud2", writer.bytes()), context);
  ASSERT_EQ(decoded.kind, DecodeResult::Kind::kOk) << decoded.reason;
  const auto* cloud =
      std::get_if<Points3DPayload>(&decoded.primitives[0].payload);
  ASSERT_NE(cloud, nullptr);
  ASSERT_EQ(cloud->colors.size(), 2u);
  EXPECT_EQ(cloud->colors[0].r, 0);
  EXPECT_EQ(cloud->colors[1].r, 255);
  EXPECT_TRUE(cloud->radii.empty());
}

TEST(PointCloud2Decoder, RequiresFloatXyz) {
  CdrWriter writer;
  WriteHeader(&writer, 0, 0, "lidar");
  writer.write_u32(1);
  writer.write_u32(1);
  writer.write_sequence_length(1);
  writer.write_string("x");
  writer.write_u32(0);
  writer.write_u8(7);
  writer.write_u32(1);
  writer.write_u8(0);
  writer.write_u32(4);
  writer.write_u32(4);
  writer.write_sequence_length(4);
  writer.write_bytes(std::vector<uint8_t>(4, 0).data(), 4);
  EmptyDecodeContext context;
  const DecodeResult decoded = PointCloud2Decoder().decode(
      Record("/points", "sensor_msgs/msg/PointCloud2", writer.bytes()), context);
  EXPECT_EQ(decoded.kind, DecodeResult::Kind::kError);
  EXPECT_EQ(decoded.reason, "point cloud requires float32 x, y, and z");
}

TEST(TfMessageDecoder, SplitsTransformsAndMarksStatic) {
  CdrWriter writer;
  writer.write_sequence_length(2);
  WriteHeader(&writer, 1, 0, "map");
  writer.write_string("base");
  writer.write_f64(1);
  writer.write_f64(2);
  writer.write_f64(3);
  writer.write_f64(0);
  writer.write_f64(0);
  writer.write_f64(0);
  writer.write_f64(1);
  WriteHeader(&writer, 0, 0, "base");
  writer.write_string("lidar");
  writer.write_f64(0);
  writer.write_f64(0);
  writer.write_f64(1);
  writer.write_f64(0);
  writer.write_f64(0);
  writer.write_f64(0);
  writer.write_f64(1);

  EmptyDecodeContext context;
  InputRecord record =
      Record("/tf_static", "tf2_msgs/msg/TFMessage", writer.bytes());
  const DecodeResult decoded = TfMessageDecoder().decode(record, context);
  ASSERT_EQ(decoded.kind, DecodeResult::Kind::kOk) << decoded.reason;
  ASSERT_EQ(decoded.primitives.size(), 2u);
  const auto* first =
      std::get_if<TransformPayload>(&decoded.primitives[0].payload);
  const auto* second =
      std::get_if<TransformPayload>(&decoded.primitives[1].payload);
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(first->parent_frame, "map");
  EXPECT_EQ(first->child_frame, "base");
  EXPECT_FLOAT_EQ(first->translation.x, 1.f);
  EXPECT_FLOAT_EQ(first->rotation.w, 1.f);
  EXPECT_TRUE(first->is_static);
  EXPECT_EQ(decoded.primitives[0].capture_time_ns, 1000000000);
  EXPECT_EQ(second->parent_frame, "base");
  EXPECT_EQ(second->child_frame, "lidar");
  EXPECT_TRUE(second->is_static);
  EXPECT_FALSE(decoded.primitives[1].capture_time_ns.has_value());

  record.topic = "/tf";
  const DecodeResult dynamic = TfMessageDecoder().decode(record, context);
  const auto* moving =
      std::get_if<TransformPayload>(&dynamic.primitives[0].payload);
  ASSERT_NE(moving, nullptr);
  EXPECT_FALSE(moving->is_static);
}

TEST(TfMessageDecoder, ReadsBagAlignment) {
  // ROS 2 records 8-byte fields on a 4-modulo-8 boundary. These payloads are
  // copied from a public RTAB-Map bag; measuring alignment from the header
  // byte truncates them or yields nonsense translations.
  const uint8_t kIdentity[] = {
      0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x64, 0x71, 0x62, 0x54,
      0x6d, 0x67, 0xbc, 0x35, 0x09, 0x00, 0x00, 0x00, 0x61, 0x7a, 0x33, 0x5f,
      0x6f, 0x64, 0x6f, 0x6d, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00,
      0x61, 0x7a, 0x33, 0x5f, 0x62, 0x61, 0x73, 0x65, 0x5f, 0x6c, 0x69, 0x6e,
      0x6b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
  };
  const uint8_t kCamera[] = {
      0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x65, 0x71, 0x62, 0x54,
      0x41, 0x32, 0x82, 0x05, 0x0a, 0x00, 0x00, 0x00, 0x62, 0x61, 0x73, 0x65,
      0x5f, 0x6c, 0x69, 0x6e, 0x6b, 0x00, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00,
      0x73, 0x74, 0x65, 0x72, 0x65, 0x6f, 0x5f, 0x63, 0x61, 0x6d, 0x65, 0x72,
      0x61, 0x5f, 0x62, 0x61, 0x73, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x7b, 0x14, 0xae, 0x47, 0xe1, 0x7a, 0x84, 0x3f, 0xb8, 0x1e, 0x85, 0xeb,
      0x51, 0xb8, 0xae, 0x3f, 0xcd, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xec, 0x3f,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa6, 0x95, 0xdb, 0x5c,
      0x8f, 0x8b, 0xc7, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x7e, 0x4f, 0x50, 0xb9, 0x36, 0x74, 0xef, 0x3f,
  };
  const uint8_t kPair[] = {
      0x00, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x65, 0x71, 0x62, 0x54,
      0x25, 0x1e, 0xdc, 0x17, 0x0f, 0x00, 0x00, 0x00, 0x62, 0x61, 0x73, 0x65,
      0x5f, 0x66, 0x6f, 0x6f, 0x74, 0x70, 0x72, 0x69, 0x6e, 0x74, 0x00, 0x00,
      0x0a, 0x00, 0x00, 0x00, 0x62, 0x61, 0x73, 0x65, 0x5f, 0x6c, 0x69, 0x6e,
      0x6b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77, 0xd2, 0xa9, 0xc8,
      0x74, 0x2a, 0xc2, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
      0x65, 0x71, 0x62, 0x54, 0x25, 0x1e, 0xdc, 0x17, 0x0a, 0x00, 0x00, 0x00,
      0x62, 0x61, 0x73, 0x65, 0x5f, 0x6c, 0x69, 0x6e, 0x6b, 0x00, 0x00, 0x00,
      0x10, 0x00, 0x00, 0x00, 0x62, 0x61, 0x73, 0x65, 0x5f, 0x6c, 0x61, 0x73,
      0x65, 0x72, 0x5f, 0x6c, 0x69, 0x6e, 0x6b, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0xb8, 0x1e, 0x85, 0xeb, 0x51, 0xb8, 0xce, 0x3f,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f,
  };

  EmptyDecodeContext context;
  const DecodeResult identity = TfMessageDecoder().decode(
      Record("/tf", "tf2_msgs/msg/TFMessage",
             std::vector<uint8_t>(std::begin(kIdentity), std::end(kIdentity))),
      context);
  ASSERT_EQ(identity.kind, DecodeResult::Kind::kOk) << identity.reason;
  const auto* fixed =
      std::get_if<TransformPayload>(&identity.primitives[0].payload);
  ASSERT_NE(fixed, nullptr);
  EXPECT_EQ(fixed->parent_frame, "az3_odom");
  EXPECT_EQ(fixed->child_frame, "az3_base_link");
  EXPECT_FLOAT_EQ(fixed->translation.x, 0.f);
  EXPECT_FLOAT_EQ(fixed->rotation.w, 1.f);

  const DecodeResult camera = TfMessageDecoder().decode(
      Record("/tf", "tf2_msgs/msg/TFMessage",
             std::vector<uint8_t>(std::begin(kCamera), std::end(kCamera))),
      context);
  ASSERT_EQ(camera.kind, DecodeResult::Kind::kOk) << camera.reason;
  const auto* mount =
      std::get_if<TransformPayload>(&camera.primitives[0].payload);
  ASSERT_NE(mount, nullptr);
  EXPECT_EQ(mount->parent_frame, "base_link");
  EXPECT_EQ(mount->child_frame, "stereo_camera_base");
  EXPECT_NEAR(mount->translation.x, 0.01f, 1e-6f);
  EXPECT_NEAR(mount->translation.y, 0.06f, 1e-6f);
  EXPECT_NEAR(mount->translation.z, 0.9f, 1e-6f);
  EXPECT_NEAR(mount->rotation.y, 0.18394653f, 1e-6f);
  EXPECT_NEAR(mount->rotation.w, 0.98293625f, 1e-6f);

  const DecodeResult pair = TfMessageDecoder().decode(
      Record("/tf", "tf2_msgs/msg/TFMessage",
             std::vector<uint8_t>(std::begin(kPair), std::end(kPair))),
      context);
  ASSERT_EQ(pair.kind, DecodeResult::Kind::kOk) << pair.reason;
  ASSERT_EQ(pair.primitives.size(), 2u);
  const auto* footprint =
      std::get_if<TransformPayload>(&pair.primitives[0].payload);
  const auto* laser =
      std::get_if<TransformPayload>(&pair.primitives[1].payload);
  ASSERT_NE(footprint, nullptr);
  ASSERT_NE(laser, nullptr);
  EXPECT_EQ(footprint->child_frame, "base_link");
  EXPECT_NEAR(footprint->translation.z, 0.14192066f, 1e-6f);
  EXPECT_EQ(laser->parent_frame, "base_link");
  EXPECT_EQ(laser->child_frame, "base_laser_link");
  EXPECT_NEAR(laser->translation.z, 0.24f, 1e-6f);
}

TEST(TfMessageDecoder, RejectsAnEmptyChildFrame) {
  CdrWriter writer;
  writer.write_sequence_length(1);
  WriteHeader(&writer, 0, 0, "map");
  writer.write_string("");
  writer.write_f64(0);
  writer.write_f64(0);
  writer.write_f64(0);
  writer.write_f64(0);
  writer.write_f64(0);
  writer.write_f64(0);
  writer.write_f64(1);
  EmptyDecodeContext context;
  const DecodeResult decoded = TfMessageDecoder().decode(
      Record("/tf", "tf2_msgs/msg/TFMessage", writer.bytes()), context);
  EXPECT_EQ(decoded.kind, DecodeResult::Kind::kError);
  EXPECT_EQ(decoded.reason, "transform is missing child_frame_id");
}

}  // namespace
}  // namespace rrd_convert
