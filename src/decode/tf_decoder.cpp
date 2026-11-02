#include "decode/tf_decoder.hpp"

#include "decode/ros_cdr.hpp"

#include <string>
#include <utility>
#include <vector>

namespace rrd_convert {

std::string TfMessageDecoder::type_name() const {
  return "tf2_msgs/msg/TFMessage";
}

DecodeResult TfMessageDecoder::decode(const InputRecord& record,
                                      DecodeContext& context) {
  (void)context;
  std::string serialization_error;
  if (!ros_cdr::require_cdr(record.serialization, &serialization_error)) {
    return DecodeResult::Error(std::move(serialization_error));
  }
  CdrReader reader(record.bytes.data(), record.bytes.size());
  uint32_t count = 0;
  if (!reader.read_sequence_length(&count)) {
    return DecodeResult::Error(reader.error());
  }
  const bool is_static = record.topic == "/tf_static";
  std::vector<VisualPrimitive> primitives;
  primitives.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    int64_t stamp_ns = 0;
    TransformPayload transform;
    double tx = 0;
    double ty = 0;
    double tz = 0;
    double qx = 0;
    double qy = 0;
    double qz = 0;
    double qw = 0;
    if (!ros_cdr::read_header(&reader, &stamp_ns, &transform.parent_frame) ||
        !reader.read_string(&transform.child_frame) || !reader.read_f64(&tx) ||
        !reader.read_f64(&ty) || !reader.read_f64(&tz) || !reader.read_f64(&qx) ||
        !reader.read_f64(&qy) || !reader.read_f64(&qz) || !reader.read_f64(&qw)) {
      return DecodeResult::Error(reader.error());
    }
    transform.translation = Vec3f{static_cast<float>(tx), static_cast<float>(ty),
                                  static_cast<float>(tz)};
    transform.rotation =
        Quatf{static_cast<float>(qx), static_cast<float>(qy),
              static_cast<float>(qz), static_cast<float>(qw)};
    if (transform.child_frame.empty()) {
      return DecodeResult::Error("transform is missing child_frame_id");
    }
    transform.is_static = is_static;
    VisualPrimitive primitive;
    primitive.topic = record.topic;
    primitive.frame_id = transform.child_frame;
    primitive.log_time_ns = record.log_time_ns;
    set_capture_time(&primitive, stamp_ns);
    primitive.payload = std::move(transform);
    primitives.push_back(std::move(primitive));
  }
  return DecodeResult::Ok(std::move(primitives));
}

}  // namespace rrd_convert
