#include "cdr_writer.hpp"
#include "decode/compressed_image_decoder.hpp"
#include "decode/compressed_video_decoder.hpp"
#include "decode/marker_decoder.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
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

InputRecord Record(std::string topic, std::vector<uint8_t> bytes) {
  InputRecord record;
  record.topic = std::move(topic);
  record.serialization = "cdr";
  record.log_time_ns = 42;
  record.bytes = std::move(bytes);
  return record;
}

void WriteColor(CdrWriter* writer, float red, float green, float blue,
                float alpha) {
  writer->write_f32(red);
  writer->write_f32(green);
  writer->write_f32(blue);
  writer->write_f32(alpha);
}

void WriteMarkerTail(CdrWriter* writer,
                     const std::vector<std::array<double, 3>>& points) {
  writer->write_i32(0);
  writer->write_u32(0);
  writer->write_u8(0);
  writer->write_sequence_length(static_cast<uint32_t>(points.size()));
  for (const std::array<double, 3>& point : points) {
    writer->write_f64(point[0]);
    writer->write_f64(point[1]);
    writer->write_f64(point[2]);
  }
  writer->write_sequence_length(0);
  writer->write_string("");
  writer->write_string("");
  writer->write_u8(0);
}

void WriteMarker(CdrWriter* writer, const std::string& ns, int32_t id,
                 int32_t type, int32_t action, double scale_x,
                 const std::vector<std::array<double, 3>>& points) {
  WriteHeader(writer, 1, 0, "map");
  writer->write_string(ns);
  writer->write_i32(id);
  writer->write_i32(type);
  writer->write_i32(action);
  writer->write_f64(1.0);
  writer->write_f64(2.0);
  writer->write_f64(3.0);
  writer->write_f64(0.0);
  writer->write_f64(0.0);
  writer->write_f64(0.0);
  writer->write_f64(1.0);
  writer->write_f64(scale_x);
  writer->write_f64(4.0);
  writer->write_f64(6.0);
  WriteColor(writer, 1.f, 0.f, 0.f, 1.f);
  WriteMarkerTail(writer, points);
}

TEST(CompressedImageDecoder, ReadsJpegBytes) {
  CdrWriter writer;
  WriteHeader(&writer, 2, 3, "camera");
  writer.write_string("jpeg");
  const uint8_t data[] = {0xff, 0xd8, 0xff, 0xd9};
  writer.write_sequence_length(sizeof(data));
  writer.write_bytes(data, sizeof(data));

  EmptyDecodeContext context;
  const DecodeResult decoded = CompressedImageDecoder().decode(
      Record("/cam/compressed", writer.bytes()), context);
  ASSERT_EQ(decoded.kind, DecodeResult::Kind::kOk);
  ASSERT_EQ(decoded.primitives.size(), 1u);
  const auto* image =
      std::get_if<EncodedImagePayload>(&decoded.primitives[0].payload);
  ASSERT_NE(image, nullptr);
  EXPECT_EQ(image->format, EncodedImagePayload::Format::kJpeg);
  EXPECT_EQ(image->bytes, std::vector<uint8_t>(data, data + sizeof(data)));
  EXPECT_EQ(decoded.primitives[0].capture_time_ns, 2000000003);
  EXPECT_EQ(decoded.primitives[0].frame_id, "camera");
}

TEST(CompressedImageDecoder, WaitsForAnH264Keyframe) {
  const uint8_t delta[] = {0x00, 0x00, 0x00, 0x01, 0x41, 0x00};
  const uint8_t idr[] = {0x00, 0x00, 0x00, 0x01, 0x65, 0x00};
  auto packet = [](const uint8_t* data, size_t size) {
    CdrWriter writer;
    WriteHeader(&writer, 0, 0, "camera");
    writer.write_string("H264");
    writer.write_sequence_length(static_cast<uint32_t>(size));
    writer.write_bytes(data, size);
    return writer.bytes();
  };

  CompressedImageDecoder decoder;
  EmptyDecodeContext context;
  const DecodeResult waiting = decoder.decode(
      Record("/cam/video", packet(delta, sizeof(delta))), context);
  EXPECT_EQ(waiting.kind, DecodeResult::Kind::kSkip);
  EXPECT_EQ(waiting.reason, "waiting for keyframe");

  const DecodeResult key = decoder.decode(
      Record("/cam/video", packet(idr, sizeof(idr))), context);
  ASSERT_EQ(key.kind, DecodeResult::Kind::kOk);
  const auto* frame = std::get_if<VideoFramePayload>(&key.primitives[0].payload);
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->codec, VideoFramePayload::Codec::kH264);
  EXPECT_TRUE(frame->keyframe);

  const DecodeResult later = decoder.decode(
      Record("/cam/video", packet(delta, sizeof(delta))), context);
  EXPECT_EQ(later.kind, DecodeResult::Kind::kOk);
}

