#include "decode/image_decoder.hpp"

#include "decode/ros_cdr.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rrd_convert {
namespace {

struct EncodingInfo {
  ImageEncoding encoding;
  int bytes_per_pixel = 0;
  bool packed_yuv = false;
};

std::optional<EncodingInfo> lookup_encoding(const std::string& name) {
  if (name == "rgb8") {
    return EncodingInfo{ImageEncoding::kRgb8, 3, false};
  }
  if (name == "rgba8") {
    return EncodingInfo{ImageEncoding::kRgba8, 4, false};
  }
  if (name == "bgr8") {
    return EncodingInfo{ImageEncoding::kBgr8, 3, false};
  }
  if (name == "bgra8") {
    return EncodingInfo{ImageEncoding::kBgra8, 4, false};
  }
  if (name == "mono8") {
    return EncodingInfo{ImageEncoding::kMono8, 1, false};
  }
  if (name == "mono16") {
    return EncodingInfo{ImageEncoding::kMono16, 2, false};
  }
  if (name == "16UC1") {
    return EncodingInfo{ImageEncoding::k16Uc1, 2, false};
  }
  if (name == "32FC1") {
    return EncodingInfo{ImageEncoding::k32Fc1, 4, false};
  }
  if (name == "nv12") {
    return EncodingInfo{ImageEncoding::kNv12, 0, true};
  }
  if (name == "yuv422_yuy2") {
    return EncodingInfo{ImageEncoding::kYuv422Yuy2, 2, true};
  }
  return std::nullopt;
}

bool multi_byte(ImageEncoding encoding) {
  return encoding == ImageEncoding::kMono16 ||
         encoding == ImageEncoding::k16Uc1 ||
         encoding == ImageEncoding::k32Fc1;
}

}  // namespace

std::string ImageDecoder::type_name() const {
  return "sensor_msgs/msg/Image";
}

DecodeResult ImageDecoder::decode(const InputRecord& record,
                                  DecodeContext& context) {
  (void)context;
  std::string serialization_error;
  if (!ros_cdr::require_cdr(record.serialization, &serialization_error)) {
    return DecodeResult::Error(std::move(serialization_error));
  }
  CdrReader reader(record.bytes.data(), record.bytes.size());
  int64_t stamp_ns = 0;
  std::string frame_id;
  uint32_t height = 0;
  uint32_t width = 0;
  std::string encoding;
  uint8_t is_bigendian = 0;
  uint32_t step = 0;
  uint32_t data_length = 0;
  if (!ros_cdr::read_header(&reader, &stamp_ns, &frame_id) ||
      !reader.read_u32(&height) || !reader.read_u32(&width) ||
      !reader.read_string(&encoding) || !reader.read_u8(&is_bigendian) ||
      !reader.read_u32(&step) || !reader.read_sequence_length(&data_length)) {
    return DecodeResult::Error(reader.error());
  }
  if (record.bytes.size() - reader.offset() < data_length) {
    return DecodeResult::Error("truncated image");
  }

  const std::optional<EncodingInfo> info = lookup_encoding(encoding);
  if (!info.has_value()) {
    return DecodeResult::Skip("unsupported image encoding");
  }
  if (width == 0 || height == 0) {
    return DecodeResult::Error("image width or height is zero");
  }
  if (is_bigendian != 0 && multi_byte(info->encoding)) {
    return DecodeResult::Error("big-endian images are not supported");
  }

  const uint64_t row_bytes = static_cast<uint64_t>(step) * height;
  uint64_t required = row_bytes;
  if (info->encoding == ImageEncoding::kNv12) {
    if ((width % 2) != 0 || (height % 2) != 0) {
      return DecodeResult::Error("nv12 image width and height must be even");
    }
    if (step < width) {
      return DecodeResult::Error("image step is smaller than the row");
    }
    required = row_bytes + row_bytes / 2;
  } else if (step < static_cast<uint32_t>(width) *
                        static_cast<uint32_t>(info->bytes_per_pixel)) {
    return DecodeResult::Error("image step is smaller than the row");
  }
  if (static_cast<uint64_t>(data_length) < required) {
    return DecodeResult::Error("truncated image");
  }

  ImagePayload payload;
  payload.width = static_cast<int>(width);
  payload.height = static_cast<int>(height);
  payload.encoding = info->encoding;
  payload.stride = static_cast<int>(step);
  const size_t begin = reader.offset();
  payload.pixels.assign(record.bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                        record.bytes.begin() +
                            static_cast<std::ptrdiff_t>(begin + data_length));

  VisualPrimitive primitive;
  primitive.topic = record.topic;
  primitive.frame_id = std::move(frame_id);
  primitive.log_time_ns = record.log_time_ns;
  set_capture_time(&primitive, stamp_ns);
  primitive.payload = std::move(payload);
  return DecodeResult::Ok({std::move(primitive)});
}

}  // namespace rrd_convert
