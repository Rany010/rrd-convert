#include "decode/point_cloud_decoder.hpp"

#include "decode/ros_cdr.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace rrd_convert {
namespace {

constexpr uint8_t kFloat32 = 7;
constexpr uint8_t kUint32 = 6;

struct Field {
  std::string name;
  uint32_t offset = 0;
  uint8_t datatype = 0;
  uint32_t count = 0;
};

const Field* find_field(const std::vector<Field>& fields, const char* name) {
  for (const Field& field : fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

bool read_f32_le(const uint8_t* point, uint32_t point_step, const Field& field,
                 float* out) {
  if (field.datatype != kFloat32 || field.count < 1 ||
      field.offset + sizeof(float) > point_step) {
    return false;
  }
  std::memcpy(out, point + field.offset, sizeof(float));
  return true;
}

bool read_packed_color(const uint8_t* point, uint32_t point_step,
                       const Field& field, Rgba8* out, bool has_alpha) {
  if (field.count < 1 || field.offset + 4 > point_step) {
    return false;
  }
  if (field.datatype != kFloat32 && field.datatype != kUint32) {
    return false;
  }
  uint32_t bits = 0;
  std::memcpy(&bits, point + field.offset, sizeof(bits));
  out->b = static_cast<uint8_t>(bits & 0xff);
  out->g = static_cast<uint8_t>((bits >> 8) & 0xff);
  out->r = static_cast<uint8_t>((bits >> 16) & 0xff);
  out->a = has_alpha ? static_cast<uint8_t>((bits >> 24) & 0xff) : 255;
  return true;
}

uint8_t gray_from_intensity(float value, float min_value, float max_value) {
  if (max_value <= min_value) {
    return 255;
  }
  const float scaled = (value - min_value) / (max_value - min_value);
  if (scaled <= 0.f) {
    return 0;
  }
  if (scaled >= 1.f) {
    return 255;
  }
  return static_cast<uint8_t>(scaled * 255.f + 0.5f);
}

}  // namespace

std::string PointCloud2Decoder::type_name() const {
  return "sensor_msgs/msg/PointCloud2";
}

DecodeResult PointCloud2Decoder::decode(const InputRecord& record,
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
  uint32_t field_count = 0;
  if (!ros_cdr::read_header(&reader, &stamp_ns, &frame_id) ||
      !reader.read_u32(&height) || !reader.read_u32(&width) ||
      !reader.read_sequence_length(&field_count)) {
    return DecodeResult::Error(reader.error());
  }

  std::vector<Field> fields;
  fields.reserve(field_count);
  for (uint32_t i = 0; i < field_count; ++i) {
    Field field;
    if (!reader.read_string(&field.name) || !reader.read_u32(&field.offset) ||
        !reader.read_u8(&field.datatype) || !reader.read_u32(&field.count)) {
      return DecodeResult::Error(reader.error());
    }
    fields.push_back(std::move(field));
  }

  uint8_t is_bigendian = 0;
  uint32_t point_step = 0;
  uint32_t row_step = 0;
  uint32_t data_length = 0;
  if (!reader.read_u8(&is_bigendian) || !reader.read_u32(&point_step) ||
      !reader.read_u32(&row_step) || !reader.read_sequence_length(&data_length)) {
    return DecodeResult::Error(reader.error());
  }
  if (record.bytes.size() - reader.offset() < data_length) {
    return DecodeResult::Error("truncated point cloud");
  }
  if (is_bigendian != 0) {
    return DecodeResult::Error("big-endian point clouds are not supported");
  }
  if (point_step == 0) {
    return DecodeResult::Error("point cloud point_step is zero");
  }

  const Field* x_field = find_field(fields, "x");
  const Field* y_field = find_field(fields, "y");
  const Field* z_field = find_field(fields, "z");
  if (x_field == nullptr || y_field == nullptr || z_field == nullptr ||
      x_field->datatype != kFloat32 || y_field->datatype != kFloat32 ||
      z_field->datatype != kFloat32) {
    return DecodeResult::Error("point cloud requires float32 x, y, and z");
  }

  if (width == 0 || height == 0) {
    if (data_length == 0) {
      Points3DPayload empty;
      VisualPrimitive primitive;
      primitive.topic = record.topic;
      primitive.frame_id = std::move(frame_id);
      primitive.log_time_ns = record.log_time_ns;
      set_capture_time(&primitive, stamp_ns);
      primitive.payload = std::move(empty);
      return DecodeResult::Ok({std::move(primitive)});
    }
    return DecodeResult::Error("point cloud width or height is zero");
  }

  const uint64_t tight_row =
      static_cast<uint64_t>(width) * static_cast<uint64_t>(point_step);
  const uint64_t used_row =
      row_step == 0 ? tight_row : static_cast<uint64_t>(row_step);
  if (used_row < tight_row) {
    return DecodeResult::Error("point cloud row_step is smaller than a row");
  }
  const uint64_t required = used_row * height;
  if (static_cast<uint64_t>(data_length) < required) {
    return DecodeResult::Error("truncated point cloud");
  }

  const uint8_t* data = record.bytes.data() + reader.offset();
  const Field* rgb = find_field(fields, "rgb");
  const Field* rgba = find_field(fields, "rgba");
  const Field* color_field = rgba != nullptr ? rgba : rgb;
  const bool has_alpha = rgba != nullptr;
  const Field* intensity = find_field(fields, "intensity");
  const bool use_intensity =
      color_field == nullptr && intensity != nullptr &&
      intensity->datatype == kFloat32 && intensity->count >= 1 &&
      intensity->offset + sizeof(float) <= point_step;

  std::vector<Vec3f> positions;
  positions.reserve(static_cast<size_t>(width) * height);
  std::vector<float> intensities;
  for (uint32_t row = 0; row < height; ++row) {
    for (uint32_t col = 0; col < width; ++col) {
      const uint8_t* point =
          data + row * used_row + static_cast<uint64_t>(col) * point_step;
      Vec3f position;
      if (!read_f32_le(point, point_step, *x_field, &position.x) ||
          !read_f32_le(point, point_step, *y_field, &position.y) ||
          !read_f32_le(point, point_step, *z_field, &position.z)) {
        return DecodeResult::Error("point field extends past point_step");
      }
      positions.push_back(position);
      if (use_intensity) {
        float value = 0.f;
        std::memcpy(&value, point + intensity->offset, sizeof(value));
        intensities.push_back(value);
      }
    }
  }

  Points3DPayload payload;
  payload.positions = std::move(positions);
  if (color_field != nullptr) {
    payload.colors.reserve(payload.positions.size());
    for (uint32_t row = 0; row < height; ++row) {
      for (uint32_t col = 0; col < width; ++col) {
        const uint8_t* point =
            data + row * used_row + static_cast<uint64_t>(col) * point_step;
        Rgba8 color;
        if (!read_packed_color(point, point_step, *color_field, &color,
                               has_alpha)) {
          return DecodeResult::Error("point field extends past point_step");
        }
        payload.colors.push_back(color);
      }
    }
  } else if (use_intensity) {
    float min_value = intensities.front();
    float max_value = intensities.front();
    for (float value : intensities) {
      if (value < min_value) {
        min_value = value;
      }
      if (value > max_value) {
        max_value = value;
      }
    }
    payload.colors.reserve(intensities.size());
    for (float value : intensities) {
      const uint8_t gray = gray_from_intensity(value, min_value, max_value);
      payload.colors.push_back(Rgba8{gray, gray, gray, 255});
    }
  }

  VisualPrimitive primitive;
  primitive.topic = record.topic;
  primitive.frame_id = std::move(frame_id);
  primitive.log_time_ns = record.log_time_ns;
  set_capture_time(&primitive, stamp_ns);
  primitive.payload = std::move(payload);
  return DecodeResult::Ok({std::move(primitive)});
}

}  // namespace rrd_convert
