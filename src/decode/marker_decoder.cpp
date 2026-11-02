#include "decode/marker_decoder.hpp"

#include "decode/ros_cdr.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rrd_convert {
namespace {

constexpr int32_t kArrow = 0;
constexpr int32_t kCube = 1;
constexpr int32_t kSphere = 2;
constexpr int32_t kCylinder = 3;
constexpr int32_t kLineStrip = 4;
constexpr int32_t kLineList = 5;
constexpr int32_t kCubeList = 6;
constexpr int32_t kSphereList = 7;
constexpr int32_t kPoints = 8;
constexpr int32_t kText = 9;
constexpr int32_t kMesh = 10;
constexpr int32_t kTriangles = 11;

constexpr int32_t kAdd = 0;
constexpr int32_t kDelete = 2;
constexpr int32_t kDeleteAll = 3;

struct MarkerMessage {
  int64_t stamp_ns = 0;
  std::string frame_id;
  std::string ns;
  int32_t id = 0;
  int32_t type = 0;
  int32_t action = 0;
  Vec3f position;
  Quatf rotation;
  Vec3f scale;
  Rgba8 color;
  std::vector<Vec3f> points;
  std::vector<Rgba8> colors;
  std::string text;
};

uint8_t color_channel(float value) {
  if (value <= 0.f) {
    return 0;
  }
  if (value >= 1.f) {
    return 255;
  }
  return static_cast<uint8_t>(value * 255.f + 0.5f);
}

bool read_color(CdrReader* reader, Rgba8* color) {
  float red = 0.f;
  float green = 0.f;
  float blue = 0.f;
  float alpha = 0.f;
  if (!reader->read_f32(&red) || !reader->read_f32(&green) ||
      !reader->read_f32(&blue) || !reader->read_f32(&alpha)) {
    return false;
  }
  color->r = color_channel(red);
  color->g = color_channel(green);
  color->b = color_channel(blue);
  color->a = color_channel(alpha);
  return true;
}

bool read_vec3(CdrReader* reader, Vec3f* out) {
  double x = 0;
  double y = 0;
  double z = 0;
  if (!reader->read_f64(&x) || !reader->read_f64(&y) || !reader->read_f64(&z)) {
    return false;
  }
  out->x = static_cast<float>(x);
  out->y = static_cast<float>(y);
  out->z = static_cast<float>(z);
  return true;
}

Quatf rotation_or_identity(Quatf rotation) {
  if (rotation.x == 0.f && rotation.y == 0.f && rotation.z == 0.f &&
      rotation.w == 0.f) {
    rotation.w = 1.f;
  }
  return rotation;
}

Vec3f rotate(Quatf rotation, Vec3f value) {
  rotation = rotation_or_identity(rotation);
  const float tx = 2.f * (rotation.y * value.z - rotation.z * value.y);
  const float ty = 2.f * (rotation.z * value.x - rotation.x * value.z);
  const float tz = 2.f * (rotation.x * value.y - rotation.y * value.x);
  return Vec3f{
      value.x + rotation.w * tx + (rotation.y * tz - rotation.z * ty),
      value.y + rotation.w * ty + (rotation.z * tx - rotation.x * tz),
      value.z + rotation.w * tz + (rotation.x * ty - rotation.y * tx),
  };
}

Vec3f transform_point(const MarkerMessage& marker, Vec3f point) {
  const Vec3f rotated = rotate(marker.rotation, point);
  return Vec3f{rotated.x + marker.position.x, rotated.y + marker.position.y,
               rotated.z + marker.position.z};
}

std::string instance_key(const MarkerMessage& marker) {
  return marker.ns + "/" + std::to_string(marker.id);
}

Rgba8 point_color(const MarkerMessage& marker, size_t index) {
  if (marker.colors.size() == marker.points.size()) {
    return marker.colors[index];
  }
  return marker.color;
}

bool read_marker(CdrReader* reader, MarkerMessage* marker) {
  double qx = 0;
  double qy = 0;
  double qz = 0;
  double qw = 0;
  int32_t lifetime_sec = 0;
  uint32_t lifetime_nsec = 0;
  uint8_t frame_locked = 0;
  uint32_t point_count = 0;
  if (!ros_cdr::read_header(reader, &marker->stamp_ns, &marker->frame_id) ||
      !reader->read_string(&marker->ns) || !reader->read_i32(&marker->id) ||
      !reader->read_i32(&marker->type) || !reader->read_i32(&marker->action) ||
      !read_vec3(reader, &marker->position) || !reader->read_f64(&qx) ||
      !reader->read_f64(&qy) || !reader->read_f64(&qz) || !reader->read_f64(&qw) ||
      !read_vec3(reader, &marker->scale) || !read_color(reader, &marker->color) ||
      !reader->read_i32(&lifetime_sec) || !reader->read_u32(&lifetime_nsec) ||
      !reader->read_u8(&frame_locked) ||
      !reader->read_sequence_length(&point_count)) {
    return false;
  }
  (void)lifetime_sec;
  (void)lifetime_nsec;
  (void)frame_locked;
  marker->rotation = Quatf{static_cast<float>(qx), static_cast<float>(qy),
                           static_cast<float>(qz), static_cast<float>(qw)};
  marker->points.reserve(point_count);
  for (uint32_t i = 0; i < point_count; ++i) {
    Vec3f point;
    if (!read_vec3(reader, &point)) {
      return false;
    }
    marker->points.push_back(point);
  }
  uint32_t color_count = 0;
  if (!reader->read_sequence_length(&color_count)) {
    return false;
  }
  marker->colors.reserve(color_count);
  for (uint32_t i = 0; i < color_count; ++i) {
    Rgba8 color;
    if (!read_color(reader, &color)) {
      return false;
    }
    marker->colors.push_back(color);
  }
  std::string mesh;
  uint8_t mesh_embedded = 0;
  if (!reader->read_string(&marker->text) || !reader->read_string(&mesh) ||
      !reader->read_u8(&mesh_embedded)) {
    return false;
  }
  (void)mesh;
  (void)mesh_embedded;
  return true;
}

VisualPrimitive base_primitive(const InputRecord& record,
                               const MarkerMessage& marker) {
  VisualPrimitive primitive;
  primitive.topic = record.topic;
  primitive.frame_id = marker.frame_id;
  primitive.log_time_ns = record.log_time_ns;
  set_capture_time(&primitive, marker.stamp_ns);
  return primitive;
}

DecodeResult convert_marker(const InputRecord& record,
                            const MarkerMessage& marker, bool* skipped) {
  if (marker.action == kDeleteAll) {
    VisualPrimitive primitive = base_primitive(record, marker);
    primitive.payload = ClearPayload{true};
    return DecodeResult::Ok({std::move(primitive)});
  }
  if (marker.action == kDelete) {
    VisualPrimitive primitive = base_primitive(record, marker);
    primitive.instance = instance_key(marker);
    primitive.payload = ClearPayload{false};
    return DecodeResult::Ok({std::move(primitive)});
  }
  if (marker.action != kAdd) {
    return DecodeResult::Error("unsupported marker action");
  }
  if (marker.type == kMesh || marker.type == kTriangles) {
    *skipped = true;
    return DecodeResult::Ok({});
  }
  if (marker.type < kArrow || marker.type > kText) {
    *skipped = true;
    return DecodeResult::Ok({});
  }

  VisualPrimitive primitive = base_primitive(record, marker);
  primitive.instance = instance_key(marker);
  const Quatf rotation = rotation_or_identity(marker.rotation);
  const Vec3f half{marker.scale.x * 0.5f, marker.scale.y * 0.5f,
                   marker.scale.z * 0.5f};

  if (marker.type == kCube || marker.type == kSphere ||
      marker.type == kCylinder) {
    Shape3D shape;
    shape.kind = marker.type == kCube
                     ? ShapeKind::kBox
                     : (marker.type == kSphere ? ShapeKind::kEllipsoid
                                               : ShapeKind::kCylinder);
    shape.center = marker.position;
    shape.half_sizes = half;
    shape.rotation = rotation;
    shape.color = marker.color;
    primitive.payload = Shapes3DPayload{{shape}};
    return DecodeResult::Ok({std::move(primitive)});
  }

  if (marker.type == kCubeList || marker.type == kSphereList) {
    const float extent = marker.scale.x * 0.5f;
    const Vec3f list_half{extent, extent, extent};
    Shapes3DPayload shapes;
    shapes.shapes.reserve(marker.points.size());
    for (size_t i = 0; i < marker.points.size(); ++i) {
      Shape3D shape;
      shape.kind = marker.type == kCubeList ? ShapeKind::kBox
                                            : ShapeKind::kEllipsoid;
      shape.center = transform_point(marker, marker.points[i]);
      shape.half_sizes = list_half;
      shape.rotation = rotation;
      shape.color = point_color(marker, i);
      shapes.shapes.push_back(shape);
    }
    primitive.payload = std::move(shapes);
    return DecodeResult::Ok({std::move(primitive)});
  }

  if (marker.type == kPoints) {
    Points3DPayload cloud;
    cloud.positions.reserve(marker.points.size());
    for (const Vec3f& point : marker.points) {
      cloud.positions.push_back(transform_point(marker, point));
    }
    if (marker.colors.size() == marker.points.size()) {
      cloud.colors = marker.colors;
    } else if (!marker.points.empty()) {
      cloud.colors.assign(marker.points.size(), marker.color);
    }
    if (marker.scale.x > 0.f) {
      cloud.radii.assign(marker.points.size(), marker.scale.x * 0.5f);
    }
    primitive.payload = std::move(cloud);
    return DecodeResult::Ok({std::move(primitive)});
  }

  if (marker.type == kArrow) {
    Arrows3DPayload arrows;
    Arrow3D arrow;
    arrow.color = marker.color;
    if (marker.points.empty()) {
      arrow.origin = marker.position;
      arrow.vector = rotate(rotation, Vec3f{marker.scale.x, 0.f, 0.f});
    } else if (marker.points.size() >= 2) {
      arrow.origin = transform_point(marker, marker.points[0]);
      const Vec3f end = transform_point(marker, marker.points[1]);
      arrow.vector = Vec3f{end.x - arrow.origin.x, end.y - arrow.origin.y,
                           end.z - arrow.origin.z};
    } else {
      return DecodeResult::Error("arrow point count is not 0 or 2");
    }
    arrows.arrows.push_back(arrow);
    primitive.payload = std::move(arrows);
    return DecodeResult::Ok({std::move(primitive)});
  }

  if (marker.type == kText) {
    TextLabels3DPayload labels;
    labels.labels.push_back(
        TextLabel3D{marker.position, marker.text});
    primitive.payload = std::move(labels);
    return DecodeResult::Ok({std::move(primitive)});
  }

  if (marker.type == kLineList && (marker.points.size() % 2) != 0) {
    return DecodeResult::Error("line list has an odd number of points");
  }
  LineStrips3DPayload strips;
  if (marker.type == kLineStrip) {
    std::vector<Vec3f> strip;
    strip.reserve(marker.points.size());
    for (const Vec3f& point : marker.points) {
      strip.push_back(transform_point(marker, point));
    }
    if (!strip.empty()) {
      strips.strips.push_back(std::move(strip));
    }
  } else {
    for (size_t i = 0; i + 1 < marker.points.size(); i += 2) {
      strips.strips.push_back(
          {transform_point(marker, marker.points[i]),
           transform_point(marker, marker.points[i + 1])});
    }
  }
  strips.color = marker.colors.empty() ? marker.color : marker.colors.front();
  if (marker.scale.x > 0.f) {
    strips.width = marker.scale.x;
  }
  primitive.payload = std::move(strips);
  return DecodeResult::Ok({std::move(primitive)});
}

DecodeResult convert_markers(const InputRecord& record,
                             const std::vector<MarkerMessage>& markers) {
  std::vector<VisualPrimitive> primitives;
  bool skipped = false;
  for (const MarkerMessage& marker : markers) {
    bool marker_skipped = false;
    DecodeResult converted = convert_marker(record, marker, &marker_skipped);
    if (converted.kind == DecodeResult::Kind::kError) {
      return converted;
    }
    skipped = skipped || marker_skipped;
    primitives.insert(primitives.end(),
                      std::make_move_iterator(converted.primitives.begin()),
                      std::make_move_iterator(converted.primitives.end()));
  }
  if (primitives.empty() && skipped) {
    return DecodeResult::Skip("unsupported marker type");
  }
  return DecodeResult::Ok(std::move(primitives));
}

}  // namespace

std::string MarkerDecoder::type_name() const {
  return "visualization_msgs/msg/Marker";
}

DecodeResult MarkerDecoder::decode(const InputRecord& record,
                                   DecodeContext& context) {
  (void)context;
  std::string serialization_error;
  if (!ros_cdr::require_cdr(record.serialization, &serialization_error)) {
    return DecodeResult::Error(std::move(serialization_error));
  }
  CdrReader reader(record.bytes.data(), record.bytes.size());
  MarkerMessage marker;
  if (!read_marker(&reader, &marker)) {
    return DecodeResult::Error(reader.error());
  }
  return convert_markers(record, {marker});
}

std::string MarkerArrayDecoder::type_name() const {
  return "visualization_msgs/msg/MarkerArray";
}

DecodeResult MarkerArrayDecoder::decode(const InputRecord& record,
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
  std::vector<MarkerMessage> markers;
  markers.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    MarkerMessage marker;
    if (!read_marker(&reader, &marker)) {
      return DecodeResult::Error(reader.error());
    }
    markers.push_back(std::move(marker));
  }
  return convert_markers(record, markers);
}

}  // namespace rrd_convert
