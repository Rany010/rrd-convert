#include "cdr/dynamic_message.hpp"
#include "cdr/msg_parser.hpp"
#include "cdr_writer.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

namespace rrd_convert {
namespace {

const Field* FindField(const MessageCatalog& catalog, const std::string& type_name,
                       const std::string& name) {
  const auto found = catalog.types.find(type_name);
  if (found == catalog.types.end()) {
    return nullptr;
  }
  for (const Field& field : found->second.fields) {
    if (field.name == name) {
      return &field;
    }
  }
  return nullptr;
}

std::string Separator() { return std::string(80, '='); }

TEST(MsgParser, ParsesFieldsConstantsArraysAndNestedTypes) {
  const std::string text =
      "# comment\r\n"
      "uint32 CONST = 1\n"
      "byte data\n"
      "string<=8 name\n"
      "float64[2] pair\n"
      "int32[<=4] limited\n"
      "uint8[] flags\n"
      "geometry_msgs/Point origin\n"
      "Point local\n" +
      Separator() +
      "\nMSG: geometry_msgs/Point\n"
      "float64 x\n"
      "float64 y\n"
      "float64 z\n";
  MessageCatalog catalog;
  std::string error;
  ASSERT_TRUE(parse_ros2msg("demo_msgs/Sample", text, &catalog, &error)) << error;

  const Field* data = FindField(catalog, "demo_msgs/msg/Sample", "data");
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->type.kind, FieldKind::kUint8);
  EXPECT_EQ(FindField(catalog, "demo_msgs/msg/Sample", "CONST"), nullptr);

  const Field* name = FindField(catalog, "demo_msgs/msg/Sample", "name");
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->type.kind, FieldKind::kString);
  EXPECT_EQ(name->type.string_bound, 8u);
  EXPECT_EQ(name->type.form, ArrayForm::kScalar);

  const Field* pair = FindField(catalog, "demo_msgs/msg/Sample", "pair");
  ASSERT_NE(pair, nullptr);
  EXPECT_EQ(pair->type.form, ArrayForm::kFixed);
  EXPECT_EQ(pair->type.length, 2u);

  const Field* limited = FindField(catalog, "demo_msgs/msg/Sample", "limited");
  ASSERT_NE(limited, nullptr);
  EXPECT_EQ(limited->type.form, ArrayForm::kBounded);
  EXPECT_EQ(limited->type.length, 4u);

  const Field* flags = FindField(catalog, "demo_msgs/msg/Sample", "flags");
  ASSERT_NE(flags, nullptr);
  EXPECT_EQ(flags->type.form, ArrayForm::kSequence);

  const Field* origin = FindField(catalog, "demo_msgs/msg/Sample", "origin");
  ASSERT_NE(origin, nullptr);
  EXPECT_EQ(origin->type.message_type, "geometry_msgs/msg/Point");
  const Field* local = FindField(catalog, "demo_msgs/msg/Sample", "local");
  ASSERT_NE(local, nullptr);
  EXPECT_EQ(local->type.message_type, "demo_msgs/msg/Point");
  EXPECT_NE(catalog.types.find("geometry_msgs/msg/Point"), catalog.types.end());

  MessageCatalog duplicate;
  const std::string again = text + Separator() + "\nMSG: demo_msgs/msg/Sample\nuint8 other\n";
  EXPECT_FALSE(parse_ros2msg("demo_msgs/msg/Sample", again, &duplicate, &error));
  EXPECT_NE(error.find("duplicate message type"), std::string::npos);

  EXPECT_FALSE(parse_ros2msg("demo_msgs/msg/Sample", "uint32\n", &duplicate, &error));
  EXPECT_NE(error.find("invalid field line"), std::string::npos);
}

