#include "sink/rerun_sink.hpp"

#include "sink/root_frame.hpp"

#include <rerun.hpp>
#include <rerun/blueprint/archetypes/container_blueprint.hpp>
#include <rerun/blueprint/archetypes/view_blueprint.hpp>
#include <rerun/blueprint/archetypes/view_contents.hpp>
#include <rerun/blueprint/archetypes/viewport_blueprint.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

namespace rrd_convert {
namespace {

Status from_rerun(const rerun::Error& error) {
  if (error.is_ok()) {
    return Status::Ok();
  }
  if (error.description.empty()) {
    return Status::Error("", "rerun log failed");
  }
  return Status::Error("", error.description);
}

rerun::Color to_color(Rgba8 color) {
  return rerun::Color(color.r, color.g, color.b, color.a);
}

rerun::Quaternion to_quaternion(Quatf rotation) {
  return rerun::Quaternion::from_xyzw(rotation.x, rotation.y, rotation.z,
                                      rotation.w);
}

template <typename Archetype>
Status log_temporal(rerun::RecordingStream* stream, const std::string& path,
                    const std::string& frame_id, Archetype archetype) {
  if (frame_id.empty()) {
    return from_rerun(stream->try_log(path, std::move(archetype)));
  }
  return from_rerun(stream->try_log(path, std::move(archetype),
                                    rerun::CoordinateFrame(frame_id)));
}

std::vector<uint8_t> copy_rows(const uint8_t* pixels, size_t pixel_count,
                               int height, int stride, int row_bytes) {
  std::vector<uint8_t> packed(static_cast<size_t>(row_bytes) * height);
  const int copy = std::min(stride, row_bytes);
  for (int row = 0; row < height; ++row) {
    const size_t src = static_cast<size_t>(row) * static_cast<size_t>(stride);
    if (src >= pixel_count) {
      break;
    }
    const size_t available = pixel_count - src;
    const size_t count = std::min(static_cast<size_t>(copy), available);
    std::memcpy(packed.data() + static_cast<size_t>(row) * row_bytes,
                pixels + src, count);
  }
  return packed;
}

std::vector<uint8_t> pack_color(const ImagePayload& image, int bytes_per_pixel) {
  const int row_bytes = image.width * bytes_per_pixel;
  if (image.stride == row_bytes) {
    const size_t tight =
        static_cast<size_t>(row_bytes) * static_cast<size_t>(image.height);
    if (image.pixels.size() == tight) {
      return image.pixels;
    }
    return std::vector<uint8_t>(image.pixels.begin(),
                                image.pixels.begin() +
                                    static_cast<std::ptrdiff_t>(
                                        std::min(tight, image.pixels.size())));
  }
  return copy_rows(image.pixels.data(), image.pixels.size(), image.height,
                   image.stride, row_bytes);
}

std::vector<uint8_t> pack_nv12(const ImagePayload& image) {
  const int y_row = image.width;
  const int y_bytes = y_row * image.height;
  const int uv_bytes = y_bytes / 2;
  if (image.stride == image.width) {
    const size_t tight = static_cast<size_t>(y_bytes + uv_bytes);
    if (image.pixels.size() >= tight) {
      return std::vector<uint8_t>(image.pixels.begin(),
                                  image.pixels.begin() +
                                      static_cast<std::ptrdiff_t>(tight));
    }
  }
  std::vector<uint8_t> packed(static_cast<size_t>(y_bytes + uv_bytes));
  const auto y = copy_rows(image.pixels.data(), image.pixels.size(),
                           image.height, image.stride, y_row);
  std::memcpy(packed.data(), y.data(), y.size());
  const size_t uv_offset = static_cast<size_t>(image.stride) * image.height;
  if (uv_offset < image.pixels.size()) {
    const auto uv =
        copy_rows(image.pixels.data() + uv_offset,
                  image.pixels.size() - uv_offset, image.height / 2,
                  image.stride, y_row);
    std::memcpy(packed.data() + y_bytes, uv.data(), uv.size());
  }
  return packed;
}

constexpr int kMaxCameraViews = 8;

bool read_uuid(std::array<uint8_t, 16>* out) {
  std::ifstream random("/dev/urandom", std::ios::binary);
  if (!random.read(reinterpret_cast<char*>(out->data()),
                   static_cast<std::streamsize>(out->size()))) {
    return false;
  }
  (*out)[6] = static_cast<uint8_t>(((*out)[6] & 0x0f) | 0x40);
  (*out)[8] = static_cast<uint8_t>(((*out)[8] & 0x3f) | 0x80);
  return true;
}

std::string format_uuid(const std::array<uint8_t, 16>& bytes) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string text;
  text.reserve(36);
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      text.push_back('-');
    }
    text.push_back(kHex[bytes[i] >> 4]);
    text.push_back(kHex[bytes[i] & 0x0f]);
  }
  return text;
}