TEST(CompressedImageDecoder, SkipsCompressedDepth) {
  CdrWriter writer;
  WriteHeader(&writer, 0, 0, "camera");
  writer.write_string("compressedDepth");
  writer.write_sequence_length(1);
  writer.write_u8(1);
  EmptyDecodeContext context;
  const DecodeResult decoded = CompressedImageDecoder().decode(
      Record("/cam/depth", writer.bytes()), context);
  EXPECT_EQ(decoded.kind, DecodeResult::Kind::kSkip);
  EXPECT_EQ(decoded.reason, "unsupported compressed image format");
}

TEST(CompressedVideoDecoder, ReadsAnH265Keyframe) {
  CdrWriter writer;
  WriteHeader(&writer, 4, 0, "camera");
  const uint8_t irap[] = {0x00, 0x00, 0x00, 0x01, 0x26, 0x01};
  writer.write_sequence_length(sizeof(irap));
  writer.write_bytes(irap, sizeof(irap));
  writer.write_string("h265");

  EmptyDecodeContext context;
  const DecodeResult decoded = CompressedVideoDecoder().decode(
      Record("/cam/foxglove", writer.bytes()), context);
  ASSERT_EQ(decoded.kind, DecodeResult::Kind::kOk);
  const auto* frame =
      std::get_if<VideoFramePayload>(&decoded.primitives[0].payload);
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->codec, VideoFramePayload::Codec::kH265);
  EXPECT_TRUE(frame->keyframe);
  EXPECT_EQ(frame->annex_b, std::vector<uint8_t>(irap, irap + sizeof(irap)));
  EXPECT_EQ(decoded.primitives[0].capture_time_ns, 4000000000LL);
}

TEST(MarkerDecoder, ConvertsACubeToHalfExtents) {
  CdrWriter writer;
  WriteMarker(&writer, "obstacles", 3, 1, 0, 2.0, {});
  EmptyDecodeContext context;
  const DecodeResult decoded =
      MarkerDecoder().decode(Record("/markers", writer.bytes()), context);
  ASSERT_EQ(decoded.kind, DecodeResult::Kind::kOk);
  ASSERT_EQ(decoded.primitives.size(), 1u);
  EXPECT_EQ(decoded.primitives[0].instance, "obstacles/3");
  const auto* shapes =
      std::get_if<Shapes3DPayload>(&decoded.primitives[0].payload);
  ASSERT_NE(shapes, nullptr);
  ASSERT_EQ(shapes->shapes.size(), 1u);
  EXPECT_EQ(shapes->shapes[0].kind, ShapeKind::kBox);
  EXPECT_FLOAT_EQ(shapes->shapes[0].center.x, 1.f);
  EXPECT_FLOAT_EQ(shapes->shapes[0].half_sizes.x, 1.f);
  EXPECT_FLOAT_EQ(shapes->shapes[0].half_sizes.y, 2.f);
  EXPECT_FLOAT_EQ(shapes->shapes[0].half_sizes.z, 3.f);
  EXPECT_EQ(shapes->shapes[0].color.r, 255);
}