TEST(DynamicMessage, ReadsNestedPathsArraysAndBounds) {
  const std::string text =
      "uint32 CONST = 1\n"
      "int32 count\n"
      "string name\n"
      "float64[2] pair\n"
      "int32[<=4] limited\n"
      "string<=4 label\n"
      "uint8[] flags\n"
      "geometry_msgs/Point origin\n"
      "geometry_msgs/Point[] points\n" +
      Separator() +
      "\nMSG: geometry_msgs/msg/Point\n"
      "float64 x\n"
      "float64 y\n"
      "float64 z\n";
  CdrWriter writer;
  writer.write_i32(-3);
  writer.write_string("cam");
  writer.write_f64(1.5);
  writer.write_f64(-2.0);
  writer.write_sequence_length(2);
  writer.write_i32(4);
  writer.write_i32(5);
  writer.write_string("ab");
  writer.write_sequence_length(2);
  writer.write_u8(9);
  writer.write_u8(8);
  writer.write_f64(1);
  writer.write_f64(2);
  writer.write_f64(3);
  writer.write_sequence_length(1);
  writer.write_f64(4);
  writer.write_f64(5);
  writer.write_f64(6);

  DynamicValue value;
  std::string error;
  ASSERT_TRUE(read_defined_message("demo_msgs/msg/Sample", "ros2msg", text, {},
                                   writer.bytes().data(), writer.bytes().size(),
                                   &value, &error))
      << error;

  const DynamicValue* count = find_path(value, "count", &error);
  ASSERT_NE(count, nullptr) << error;
  EXPECT_EQ(count->kind, DynamicValue::Kind::kInt);
  EXPECT_EQ(count->int_value, -3);

  const DynamicValue* name = find_path(value, "name", &error);
  ASSERT_NE(name, nullptr);
  EXPECT_EQ(name->string_value, "cam");

  const DynamicValue* pair = find_path(value, "pair[1]", &error);
  ASSERT_NE(pair, nullptr) << error;
  EXPECT_DOUBLE_EQ(pair->float_value, -2.0);
  double number = 0;
  EXPECT_TRUE(pair->number(&number));
  EXPECT_DOUBLE_EQ(number, -2.0);

  const DynamicValue* limited = find_path(value, "limited[0]", &error);
  ASSERT_NE(limited, nullptr);
  EXPECT_EQ(limited->int_value, 4);
  const DynamicValue* label = find_path(value, "label", &error);
  ASSERT_NE(label, nullptr);
  EXPECT_EQ(label->string_value, "ab");
  const DynamicValue* flag = find_path(value, "flags[1]", &error);
  ASSERT_NE(flag, nullptr);
  EXPECT_EQ(flag->uint_value, 8u);
  const DynamicValue* origin_y = find_path(value, "origin.y", &error);
  ASSERT_NE(origin_y, nullptr) << error;
  EXPECT_DOUBLE_EQ(origin_y->float_value, 2);
  const DynamicValue* point_z = find_path(value, "points[0].z", &error);
  ASSERT_NE(point_z, nullptr) << error;
  EXPECT_DOUBLE_EQ(point_z->float_value, 6);

  EXPECT_EQ(find_path(value, "pair[2]", &error), nullptr);
  EXPECT_NE(error.find("index out of range"), std::string::npos);
  EXPECT_EQ(find_path(value, "count[0]", &error), nullptr);
  EXPECT_NE(error.find("value is not an array"), std::string::npos);
  EXPECT_EQ(find_path(value, "missing", &error), nullptr);
  EXPECT_NE(error.find("field not found"), std::string::npos);
  EXPECT_EQ(find_path(value, "origin.", &error), nullptr);
  EXPECT_NE(error.find("invalid field path"), std::string::npos);
}