Status log_view(rerun::RecordingStream* stream, const std::string& id,
                const char* view_class, const char* name, const std::string& origin,
                const std::string& query) {
  const std::string path = "view/" + id;
  std::vector<rerun::blueprint::components::QueryExpression> queries;
  queries.emplace_back(query);
  rerun::Error error = stream->try_log(
      path + "/ViewContents",
      rerun::blueprint::archetypes::ViewContents(std::move(queries)));
  if (error.is_err()) {
    return from_rerun(error);
  }
  error = stream->try_log(
      path, rerun::blueprint::archetypes::ViewBlueprint(
                rerun::blueprint::components::ViewClass(std::string(view_class)))
                .with_display_name(rerun::components::Name(name))
                .with_space_origin(rerun::blueprint::components::ViewOrigin(origin)));
  return from_rerun(error);
}

Status log_container(rerun::RecordingStream* stream, const std::string& id,
                     rerun::blueprint::components::ContainerKind kind,
                     const std::vector<std::string>& child_paths,
                     const std::vector<float>& shares) {
  std::vector<rerun::blueprint::components::IncludedContent> contents;
  contents.reserve(child_paths.size());
  for (const std::string& child : child_paths) {
    contents.emplace_back(child);
  }
  auto container =
      rerun::blueprint::archetypes::ContainerBlueprint().with_container_kind(kind).with_contents(
          std::move(contents));
  if (kind == rerun::blueprint::components::ContainerKind::Horizontal &&
      shares.size() == child_paths.size()) {
    std::vector<rerun::blueprint::components::ColumnShare> columns;
    columns.reserve(shares.size());
    for (float share : shares) {
      columns.emplace_back(share);
    }
    container = std::move(container).with_col_shares(std::move(columns));
  }
  if (kind == rerun::blueprint::components::ContainerKind::Vertical &&
      shares.size() == child_paths.size()) {
    std::vector<rerun::blueprint::components::RowShare> rows;
    rows.reserve(shares.size());
    for (float share : shares) {
      rows.emplace_back(share);
    }
    container = std::move(container).with_row_shares(std::move(rows));
  }
  return from_rerun(stream->try_log("container/" + id, std::move(container)));
}

