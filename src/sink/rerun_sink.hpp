#ifndef RRD_CONVERT_SRC_SINK_RERUN_SINK_HPP_
#define RRD_CONVERT_SRC_SINK_RERUN_SINK_HPP_

#include "core/sink.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace rerun {
class RecordingStream;
}

namespace rrd_convert {

class RerunSink : public Sink {
 public:
  RerunSink();
  ~RerunSink() override;

  std::string name() const override;
  Status open(const std::string& path, const SinkOptions& options) override;
  Status write(const VisualPrimitive& primitive) override;
  Status close() override;

 private:
  Status log_transform(const VisualPrimitive& primitive,
                       const TransformPayload& transform);
  Status log_image(const VisualPrimitive& primitive, const ImagePayload& image);
  Status log_points(const VisualPrimitive& primitive,
                    const Points3DPayload& points);
  Status log_encoded_image(const VisualPrimitive& primitive,
                           const EncodedImagePayload& image);
  Status log_video(const VisualPrimitive& primitive,
                   const VideoFramePayload& frame);
  Status log_shapes(const VisualPrimitive& primitive,
                    const Shapes3DPayload& shapes);
  Status log_arrows(const VisualPrimitive& primitive,
                    const Arrows3DPayload& arrows);
  Status log_lines(const VisualPrimitive& primitive,
                   const LineStrips3DPayload& lines);
  Status log_labels(const VisualPrimitive& primitive,
                    const TextLabels3DPayload& labels);
  Status log_clear(const VisualPrimitive& primitive, const ClearPayload& clear);
  void set_timelines(const VisualPrimitive& primitive);
  std::string entity_path(const VisualPrimitive& primitive) const;

  std::unique_ptr<rerun::RecordingStream> stream_;
  std::optional<std::string> requested_root_;
  std::unordered_map<std::string, int> parent_counts_;
  std::unordered_set<std::string> child_frames_;
  std::unordered_map<std::string, int> video_codecs_;
  bool open_ = false;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_SINK_RERUN_SINK_HPP_
