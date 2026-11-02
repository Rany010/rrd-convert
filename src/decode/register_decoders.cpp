#include "decode/register_decoders.hpp"

#include "decode/compressed_image_decoder.hpp"
#include "decode/compressed_video_decoder.hpp"
#include "decode/image_decoder.hpp"
#include "decode/marker_decoder.hpp"
#include "decode/point_cloud_decoder.hpp"
#include "decode/tf_decoder.hpp"

#include <memory>

namespace rrd_convert {

void register_builtin_decoders(Registry* registry) {
  registry->add_decoder("sensor_msgs/msg/Image",
                        [] { return std::make_unique<ImageDecoder>(); });
  registry->add_decoder("sensor_msgs/msg/PointCloud2", [] {
    return std::make_unique<PointCloud2Decoder>();
  });
  registry->add_decoder("tf2_msgs/msg/TFMessage",
                        [] { return std::make_unique<TfMessageDecoder>(); });
  registry->add_decoder("sensor_msgs/msg/CompressedImage", [] {
    return std::make_unique<CompressedImageDecoder>();
  });
  registry->add_decoder("foxglove_msgs/msg/CompressedVideo", [] {
    return std::make_unique<CompressedVideoDecoder>();
  });
  registry->add_decoder("visualization_msgs/msg/Marker",
                        [] { return std::make_unique<MarkerDecoder>(); });
  registry->add_decoder("visualization_msgs/msg/MarkerArray", [] {
    return std::make_unique<MarkerArrayDecoder>();
  });
}

}  // namespace rrd_convert