Status write_blueprint_file(const std::string& path,
                            const std::vector<std::string>& image_paths) {
  std::array<uint8_t, 16> root_id{};
  std::array<uint8_t, 16> scene_id{};
  std::array<uint8_t, 16> cameras_id{};
  if (!read_uuid(&root_id) || !read_uuid(&scene_id) || !read_uuid(&cameras_id)) {
    return Status::Error(path, "could not read /dev/urandom");
  }
  const std::string root = format_uuid(root_id);
  const std::string scene = format_uuid(scene_id);
  const std::string cameras = format_uuid(cameras_id);

  rerun::RecordingStream stream("rrd-convert", "blueprint",
                                rerun::StoreKind::Blueprint);
  const rerun::Error saved = stream.save(path);
  if (saved.is_err()) {
    return from_rerun(saved);
  }
  stream.set_time_sequence("blueprint", 0);

  Status status = log_view(&stream, scene, "3D", "Scene", "/", "+ /**");
  std::vector<std::string> root_children = {"view/" + scene};
  std::vector<float> root_shares = {1.f};
  const size_t camera_count =
      std::min(image_paths.size(), static_cast<size_t>(kMaxCameraViews));
  if (camera_count > 0) {
    std::vector<std::string> camera_children;
    std::vector<float> camera_shares;
    std::vector<std::array<uint8_t, 16>> camera_ids(camera_count);
    for (size_t i = 0; i < camera_count; ++i) {
      if (!read_uuid(&camera_ids[i])) {
        return Status::Error(path, "could not read /dev/urandom");
      }
      const std::string id = format_uuid(camera_ids[i]);
      const std::string& entity = image_paths[i];
      const std::string::size_type slash = entity.find_last_of('/');
      const std::string name =
          slash == std::string::npos ? entity : entity.substr(slash + 1);
      status = log_view(&stream, id, "2D", name.c_str(), entity, "+ " + entity + "/**");
      if (!status.ok()) {
        return status;
      }
      camera_children.push_back("view/" + id);
      camera_shares.push_back(1.f);
    }
    status = log_container(&stream, cameras,
                           rerun::blueprint::components::ContainerKind::Vertical,
                           camera_children, camera_shares);
    if (!status.ok()) {
      return status;
    }
    root_children.push_back("container/" + cameras);
    root_shares = {2.f, 1.f};
  }
  status = log_container(&stream, root,
                         rerun::blueprint::components::ContainerKind::Horizontal,
                         root_children, root_shares);
  if (!status.ok()) {
    return status;
  }
  const rerun::Error logged = stream.try_log(
      "viewport",
      rerun::blueprint::archetypes::ViewportBlueprint()
          .with_root_container(root_id)
          .with_auto_layout(false)
          .with_auto_views(false));
  if (logged.is_err()) {
    return from_rerun(logged);
  }
  return from_rerun(stream.flush_blocking());
}

Status append_file(const std::string& destination, const std::string& source) {
  std::ifstream input(source, std::ios::binary);
  std::ofstream output(destination, std::ios::binary | std::ios::app);
  if (!input || !output) {
    return Status::Error(destination, "could not append the default blueprint");
  }
  output << input.rdbuf();
  if (!output) {
    return Status::Error(destination, "could not append the default blueprint");
  }
  return Status::Ok();
}

}  // namespace

RerunSink::RerunSink() = default;

RerunSink::~RerunSink() = default;

std::string RerunSink::name() const { return "rerun"; }

Status RerunSink::open(const std::string& path, const SinkOptions& options) {
  if (open_) {
    return Status::Error(path, "sink is already open");
  }
  stream_ = std::make_unique<rerun::RecordingStream>("rrd-convert");
  const rerun::Error saved = stream_->save(path);
  if (saved.is_err()) {
    stream_.reset();
    return Status::Error(path, saved.description.empty() ? "failed to open output"
                                                        : saved.description);
  }
  path_ = path;
  image_paths_.clear();
  requested_root_ = options.root_frame;
  parent_counts_.clear();
  child_frames_.clear();
  video_codecs_.clear();
  open_ = true;
  return Status::Ok();
}

void RerunSink::set_timelines(const VisualPrimitive& primitive) {
  stream_->set_time_timestamp_nanos_since_epoch("log_time",
                                                primitive.log_time_ns);
  if (primitive.capture_time_ns.has_value()) {
    stream_->set_time_timestamp_nanos_since_epoch("capture_time",
                                                  *primitive.capture_time_ns);
  } else {
    stream_->disable_timeline("capture_time");
  }
}

