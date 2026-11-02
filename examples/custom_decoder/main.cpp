#include "app/run_cli.hpp"
#include "decode/read_dynamic.hpp"

#include <memory>
#include <string>
#include <utility>

namespace {
namespace rrd = rrd_convert;

// Maps example_msgs/msg/Detection to one labeled 3D point.
//
//   geometry_msgs/Point position
//   string label
//
// A bag definition is used when it is ros2msg. Otherwise pass
// `--msg-path` pointing at this directory's `msgs` folder.
class DetectionDecoder : public rrd::Decoder {
 public:
  std::string type_name() const override {
    return "example_msgs/msg/Detection";
  }

  rrd::DecodeResult decode(const rrd::InputRecord& record,
                           rrd::DecodeContext& context) override {
    rrd::DynamicValue value;
    std::string error;
    if (!rrd::read_dynamic_record(context, record, &value, &error)) {
      return rrd::DecodeResult::Error(std::move(error));
    }
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    if (!read_number(value, "position.x", &x, &error) ||
        !read_number(value, "position.y", &y, &error) ||
        !read_number(value, "position.z", &z, &error)) {
      return rrd::DecodeResult::Error(std::move(error));
    }
    const rrd::DynamicValue* label = rrd::find_path(value, "label", &error);
    if (label == nullptr || label->kind != rrd::DynamicValue::Kind::kString) {
      if (error.empty()) {
        error = "label is not a string";
      }
      return rrd::DecodeResult::Error(std::move(error));
    }

    rrd::TextLabel3D text;
    text.position = rrd::Vec3f{x, y, z};
    text.text = label->string_value;
    rrd::TextLabels3DPayload payload;
    payload.labels.push_back(std::move(text));

    rrd::VisualPrimitive primitive;
    primitive.topic = record.topic;
    primitive.log_time_ns = record.log_time_ns;
    primitive.payload = std::move(payload);
    return rrd::DecodeResult::Ok({std::move(primitive)});
  }

 private:
  static bool read_number(const rrd::DynamicValue& value, const char* path,
                          float* out, std::string* error) {
    const rrd::DynamicValue* field = rrd::find_path(value, path, error);
    if (field == nullptr) {
      return false;
    }
    double number = 0;
    if (!field->number(&number)) {
      *error = std::string(path) + " is not a number";
      return false;
    }
    *out = static_cast<float>(number);
    return true;
  }
};

void register_detection(rrd::Registry& registry) {
  registry.add_decoder("example_msgs/msg/Detection", [] {
    return std::make_unique<DetectionDecoder>();
  });
}

}  // namespace

int main(int argc, char** argv) {
  return rrd_convert::run_cli(argc, argv, register_detection);
}
