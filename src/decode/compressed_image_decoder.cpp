#include "decode/compressed_image_decoder.hpp"

#include "decode/annexb.hpp"
#include "decode/ros_cdr.hpp"

#include <string>
#include <utility>
#include <vector>

namespace rrd_convert {
namespace {

enum class CompressedKind { kJpeg, kPng, kH264, kH265, kOther };

CompressedKind classify_format(const std::string& format) {
  const std::string lower = annexb::lower_copy(format);
  if (annexb::contains(lower, "h265") || annexb::contains(lower, "h.265") ||
      annexb::contains(lower, "hevc")) {
    return CompressedKind::kH265;
  }
  if (annexb::contains(lower, "h264") || annexb::contains(lower, "h.264") ||
      annexb::contains(lower, "avc")) {
    return CompressedKind::kH264;
  }
  if (annexb::contains(lower, "jpeg") || annexb::contains(lower, "jpg")) {
    return CompressedKind::kJpeg;
  }
  if (annexb::contains(lower, "png")) {
    return CompressedKind::kPng;
  }
  return CompressedKind::kOther;
}

DecodeResult video_frame(const InputRecord& record, int64_t stamp_ns,
                         std::string frame_id, std::vector<uint8_t> bytes,
                         bool h265,
                         std::unordered_set<std::string>* keyframe_topics) {
  const bool keyframe = annexb::is_keyframe(bytes.data(), bytes.size(), h265);
  if (!keyframe && keyframe_topics->count(record.topic) == 0) {
    return DecodeResult::Skip("waiting for keyframe");
  }
  if (keyframe) {
    keyframe_topics->insert(record.topic);
  }
  VideoFramePayload payload;
  payload.codec =
      h265 ? VideoFramePayload::Codec::kH265 : VideoFramePayload::Codec::kH264;
  payload.annex_b = std::move(bytes);
  payload.keyframe = keyframe;
  VisualPrimitive primitive;
  primitive.topic = record.topic;
  primitive.frame_id = std::move(frame_id);
  primitive.log_time_ns = record.log_time_ns;
  set_capture_time(&primitive, stamp_ns);
  primitive.payload = std::move(payload);
  return DecodeResult::Ok({std::move(primitive)});
}

}  // namespace

std::string CompressedImageDecoder::type_name() const {
  return "sensor_msgs/msg/CompressedImage";
}

DecodeResult CompressedImageDecoder::decode(const InputRecord& record,
                                            DecodeContext& context) {
  (void)context;
  std::string serialization_error;
  if (!ros_cdr::require_cdr(record.serialization, &serialization_error)) {
    return DecodeResult::Error(std::move(serialization_error));
  }
  CdrReader reader(record.bytes.data(), record.bytes.size());
  int64_t stamp_ns = 0;
  std::string frame_id;
  std::string format;
  uint32_t length = 0;
  if (!ros_cdr::read_header(&reader, &stamp_ns, &frame_id) ||
      !reader.read_string(&format) || !reader.read_sequence_length(&length)) {
    return DecodeResult::Error(reader.error());
  }
  if (record.bytes.size() - reader.offset() < length) {
    return DecodeResult::Error("truncated compressed image");
  }
  if (length == 0) {
    return DecodeResult::Error("compressed image has no data");
  }
  std::vector<uint8_t> bytes(
      record.bytes.begin() + static_cast<std::ptrdiff_t>(reader.offset()),
      record.bytes.begin() +
          static_cast<std::ptrdiff_t>(reader.offset() + length));
  const CompressedKind kind = classify_format(format);
  if (kind == CompressedKind::kOther) {
    return DecodeResult::Skip("unsupported compressed image format");
  }
  if (kind == CompressedKind::kH264 || kind == CompressedKind::kH265) {
    return video_frame(record, stamp_ns, std::move(frame_id), std::move(bytes),
                       kind == CompressedKind::kH265, &keyframe_topics_);
  }
  EncodedImagePayload payload;
  payload.format = kind == CompressedKind::kPng
                       ? EncodedImagePayload::Format::kPng
                       : EncodedImagePayload::Format::kJpeg;
  payload.bytes = std::move(bytes);
  VisualPrimitive primitive;
  primitive.topic = record.topic;
  primitive.frame_id = std::move(frame_id);
  primitive.log_time_ns = record.log_time_ns;
  set_capture_time(&primitive, stamp_ns);
  primitive.payload = std::move(payload);
  return DecodeResult::Ok({std::move(primitive)});
}

}  // namespace rrd_convert