std::string RerunSink::entity_path(const VisualPrimitive& primitive) const {
  std::string path = primitive.topic.empty() ? "unnamed" : primitive.topic;
  if (path.front() != '/') {
    path.insert(path.begin(), '/');
  }
  if (primitive.instance.has_value() && !primitive.instance->empty()) {
    if (path.back() != '/') {
      path.push_back('/');
    }
    path += *primitive.instance;
  }
  return path;
}

void RerunSink::remember_image(const std::string& path) {
  if (std::find(image_paths_.begin(), image_paths_.end(), path) !=
      image_paths_.end()) {
    return;
  }
  image_paths_.push_back(path);
}

Status RerunSink::log_transform(const VisualPrimitive& primitive,
                                const TransformPayload& transform) {
  parent_counts_[transform.parent_frame] += 1;
  child_frames_.insert(transform.child_frame);
  std::string path = "/tf/";
  if (!transform.child_frame.empty() && transform.child_frame.front() == '/') {
    path = "/tf" + transform.child_frame;
  } else {
    path += transform.child_frame;
  }
  auto logged =
      rerun::Transform3D::from_translation_rotation(
          {transform.translation.x, transform.translation.y,
           transform.translation.z},
          rerun::Quaternion::from_xyzw(transform.rotation.x, transform.rotation.y,
                                       transform.rotation.z, transform.rotation.w))
          .with_parent_frame(transform.parent_frame)
          .with_child_frame(transform.child_frame);
  if (transform.is_static) {
    return from_rerun(stream_->try_log_static(path, std::move(logged)));
  }
  set_timelines(primitive);
  return from_rerun(stream_->try_log(path, std::move(logged)));
}

Status RerunSink::log_image(const VisualPrimitive& primitive,
                            const ImagePayload& image) {
  const std::string path = entity_path(primitive);
  set_timelines(primitive);
  const auto resolution = rerun::WidthHeight{
      static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height)};
  rerun::Error error = rerun::Error::ok();
  if (is_depth_encoding(image.encoding)) {
    const bool u16 = image.encoding == ImageEncoding::k16Uc1;
    std::vector<uint8_t> bytes = pack_color(image, u16 ? 2 : 4);
    auto depth = rerun::DepthImage(
        rerun::take_ownership(std::move(bytes)), resolution,
        u16 ? rerun::datatypes::ChannelDatatype::U16
            : rerun::datatypes::ChannelDatatype::F32);
    depth = std::move(depth).with_meter(u16 ? 1000.0f : 1.0f);
    if (primitive.frame_id.empty()) {
      error = stream_->try_log(path, std::move(depth));
    } else {
      error = stream_->try_log(path, std::move(depth),
                              rerun::CoordinateFrame(primitive.frame_id));
    }
  } else if (image.encoding == ImageEncoding::kNv12 ||
             image.encoding == ImageEncoding::kYuv422Yuy2) {
    std::vector<uint8_t> bytes =
        image.encoding == ImageEncoding::kNv12
            ? pack_nv12(image)
            : pack_color(image, 2);
    const auto format = image.encoding == ImageEncoding::kNv12
                            ? rerun::datatypes::PixelFormat::NV12
                            : rerun::datatypes::PixelFormat::YUY2;
    rerun::Image logged(rerun::take_ownership(std::move(bytes)), resolution,
                        format);
    if (primitive.frame_id.empty()) {
      error = stream_->try_log(path, std::move(logged));
    } else {
      error = stream_->try_log(path, std::move(logged),
                              rerun::CoordinateFrame(primitive.frame_id));
    }
  } else {
    rerun::datatypes::ColorModel model = rerun::datatypes::ColorModel::RGB;
    rerun::datatypes::ChannelDatatype datatype =
        rerun::datatypes::ChannelDatatype::U8;
    int bytes_per_pixel = 3;
    switch (image.encoding) {
      case ImageEncoding::kRgb8:
        model = rerun::datatypes::ColorModel::RGB;
        bytes_per_pixel = 3;
        break;
      case ImageEncoding::kRgba8:
        model = rerun::datatypes::ColorModel::RGBA;
        bytes_per_pixel = 4;
        break;
      case ImageEncoding::kBgr8:
        model = rerun::datatypes::ColorModel::BGR;
        bytes_per_pixel = 3;
        break;
      case ImageEncoding::kBgra8:
        model = rerun::datatypes::ColorModel::BGRA;
        bytes_per_pixel = 4;
        break;
      case ImageEncoding::kMono8:
        model = rerun::datatypes::ColorModel::L;
        bytes_per_pixel = 1;
        break;
      case ImageEncoding::kMono16:
        model = rerun::datatypes::ColorModel::L;
        datatype = rerun::datatypes::ChannelDatatype::U16;
        bytes_per_pixel = 2;
        break;
      case ImageEncoding::k16Uc1:
      case ImageEncoding::k32Fc1:
      case ImageEncoding::kNv12:
      case ImageEncoding::kYuv422Yuy2:
        break;
    }
    std::vector<uint8_t> bytes = pack_color(image, bytes_per_pixel);
    rerun::Image logged(rerun::take_ownership(std::move(bytes)), resolution, model,
                        datatype);
    if (primitive.frame_id.empty()) {
      error = stream_->try_log(path, std::move(logged));
    } else {
      error = stream_->try_log(path, std::move(logged),
                              rerun::CoordinateFrame(primitive.frame_id));
    }
  }
  return from_rerun(error);
}