TEST(MarkerDecoder, ArrowUsesThePoseWhenThereAreNoPoints) {
  CdrWriter writer;
  WriteMarker(&writer, "arrows", 1, 0, 0, 4.0, {});
  EmptyDecodeContext context;
  const DecodeResult decoded =
      MarkerDecoder().decode(Record("/markers", writer.bytes()), context);
  ASSERT_EQ(decoded.kind, DecodeResult::Kind::kOk);
  const auto* arrows =
      std::get_if<Arrows3DPayload>(&decoded.primitives[0].payload);
  ASSERT_NE(arrows, nullptr);
  ASSERT_EQ(arrows->arrows.size(), 1u);
  EXPECT_FLOAT_EQ(arrows->arrows[0].origin.x, 1.f);
  EXPECT_FLOAT_EQ(arrows->arrows[0].vector.x, 4.f);
  EXPECT_FLOAT_EQ(arrows->arrows[0].vector.y, 0.f);
}

TEST(MarkerDecoder, DeleteClearsOneInstanceAndDeleteAllClearsTheTopic) {
  CdrWriter deleted;
  WriteMarker(&deleted, "obstacles", 3, 1, 2, 1.0, {});
  CdrWriter all;
  WriteMarker(&all, "obstacles", 3, 1, 3, 1.0, {});
  EmptyDecodeContext context;
  const DecodeResult one =
      MarkerDecoder().decode(Record("/markers", deleted.bytes()), context);
  ASSERT_EQ(one.kind, DecodeResult::Kind::kOk);
  EXPECT_EQ(one.primitives[0].instance, "obstacles/3");
  const auto* clear = std::get_if<ClearPayload>(&one.primitives[0].payload);
  ASSERT_NE(clear, nullptr);
  EXPECT_FALSE(clear->recursive);

  const DecodeResult every =
      MarkerDecoder().decode(Record("/markers", all.bytes()), context);
  ASSERT_EQ(every.kind, DecodeResult::Kind::kOk);
  EXPECT_FALSE(every.primitives[0].instance.has_value());
  const auto* wipe = std::get_if<ClearPayload>(&every.primitives[0].payload);
  ASSERT_NE(wipe, nullptr);
  EXPECT_TRUE(wipe->recursive);
}

TEST(MarkerDecoder, SkipsTriangleListsAndRejectsAShortLineList) {
  CdrWriter triangles;
  WriteMarker(&triangles, "mesh", 1, 11, 0, 1.0, {});
  EmptyDecodeContext context;
  const DecodeResult skipped =
      MarkerDecoder().decode(Record("/markers", triangles.bytes()), context);
  EXPECT_EQ(skipped.kind, DecodeResult::Kind::kSkip);
  EXPECT_EQ(skipped.reason, "unsupported marker type");

  CdrWriter lines;
  WriteMarker(&lines, "lines", 1, 5, 0, 0.2,
              {std::array<double, 3>{0.0, 0.0, 0.0}});
  const DecodeResult bad =
      MarkerDecoder().decode(Record("/markers", lines.bytes()), context);
  EXPECT_EQ(bad.kind, DecodeResult::Kind::kError);
  EXPECT_EQ(bad.reason, "line list has an odd number of points");
}

TEST(MarkerArrayDecoder, KeepsSupportedMarkersBesideASkippedOne) {
  CdrWriter writer;
  writer.write_sequence_length(2);
  WriteMarker(&writer, "skip", 1, 10, 0, 1.0, {});
  WriteMarker(&writer, "keep", 2, 2, 0, 2.0, {});
  EmptyDecodeContext context;
  const DecodeResult decoded =
      MarkerArrayDecoder().decode(Record("/markers", writer.bytes()), context);
  ASSERT_EQ(decoded.kind, DecodeResult::Kind::kOk);
  ASSERT_EQ(decoded.primitives.size(), 1u);
  EXPECT_EQ(decoded.primitives[0].instance, "keep/2");
  const auto* shapes =
      std::get_if<Shapes3DPayload>(&decoded.primitives[0].payload);
  ASSERT_NE(shapes, nullptr);
  EXPECT_EQ(shapes->shapes[0].kind, ShapeKind::kEllipsoid);
  EXPECT_FLOAT_EQ(shapes->shapes[0].half_sizes.x, 1.f);
}

}  // namespace
}  // namespace rrd_convert
