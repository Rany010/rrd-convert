#ifndef RRD_CONVERT_SRC_CORE_VISUAL_PRIMITIVE_HPP_
#define RRD_CONVERT_SRC_CORE_VISUAL_PRIMITIVE_HPP_

#include <any>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace rrd_convert {

struct Vec3f {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
};

// Rotation in xyzw order.
struct Quatf {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  float w = 1.f;
};

struct Rgba8 {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
};

enum class ImageEncoding {
  kRgb8,
  kRgba8,
  kBgr8,
  kBgra8,
  kMono8,
  kMono16,
  k16Uc1,
  k32Fc1,
  kNv12,
  kYuv422Yuy2,
};

struct ImagePayload {
  int width = 0;
  int height = 0;
  ImageEncoding encoding = ImageEncoding::kRgb8;
  int stride = 0;
  std::vector<uint8_t> pixels;
};

struct EncodedImagePayload {
  enum class Format { kJpeg, kPng };
  Format format = Format::kJpeg;
  std::vector<uint8_t> bytes;
};

struct VideoFramePayload {
  enum class Codec { kH264, kH265 };
  Codec codec = Codec::kH264;
  std::vector<uint8_t> annex_b;
  bool keyframe = false;
};

struct Points3DPayload {
  std::vector<Vec3f> positions;
  std::vector<Rgba8> colors;
  std::vector<float> radii;
};

struct LineStrips3DPayload {
  std::vector<std::vector<Vec3f>> strips;
  std::optional<Rgba8> color;
  std::optional<float> width;
};

enum class ShapeKind { kBox, kEllipsoid, kCylinder };

struct Shape3D {
  ShapeKind kind = ShapeKind::kBox;
  Vec3f center;
  Vec3f half_sizes;
  Quatf rotation;
  Rgba8 color;
};

struct Shapes3DPayload {
  std::vector<Shape3D> shapes;
};

struct Arrow3D {
  Vec3f origin;
  Vec3f vector;
  Rgba8 color;
};

struct Arrows3DPayload {
  std::vector<Arrow3D> arrows;
};

struct TextLabel3D {
  Vec3f position;
  std::string text;
};

struct TextLabels3DPayload {
  std::vector<TextLabel3D> labels;
};

struct TransformPayload {
  std::string parent_frame;
  std::string child_frame;
  Vec3f translation;
  Quatf rotation;
  bool is_static = false;
};

struct NamedScalar {
  std::string name;
  double value = 0.0;
};

struct ScalarsPayload {
  std::vector<NamedScalar> values;
};

enum class TextLevel { kTrace, kDebug, kInfo, kWarn, kError };

struct TextPayload {
  enum class Kind { kLog, kDocument };
  Kind kind = Kind::kLog;
  std::optional<TextLevel> level;
  std::string text;
};

// Clears one instance, or the whole topic when recursive is set.
struct ClearPayload {
  bool recursive = false;
};

// Plugin-defined primitive. Sinks that do not recognize kind skip it.
struct ExtensionPayload {
  std::string kind;
  std::any payload;
};

using PrimitivePayload = std::variant<ImagePayload,
                                      EncodedImagePayload,
                                      VideoFramePayload,
                                      Points3DPayload,
                                      LineStrips3DPayload,
                                      Shapes3DPayload,
                                      Arrows3DPayload,
                                      TextLabels3DPayload,
                                      TransformPayload,
                                      ScalarsPayload,
                                      TextPayload,
                                      ClearPayload,
                                      ExtensionPayload>;

// Engine-neutral visual record. Entity paths are a sink concern.
struct VisualPrimitive {
  std::string topic;
  std::string frame_id;
  std::optional<std::string> instance;
  int64_t log_time_ns = 0;
  std::optional<int64_t> capture_time_ns;
  PrimitivePayload payload;
};

// A stamp of zero means the message has no header time.
void set_capture_time(VisualPrimitive* primitive, int64_t stamp_ns);

bool is_depth_encoding(ImageEncoding encoding);

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_CORE_VISUAL_PRIMITIVE_HPP_