Status RerunSink::log_points(const VisualPrimitive& primitive,
                             const Points3DPayload& points) {
  std::vector<rerun::Position3D> positions;
  positions.reserve(points.positions.size());
  for (const Vec3f& position : points.positions) {
    positions.emplace_back(position.x, position.y, position.z);
  }
  auto cloud = rerun::Points3D(rerun::take_ownership(std::move(positions)));
  if (!points.colors.empty()) {
    std::vector<rerun::Color> colors;
    colors.reserve(points.colors.size());
    for (const Rgba8& color : points.colors) {
      colors.emplace_back(color.r, color.g, color.b, color.a);
    }
    cloud = std::move(cloud).with_colors(rerun::take_ownership(std::move(colors)));
  }
  if (!points.radii.empty()) {
    std::vector<rerun::Radius> radii;
    radii.reserve(points.radii.size());
    for (float radius : points.radii) {
      radii.push_back(rerun::Radius::scene_units(radius));
    }
    cloud = std::move(cloud).with_radii(rerun::take_ownership(std::move(radii)));
  }
  set_timelines(primitive);
  const std::string path = entity_path(primitive);
  if (primitive.frame_id.empty()) {
    return from_rerun(stream_->try_log(path, std::move(cloud)));
  }
  return from_rerun(stream_->try_log(
      path, std::move(cloud), rerun::CoordinateFrame(primitive.frame_id)));
}

Status RerunSink::log_encoded_image(const VisualPrimitive& primitive,
                                    const EncodedImagePayload& image) {
  std::vector<uint8_t> bytes = image.bytes;
  const char* media = image.format == EncodedImagePayload::Format::kPng
                          ? "image/png"
                          : "image/jpeg";
  auto encoded = rerun::EncodedImage::from_bytes(
      rerun::take_ownership(std::move(bytes)), rerun::MediaType(media));
  set_timelines(primitive);
  return log_temporal(stream_.get(), entity_path(primitive), primitive.frame_id,
                      std::move(encoded));
}

