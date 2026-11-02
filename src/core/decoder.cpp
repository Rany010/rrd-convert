#include "core/decoder.hpp"

#include "core/visual_primitive.hpp"

#include <utility>

namespace rrd_convert {

DecodeResult DecodeResult::Ok(std::vector<VisualPrimitive> primitives) {
  DecodeResult result;
  result.kind = Kind::kOk;
  result.primitives = std::move(primitives);
  return result;
}

DecodeResult DecodeResult::Skip(std::string reason) {
  DecodeResult result;
  result.kind = Kind::kSkip;
  result.reason = std::move(reason);
  return result;
}

DecodeResult DecodeResult::Error(std::string reason) {
  DecodeResult result;
  result.kind = Kind::kError;
  result.reason = std::move(reason);
  return result;
}

void set_capture_time(VisualPrimitive* primitive, int64_t stamp_ns) {
  if (stamp_ns == 0) {
    primitive->capture_time_ns.reset();
    return;
  }
  primitive->capture_time_ns = stamp_ns;
}

bool is_depth_encoding(ImageEncoding encoding) {
  return encoding == ImageEncoding::k16Uc1 ||
         encoding == ImageEncoding::k32Fc1;
}

}  // namespace rrd_convert