TEST(DynamicMessage, RejectsBoundsRecursionTruncationAndUnknownEncapsulation) {
  DynamicValue value;
  std::string error;
  {
    CdrWriter writer;
    writer.write_string("12345");
    EXPECT_FALSE(read_defined_message("demo_msgs/msg/Sample", "ros2msg",
                                      "string<=4 label\n", {}, writer.bytes().data(),
                                      writer.bytes().size(), &value, &error));
    EXPECT_NE(error.find("bounded string exceeds"), std::string::npos);
  }
  {
    CdrWriter writer;
    writer.write_sequence_length(2);
    writer.write_i32(1);
    writer.write_i32(2);
    EXPECT_FALSE(read_defined_message("demo_msgs/msg/Sample", "ros2msg",
                                      "int32[<=1] values\n", {}, writer.bytes().data(),
                                      writer.bytes().size(), &value, &error));
    EXPECT_NE(error.find("bounded sequence exceeds"), std::string::npos);
  }
  {
    CdrWriter writer;
    writer.write_sequence_length(2);
    writer.write_u32(static_cast<uint32_t>('A'));
    writer.write_u32(0x4E16);
    ASSERT_TRUE(read_defined_message("demo_msgs/msg/Sample", "ros2msg",
                                     "wstring text\n", {}, writer.bytes().data(),
                                     writer.bytes().size(), &value, &error))
        << error;
    const DynamicValue* text = find_path(value, "text", &error);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->string_value, "A\xE4\xB8\x96");
  }
  {
    CdrWriter writer;
    writer.write_u8(0xFF);
    writer.write_u8(0x41);
    ASSERT_TRUE(read_defined_message("demo_msgs/msg/Sample", "ros2msg",
                                     "byte data\nchar code\n", {},
                                     writer.bytes().data(), writer.bytes().size(),
                                     &value, &error))
        << error;
    EXPECT_EQ(find_path(value, "data", &error)->uint_value, 0xFFu);
    EXPECT_EQ(find_path(value, "code", &error)->uint_value, 0x41u);
  }
  {
    const uint8_t header[] = {0x00, 0x01, 0x00, 0x00};
    ASSERT_TRUE(read_defined_message("demo_msgs/msg/Sample", "ros2msg",
                                     "uint32 CONST = 1\n", {}, header, sizeof(header),
                                     &value, &error))
        << error;
    EXPECT_EQ(find_path(value, "CONST", &error), nullptr);
  }
  {
    const uint8_t header[] = {0x00, 0x01, 0x00, 0x00};
    EXPECT_FALSE(read_defined_message(
        "demo_msgs/msg/Loop", "ros2msg", "demo_msgs/msg/Loop loop\n", {}, header,
        sizeof(header), &value, &error));
    EXPECT_NE(error.find("recursive message type"), std::string::npos);
  }
  {
    const uint8_t header[] = {0x00, 0x01, 0x00, 0x00};
    EXPECT_FALSE(read_defined_message("demo_msgs/msg/Sample", "ros2msg",
                                      "uint32 value\n", {}, header, sizeof(header),
                                      &value, &error));
    EXPECT_NE(error.find("truncated"), std::string::npos);
  }
  {
    const uint8_t bytes[] = {0x00, 0x11, 0x00, 0x00};
    EXPECT_FALSE(read_defined_message("demo_msgs/msg/Sample", "ros2msg",
                                      "uint32 value\n", {}, bytes, sizeof(bytes),
                                      &value, &error));
    EXPECT_NE(error.find("unknown cdr encapsulation"), std::string::npos);
  }
  {
    CdrWriter writer(false);
    writer.write_u32(0x01020304);
    ASSERT_TRUE(read_defined_message("demo_msgs/msg/Sample", "ros2msg",
                                     "uint32 value\n", {}, writer.bytes().data(),
                                     writer.bytes().size(), &value, &error))
        << error;
    EXPECT_EQ(find_path(value, "value", &error)->uint_value, 0x01020304u);
  }
}

class MsgPathDir : public ::testing::Test {
 protected:
  void SetUp() override {
    root_ = std::filesystem::temp_directory_path() /
            ("rrd-convert-msg-" + std::to_string(::getpid()) + "-" +
             std::to_string(++seq_));
    std::filesystem::create_directories(root_ / "demo_msgs" / "msg");
    std::filesystem::create_directories(root_ / "geometry_msgs" / "msg");
  }

  void TearDown() override { std::filesystem::remove_all(root_); }

  static int seq_;
  std::filesystem::path root_;
};

int MsgPathDir::seq_ = 0;