Status RerunSink::log_video(const VisualPrimitive& primitive,
                            const VideoFramePayload& frame) {
  const std::string path = entity_path(primitive);
  const int codec = frame.codec == VideoFramePayload::Codec::kH265 ? 2 : 1;
  const auto known = video_codecs_.find(path);
  if (known == video_codecs_.end() || known->second != codec) {
    const auto rerun_codec = codec == 2 ? rerun::components::VideoCodec::H265
                                        : rerun::components::VideoCodec::H264;
    const Status logged =
        from_rerun(stream_->try_log_static(path, rerun::VideoStream(rerun_codec)));
    if (!logged.ok()) {
      return logged;
    }
    video_codecs_[path] = codec;
  }
  std::vector<uint8_t> bytes = frame.annex_b;
  auto sample = rerun::VideoStream().with_sample(rerun::components::VideoSample(
      rerun::take_ownership(std::move(bytes))));
  set_timelines(primitive);
  return log_temporal(stream_.get(), path, primitive.frame_id, std::move(sample));
}

Status RerunSink::log_shapes(const VisualPrimitive& primitive,
                             const Shapes3DPayload& shapes) {
  std::vector<rerun::components::Translation3D> box_centers;
  std::vector<rerun::HalfSize3D> box_sizes;
  std::vector<rerun::components::RotationQuat> box_rotations;
  std::vector<rerun::Color> box_colors;
  std::vector<rerun::components::Translation3D> ellipsoid_centers;
  std::vector<rerun::HalfSize3D> ellipsoid_sizes;
  std::vector<rerun::components::RotationQuat> ellipsoid_rotations;
  std::vector<rerun::Color> ellipsoid_colors;
  std::vector<rerun::components::Length> cylinder_lengths;
  std::vector<rerun::Radius> cylinder_radii;
  std::vector<rerun::components::Translation3D> cylinder_centers;
  std::vector<rerun::components::RotationQuat> cylinder_rotations;
  std::vector<rerun::Color> cylinder_colors;
  for (const Shape3D& shape : shapes.shapes) {
    const auto center = rerun::components::Translation3D(
        shape.center.x, shape.center.y, shape.center.z);
    const auto rotation = rerun::components::RotationQuat(to_quaternion(shape.rotation));
    const auto color = to_color(shape.color);
    if (shape.kind == ShapeKind::kBox) {
      box_centers.push_back(center);
      box_sizes.emplace_back(shape.half_sizes.x, shape.half_sizes.y,
                             shape.half_sizes.z);
      box_rotations.push_back(rotation);
      box_colors.push_back(color);
    } else if (shape.kind == ShapeKind::kEllipsoid) {
      ellipsoid_centers.push_back(center);
      ellipsoid_sizes.emplace_back(shape.half_sizes.x, shape.half_sizes.y,
                                   shape.half_sizes.z);
      ellipsoid_rotations.push_back(rotation);
      ellipsoid_colors.push_back(color);
    } else {
      cylinder_lengths.emplace_back(shape.half_sizes.z * 2.f);
      cylinder_radii.push_back(rerun::Radius::scene_units(shape.half_sizes.x));
      cylinder_centers.push_back(center);
      cylinder_rotations.push_back(rotation);
      cylinder_colors.push_back(color);
    }
  }
  set_timelines(primitive);
  const std::string path = entity_path(primitive);
  if (!box_centers.empty()) {
    auto boxes = rerun::Boxes3D::from_centers_and_half_sizes(
                     rerun::take_ownership(std::move(box_centers)),
                     rerun::take_ownership(std::move(box_sizes)))
                     .with_quaternions(rerun::take_ownership(std::move(box_rotations)))
                     .with_colors(rerun::take_ownership(std::move(box_colors)));
    const Status logged =
        log_temporal(stream_.get(), path, primitive.frame_id, std::move(boxes));
    if (!logged.ok()) {
      return logged;
    }
  }
  if (!ellipsoid_centers.empty()) {
    auto ellipsoids = rerun::Ellipsoids3D::from_centers_and_half_sizes(
                          rerun::take_ownership(std::move(ellipsoid_centers)),
                          rerun::take_ownership(std::move(ellipsoid_sizes)))
                          .with_quaternions(
                              rerun::take_ownership(std::move(ellipsoid_rotations)))
                          .with_colors(
                              rerun::take_ownership(std::move(ellipsoid_colors)));
    const Status logged = log_temporal(stream_.get(), path, primitive.frame_id,
                                      std::move(ellipsoids));
    if (!logged.ok()) {
      return logged;
    }
  }
  if (!cylinder_centers.empty()) {
    auto cylinders =
        rerun::Cylinders3D::from_lengths_and_radii(
            rerun::take_ownership(std::move(cylinder_lengths)),
            rerun::take_ownership(std::move(cylinder_radii)))
            .with_centers(rerun::take_ownership(std::move(cylinder_centers)))
            .with_quaternions(rerun::take_ownership(std::move(cylinder_rotations)))
            .with_colors(rerun::take_ownership(std::move(cylinder_colors)));
    return log_temporal(stream_.get(), path, primitive.frame_id,
                        std::move(cylinders));
  }
  return Status::Ok();
}

