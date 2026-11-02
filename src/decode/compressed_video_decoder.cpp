#include "decode/compressed_video_decoder.hpp"

#include "decode/annexb.hpp"
#include "decode/ros_cdr.hpp"

#include <string>
#include <utility>
#include <vector>

namespace rrd_convert {

std::string CompressedVideoDecoder::type_name() const {
  return "foxglove_msgs/msg/CompressedVideo";
}

DecodeResult CompressedVideoDecoder::decode(const InputRecord& record,
                                            DecodeContext& context) {
  (void)context;
  std::string serialization_error;
  if (!ros_cdr::require_cdr(record.serialization, &serialization_error)) {
    return DecodeResult::Error(std::move(serialization_error));
  }
  CdrReader reader(record.bytes.data(), record.bytes.size());
  int64_t stamp_ns = 0;
  std::string frame_id;
  uint32_t length = 0;
  std::string format;
  if (!ros_cdr::read_header(&reader, &stamp_ns, &frame_id) ||
      !reader.read_sequence_length(&length)) {
    return DecodeResult::Error(reader.error());
  }
  if (length == 0) {
    return DecodeResult::Error("compressed video has no data");
  }
  if (record.bytes.size() - reader.offset() < length) {
    return DecodeResult::Error("truncated compressed video");
  }
  std::vector<uint8_t> bytes(
      record.bytes.begin() + static_cast<std::ptrdiff_t>(reader.offset()),
      record.bytes.begin() +
          static_cast<std::ptrdiff_t>(reader.offset() + length));
  if (!reader.skip(length) || !reader.read_string(&format)) {
    return DecodeResult::Error(reader.error());
  }
  const std::string lower = annexb::lower_copy(format);
  const bool h265 = annexb::contains(lower, "h265") ||
                    annexb::contains(lower, "h.265") ||
                    annexb::contains(lower, "hevc");
  const bool h264 = !h265 && (annexb::contains(lower, "h264") ||
                              annexb::contains(lower, "h.264") ||
                              annexb::contains(lower, "avc"));
  if (!h264 && !h265) {
    return DecodeResult::Skip("unsupported video format");
  }
  const bool keyframe = annexb::is_keyframe(bytes.data(), bytes.size(), h265);
  if (!keyframe && keyframe_topics_.count(record.topic) == 0) {
    return DecodeResult::Skip("waiting for keyframe");
  }
  if (keyframe) {
    keyframe_topics_.insert(record.topic);
  }
  VideoFramePayload frame;
  frame.codec = h265 ? VideoFramePayload::Codec::kH265
                     : VideoFramePayload::Codec::kH264;
  frame.annex_b = std::move(bytes);
  frame.keyframe = keyframe;
  VisualPrimitive primitive;
  primitive.topic = record.topic;
  primitive.frame_id = std::move(frame_id);
  primitive.log_time_ns = record.log_time_ns;
  set_capture_time(&primitive, stamp_ns);
  primitive.payload = std::move(frame);
  return DecodeResult::Ok({std::move(primitive)});
}

}  // namespace rrd_convert