TEST_F(MsgPathDir, LoadsDependenciesAndPrefersBagRos2Msg) {
  std::ofstream(root_ / "demo_msgs" / "msg" / "Sample.msg")
      << "# comment\nuint32 CONST = 3\ngeometry_msgs/Point origin\nuint32 value\n";
  std::ofstream(root_ / "geometry_msgs" / "msg" / "Point.msg")
      << "float64 x\nfloat64 y\nfloat64 z\n";
  const std::filesystem::path empty = root_ / "empty";
  std::filesystem::create_directories(empty);

  CdrWriter writer;
  writer.write_f64(0);
  writer.write_f64(1.5);
  writer.write_f64(0);
  writer.write_u32(7);
  DynamicValue value;
  std::string error;
  ASSERT_TRUE(read_defined_message(
      "demo_msgs/msg/Sample", "", "", {empty.string(), root_.string()},
      writer.bytes().data(), writer.bytes().size(), &value, &error))
      << error;
  EXPECT_DOUBLE_EQ(find_path(value, "origin.y", &error)->float_value, 1.5);
  EXPECT_EQ(find_path(value, "value", &error)->uint_value, 7u);

  MessageCatalog catalog;
  ASSERT_EQ(load_message_files({root_.string()}, "demo_msgs/Sample", &catalog, &error),
            MessageLoadCode::kOk)
      << error;
  const std::string exported = export_ros2msg(catalog, "demo_msgs/msg/Sample");
  EXPECT_NE(exported.find("MSG: geometry_msgs/msg/Point"), std::string::npos);
  EXPECT_NE(exported.find("float64 y"), std::string::npos);
  EXPECT_EQ(exported.find("CONST"), std::string::npos);

  const ResolvedDefinition from_bag = resolve_message_definition(
      "demo_msgs/msg/Sample", std::optional<std::string>("uint32 from_bag\n"),
      std::optional<std::string>("ros2msg"), {root_.string()});
  EXPECT_EQ(from_bag.kind, ResolvedDefinition::Kind::kRos2Msg);
  EXPECT_EQ(from_bag.text, "uint32 from_bag\n");

  const ResolvedDefinition from_idl = resolve_message_definition(
      "demo_msgs/msg/Sample", std::nullopt, std::optional<std::string>("ros2idl"),
      {root_.string()});
  EXPECT_EQ(from_idl.kind, ResolvedDefinition::Kind::kRos2Msg);
  EXPECT_NE(from_idl.text.find("uint32 value"), std::string::npos);

  std::filesystem::remove(root_ / "geometry_msgs" / "msg" / "Point.msg");
  const ResolvedDefinition missing_dep = resolve_message_definition(
      "demo_msgs/msg/Sample", std::nullopt, std::nullopt, {root_.string()});
  EXPECT_EQ(missing_dep.kind, ResolvedDefinition::Kind::kInvalid);
  EXPECT_NE(missing_dep.error.find("message definition not found"), std::string::npos);
}

TEST(DynamicMessage, FillsANestedTypeFromMsgPath) {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("rrd-convert-fill-" + std::to_string(::getpid()));
  std::filesystem::create_directories(root / "geometry_msgs" / "msg");
  std::ofstream(root / "geometry_msgs" / "msg" / "Point.msg")
      << "float64 x\nfloat64 y\nfloat64 z\n";
  CdrWriter writer;
  writer.write_f64(4);
  writer.write_f64(5);
  writer.write_f64(6);
  DynamicValue value;
  std::string error;
  const bool read = read_defined_message(
      "demo_msgs/msg/Sample", "ros2msg", "geometry_msgs/Point origin\n",
      {root.string()}, writer.bytes().data(), writer.bytes().size(), &value, &error);
  std::filesystem::remove_all(root);
  ASSERT_TRUE(read) << error;
  const DynamicValue* y = find_path(value, "origin.y", &error);
  ASSERT_NE(y, nullptr) << error;
  EXPECT_DOUBLE_EQ(y->float_value, 5);
}

TEST(DynamicMessage, RejectsRos2IdlAndAMissingDefinition) {
  DynamicValue value;
  std::string error;
  const uint8_t bytes[] = {0x00, 0x01, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00};
  EXPECT_FALSE(read_defined_message("demo_msgs/msg/Sample", "ros2idl",
                                    "struct Sample { uint32 value; };", {}, bytes,
                                    sizeof(bytes), &value, &error));
  EXPECT_NE(error.find("unsupported message definition encoding: ros2idl"),
            std::string::npos);

  EXPECT_FALSE(read_defined_message("demo_msgs/msg/Sample", "", "", {}, bytes,
                                    sizeof(bytes), &value, &error));
  EXPECT_NE(error.find("no message definition for demo_msgs/msg/Sample"),
            std::string::npos);

  const ResolvedDefinition idl = resolve_message_definition(
      "demo_msgs/msg/Sample", std::nullopt, std::optional<std::string>("ros2idl"),
      {});
  EXPECT_EQ(idl.kind, ResolvedDefinition::Kind::kUnsupportedEncoding);

  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("rrd-convert-badmsg-" + std::to_string(::getpid()));
  std::filesystem::create_directories(root / "demo_msgs" / "msg");
  std::ofstream(root / "demo_msgs" / "msg" / "Sample.msg") << "not a field\n";
  const ResolvedDefinition invalid = resolve_message_definition(
      "demo_msgs/msg/Sample", std::nullopt, std::nullopt, {root.string()});
  std::filesystem::remove_all(root);
  EXPECT_EQ(invalid.kind, ResolvedDefinition::Kind::kInvalid);
  EXPECT_NE(invalid.error.find("invalid field line"), std::string::npos);
}

}  // namespace
}  // namespace rrd_convert