Status RerunSink::log_arrows(const VisualPrimitive& primitive,
                             const Arrows3DPayload& arrows) {
  if (arrows.arrows.empty()) {
    return Status::Ok();
  }
  std::vector<rerun::Vector3D> vectors;
  std::vector<rerun::Position3D> origins;
  std::vector<rerun::Color> colors;
  vectors.reserve(arrows.arrows.size());
  for (const Arrow3D& arrow : arrows.arrows) {
    vectors.emplace_back(arrow.vector.x, arrow.vector.y, arrow.vector.z);
    origins.emplace_back(arrow.origin.x, arrow.origin.y, arrow.origin.z);
    colors.push_back(to_color(arrow.color));
  }
  auto logged = rerun::Arrows3D::from_vectors(rerun::take_ownership(std::move(vectors)))
                    .with_origins(rerun::take_ownership(std::move(origins)))
                    .with_colors(rerun::take_ownership(std::move(colors)));
  set_timelines(primitive);
  return log_temporal(stream_.get(), entity_path(primitive), primitive.frame_id,
                      std::move(logged));
}

Status RerunSink::log_lines(const VisualPrimitive& primitive,
                            const LineStrips3DPayload& lines) {
  if (lines.strips.empty()) {
    return Status::Ok();
  }
  std::vector<rerun::LineStrip3D> strips;
  strips.reserve(lines.strips.size());
  for (const std::vector<Vec3f>& strip : lines.strips) {
    std::vector<rerun::Vec3D> points;
    points.reserve(strip.size());
    for (const Vec3f& point : strip) {
      points.emplace_back(point.x, point.y, point.z);
    }
    strips.emplace_back(rerun::take_ownership(std::move(points)));
  }
  auto logged =
      rerun::LineStrips3D(rerun::take_ownership(std::move(strips)));
  if (lines.width.has_value()) {
    logged = std::move(logged).with_radii(
        rerun::Radius::scene_units(*lines.width * 0.5f));
  }
  if (lines.color.has_value()) {
    logged = std::move(logged).with_colors(to_color(*lines.color));
  }
  set_timelines(primitive);
  return log_temporal(stream_.get(), entity_path(primitive), primitive.frame_id,
                      std::move(logged));
}

Status RerunSink::log_labels(const VisualPrimitive& primitive,
                             const TextLabels3DPayload& labels) {
  if (labels.labels.empty()) {
    return Status::Ok();
  }
  std::vector<rerun::Position3D> positions;
  std::vector<rerun::Text> texts;
  positions.reserve(labels.labels.size());
  for (const TextLabel3D& label : labels.labels) {
    positions.emplace_back(label.position.x, label.position.y, label.position.z);
    texts.emplace_back(label.text);
  }
  auto logged = rerun::Points3D(rerun::take_ownership(std::move(positions)))
                    .with_labels(rerun::take_ownership(std::move(texts)));
  set_timelines(primitive);
  return log_temporal(stream_.get(), entity_path(primitive), primitive.frame_id,
                      std::move(logged));
}

Status RerunSink::log_clear(const VisualPrimitive& primitive,
                            const ClearPayload& clear) {
  set_timelines(primitive);
  return from_rerun(
      stream_->try_log(entity_path(primitive), rerun::Clear(clear.recursive)));
}

Status RerunSink::write(const VisualPrimitive& primitive) {
  if (!open_ || stream_ == nullptr) {
    return Status::Error("", "sink is not open");
  }
  if (std::holds_alternative<ImagePayload>(primitive.payload) ||
      std::holds_alternative<EncodedImagePayload>(primitive.payload) ||
      std::holds_alternative<VideoFramePayload>(primitive.payload)) {
    remember_image(entity_path(primitive));
  }
  if (const auto* transform = std::get_if<TransformPayload>(&primitive.payload)) {
    return log_transform(primitive, *transform);
  }
  if (const auto* image = std::get_if<ImagePayload>(&primitive.payload)) {
    return log_image(primitive, *image);
  }
  if (const auto* points = std::get_if<Points3DPayload>(&primitive.payload)) {
    return log_points(primitive, *points);
  }
  if (const auto* encoded = std::get_if<EncodedImagePayload>(&primitive.payload)) {
    return log_encoded_image(primitive, *encoded);
  }
  if (const auto* video = std::get_if<VideoFramePayload>(&primitive.payload)) {
    return log_video(primitive, *video);
  }
  if (const auto* shapes = std::get_if<Shapes3DPayload>(&primitive.payload)) {
    return log_shapes(primitive, *shapes);
  }
  if (const auto* arrows = std::get_if<Arrows3DPayload>(&primitive.payload)) {
    return log_arrows(primitive, *arrows);
  }
  if (const auto* lines = std::get_if<LineStrips3DPayload>(&primitive.payload)) {
    return log_lines(primitive, *lines);
  }
  if (const auto* labels = std::get_if<TextLabels3DPayload>(&primitive.payload)) {
    return log_labels(primitive, *labels);
  }
  if (const auto* clear = std::get_if<ClearPayload>(&primitive.payload)) {
    return log_clear(primitive, *clear);
  }
  return Status::Error("", "unsupported primitive");
}

Status RerunSink::close() {
  if (!open_ || stream_ == nullptr) {
    return Status::Ok();
  }
  const std::optional<std::string> root =
      select_root_frame(requested_root_, parent_counts_, child_frames_);
  rerun::Error error = rerun::Error::ok();
  if (root.has_value() && !root->empty()) {
    error = stream_->try_log_static("/", rerun::CoordinateFrame(*root));
  }
  if (error.is_ok()) {
    error = stream_->flush_blocking();
  }
  stream_.reset();
  if (error.is_ok()) {
    const std::string blueprint_path = path_ + ".blueprint";
    try {
      const Status blueprint = write_blueprint_file(blueprint_path, image_paths_);
      if (!blueprint.ok()) {
        std::cerr << "warning: " << path_
                  << ": default blueprint was not written: " << blueprint.reason()
                  << "\n";
      } else {
        const Status appended = append_file(path_, blueprint_path);
        if (!appended.ok()) {
          std::cerr << "warning: " << path_
                    << ": default blueprint was not written: " << appended.reason()
                    << "\n";
        }
      }
    } catch (const std::exception& ex) {
      std::cerr << "warning: " << path_
                << ": default blueprint was not written: " << ex.what() << "\n";
    }
    std::remove(blueprint_path.c_str());
  }
  open_ = false;
  path_.clear();
  image_paths_.clear();
  parent_counts_.clear();
  child_frames_.clear();
  video_codecs_.clear();
  requested_root_.reset();
  return from_rerun(error);
}

}  // namespace rrd_convert
