#include "format/ros2_db3_adapter.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

namespace rrd_convert {
namespace {

std::string trim_copy(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                         text[end - 1] == '\r')) {
    --end;
  }
  return text.substr(begin, end - begin);
}

// Reads a plain YAML scalar. Quoted strings stop at the closing quote.
std::string yaml_scalar(const std::string& text) {
  if (text.empty()) {
    return {};
  }
  if (text.front() == '"' || text.front() == '\'') {
    const char quote = text.front();
    const std::size_t close = text.find(quote, 1);
    if (close == std::string::npos) {
      return text.substr(1);
    }
    return text.substr(1, close - 1);
  }
  std::string value;
  for (char ch : text) {
    if (ch == '#' || ch == ' ' || ch == '\t') {
      break;
    }
    value.push_back(ch);
  }
  return value;
}

bool compression_value_is_set(const std::string& value) {
  return !value.empty() && value != "~" && value != "null" && value != "Null" &&
         value != "NULL";
}

bool table_exists(sqlite3* db, const std::string& name) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(
          db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", -1,
          &stmt, nullptr) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return false;
  }
  sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
  const bool found = sqlite3_step(stmt) == SQLITE_ROW;
  sqlite3_finalize(stmt);
  return found;
}

bool has_column(sqlite3* db, const std::string& table, const std::string& column) {
  const std::string sql = "PRAGMA table_info(" + table + ")";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  bool found = false;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const unsigned char* text = sqlite3_column_text(stmt, 1);
    if (text != nullptr && column == reinterpret_cast<const char*>(text)) {
      found = true;
      break;
    }
  }
  sqlite3_finalize(stmt);
  return found;
}

std::string require_columns(sqlite3* db, const std::string& table,
                            const std::vector<std::string>& columns) {
  if (!table_exists(db, table)) {
    return "unrecognized rosbag2 table structure: missing " + table + " table";
  }
  for (const std::string& column : columns) {
    if (!has_column(db, table, column)) {
      return "unrecognized rosbag2 table structure: " + table + "." + column +
             " is missing";
    }
  }
  return {};
}

}  // namespace

struct Ros2Db3Adapter::DefinitionSet {
  // encoding -> text, per message type.
  std::map<std::string, std::map<std::string, std::string>> by_type;
};

struct Ros2Db3Adapter::Shard {
  sqlite3* db = nullptr;
  sqlite3_stmt* stmt = nullptr;
  std::string path;
  bool has_row = false;
  int64_t timestamp = 0;
  int64_t message_id = 0;
  int64_t topic_id = 0;
  std::unordered_map<int64_t, TopicInfo> topics;

  ~Shard() {
    if (stmt != nullptr) {
      sqlite3_finalize(stmt);
    }
    if (db != nullptr) {
      sqlite3_close(db);
    }
  }

  Shard(const Shard&) = delete;
  Shard& operator=(const Shard&) = delete;
  Shard() = default;

  bool pull(std::string* error) {
    if (stmt == nullptr) {
      has_row = false;
      return true;
    }
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
      has_row = false;
      return true;
    }
    if (rc != SQLITE_ROW) {
      *error = sqlite3_errmsg(db);
      has_row = false;
      return false;
    }
    has_row = true;
    message_id = sqlite3_column_int64(stmt, 0);
    timestamp = sqlite3_column_int64(stmt, 1);
    topic_id = sqlite3_column_int64(stmt, 2);
    return true;
  }
};

Ros2Db3Adapter::Ros2Db3Adapter()
    : definitions_(std::make_unique<DefinitionSet>()) {}

Ros2Db3Adapter::~Ros2Db3Adapter() { close(); }

bool Ros2Db3Adapter::matches(const std::string& path) {
  std::error_code error;
  if (std::filesystem::is_directory(path, error)) {
    return true;
  }
  const std::string extension = std::filesystem::path(path).extension().string();
  return extension == ".db3" || extension == ".zstd";
}

void Ros2Db3Adapter::close() {
  shards_.clear();
  topics_.clear();
  topics_by_name_.clear();
  pending_error_.clear();
  if (definitions_ != nullptr) {
    definitions_->by_type.clear();
  }
  opened_ = false;
}

Status Ros2Db3Adapter::open(const std::string& path, const TopicFilter& filter) {
  close();
  input_path_ = path;
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    const std::string reason = error ? error.message() : "input not found";
    return Status::Error(path, reason);
  }
  if (std::filesystem::is_directory(path, error)) {
    return open_directory(path, filter);
  }
  if (std::filesystem::path(path).extension() == ".zstd") {
    return Status::Error(path, "compressed bags are not supported");
  }
  if (std::filesystem::path(path).extension() != ".db3") {
    return Status::Error(path, "not a .db3 file");
  }
  const Status status = open_file(path, filter);
  if (!status.ok()) {
    close();
    return status;
  }
  opened_ = true;
  return Status::Ok();
}

Status Ros2Db3Adapter::open_directory(const std::string& path,
                                      const TopicFilter& filter) {
  std::error_code error;
  for (std::filesystem::directory_iterator it(path, error), end;
       !error && it != end; it.increment(error)) {
    std::error_code file_error;
    if (!it->is_regular_file(file_error)) {
      continue;
    }
    if (it->path().extension() == ".zstd") {
      return Status::Error(path, "compressed bags are not supported: " +
                                     it->path().filename().string());
    }
  }
  if (error) {
    return Status::Error(path, "failed to read bag directory: " + error.message());
  }

  const std::filesystem::path metadata = std::filesystem::path(path) / "metadata.yaml";
  if (std::filesystem::is_regular_file(metadata)) {
    std::ifstream input(metadata);
    if (!input) {
      return Status::Error(path, "failed to read metadata.yaml");
    }
    std::string line;
    while (std::getline(input, line)) {
      const std::string trimmed = trim_copy(line);
      const std::string key = "compression_format:";
      if (trimmed.empty() || trimmed[0] == '#' ||
          trimmed.compare(0, key.size(), key) != 0) {
        continue;
      }
      const std::string value = yaml_scalar(trim_copy(trimmed.substr(key.size())));
      if (compression_value_is_set(value)) {
        return Status::Error(path, "compressed bags are not supported: compression_format is " +
                                       value);
      }
    }
  }

  std::vector<std::filesystem::path> files;
  for (std::filesystem::directory_iterator it(path, error), end;
       !error && it != end; it.increment(error)) {
    std::error_code file_error;
    if (it->is_regular_file(file_error) && it->path().extension() == ".db3") {
      files.push_back(it->path());
    }
  }
  if (error) {
    return Status::Error(path, "failed to read bag directory: " + error.message());
  }
  std::sort(files.begin(), files.end());
  if (files.empty()) {
    return Status::Error(path, "no .db3 files in bag directory");
  }
  for (const std::filesystem::path& file : files) {
    const Status status = open_file(file.string(), filter);
    if (!status.ok()) {
      close();
      return status;
    }
  }
  opened_ = true;
  return Status::Ok();
}

Status Ros2Db3Adapter::open_file(const std::string& path,
                                 const TopicFilter& filter) {
  auto shard = std::make_unique<Shard>();
  shard->path = path;
  const int open_rc =
      sqlite3_open_v2(path.c_str(), &shard->db, SQLITE_OPEN_READONLY, nullptr);
  if (open_rc != SQLITE_OK) {
    const std::string reason =
        shard->db != nullptr ? sqlite3_errmsg(shard->db) : "sqlite open failed";
    return Status::Error(path, std::string("not a sqlite database: ") + reason);
  }
  sqlite3_stmt* probe = nullptr;
  if (sqlite3_prepare_v2(shard->db, "SELECT 1 FROM sqlite_master LIMIT 1", -1,
                         &probe, nullptr) != SQLITE_OK) {
    const std::string reason = sqlite3_errmsg(shard->db);
    sqlite3_finalize(probe);
    return Status::Error(path, "not a sqlite database: " + reason);
  }
  sqlite3_finalize(probe);

  std::string structure = require_columns(
      shard->db, "topics", {"id", "name", "type", "serialization_format"});
  if (structure.empty()) {
    structure = require_columns(shard->db, "messages",
                                {"id", "topic_id", "timestamp", "data"});
  }
  if (structure.empty() && table_exists(shard->db, "message_definitions")) {
    structure = require_columns(
        shard->db, "message_definitions",
        {"topic_type", "encoding", "encoded_message_definition"});
  }
  if (!structure.empty()) {
    return Status::Error(path, structure);
  }

  sqlite3_stmt* topic_stmt = nullptr;
  if (sqlite3_prepare_v2(shard->db,
                         "SELECT id, name, \"type\", serialization_format "
                         "FROM topics ORDER BY id ASC",
                         -1, &topic_stmt, nullptr) != SQLITE_OK) {
    return Status::Error(path, sqlite3_errmsg(shard->db));
  }
  std::vector<int64_t> allowed_ids;
  while (sqlite3_step(topic_stmt) == SQLITE_ROW) {
    TopicInfo info;
    const int64_t id = sqlite3_column_int64(topic_stmt, 0);
    const unsigned char* name = sqlite3_column_text(topic_stmt, 1);
    const unsigned char* type = sqlite3_column_text(topic_stmt, 2);
    const unsigned char* serialization = sqlite3_column_text(topic_stmt, 3);
    info.name = name != nullptr ? reinterpret_cast<const char*>(name) : "";
    info.type_name = type != nullptr ? reinterpret_cast<const char*>(type) : "";
    info.serialization = serialization != nullptr
                             ? reinterpret_cast<const char*>(serialization)
                             : "";
    if (!filter.allows(info.name)) {
      continue;
    }
    const auto existing = topics_by_name_.find(info.name);
    if (existing == topics_by_name_.end()) {
      topics_by_name_.emplace(info.name, info);
      topics_.push_back(info);
    } else if (existing->second.type_name != info.type_name ||
               existing->second.serialization != info.serialization) {
      sqlite3_finalize(topic_stmt);
      return Status::Error(input_path_, "conflicting topic metadata for " + info.name);
    }
    shard->topics.emplace(id, info);
    allowed_ids.push_back(id);
  }
  sqlite3_finalize(topic_stmt);

  if (table_exists(shard->db, "message_definitions")) {
    sqlite3_stmt* def_stmt = nullptr;
    if (sqlite3_prepare_v2(
            shard->db,
            "SELECT topic_type, encoding, encoded_message_definition "
            "FROM message_definitions",
            -1, &def_stmt, nullptr) != SQLITE_OK) {
      return Status::Error(path, sqlite3_errmsg(shard->db));
    }
    while (sqlite3_step(def_stmt) == SQLITE_ROW) {
      const unsigned char* type = sqlite3_column_text(def_stmt, 0);
      const unsigned char* encoding = sqlite3_column_text(def_stmt, 1);
      const unsigned char* text = sqlite3_column_text(def_stmt, 2);
      const std::string type_name =
          type != nullptr ? reinterpret_cast<const char*>(type) : "";
      const std::string encoding_name =
          encoding != nullptr ? reinterpret_cast<const char*>(encoding) : "";
      const std::string body =
          text != nullptr ? reinterpret_cast<const char*>(text) : "";
      std::string& slot = definitions_->by_type[type_name][encoding_name];
      if (!slot.empty() && slot != body) {
        sqlite3_finalize(def_stmt);
        return Status::Error(input_path_,
                             "conflicting message definition for " + type_name);
      }
      if (!body.empty()) {
        slot = body;
      }
    }
    sqlite3_finalize(def_stmt);
  }

  if (!allowed_ids.empty()) {
    std::ostringstream sql;
    sql << "SELECT id, timestamp, topic_id, data FROM messages WHERE topic_id IN (";
    for (std::size_t i = 0; i < allowed_ids.size(); ++i) {
      if (i != 0) {
        sql << ",";
      }
      sql << "?";
    }
    sql << ") ORDER BY timestamp ASC, id ASC";
    if (sqlite3_prepare_v2(shard->db, sql.str().c_str(), -1, &shard->stmt,
                           nullptr) != SQLITE_OK) {
      return Status::Error(path, sqlite3_errmsg(shard->db));
    }
    for (std::size_t i = 0; i < allowed_ids.size(); ++i) {
      sqlite3_bind_int64(shard->stmt, static_cast<int>(i + 1), allowed_ids[i]);
    }
    std::string pull_error;
    if (!shard->pull(&pull_error)) {
      return Status::Error(path, pull_error);
    }
  }

  shards_.push_back(std::move(shard));
  return Status::Ok();
}

std::vector<TopicInfo> Ros2Db3Adapter::topics() const { return topics_; }

std::optional<std::string> Ros2Db3Adapter::message_definition(
    const std::string& type_name) const {
  if (definitions_ == nullptr) {
    return std::nullopt;
  }
  const auto found = definitions_->by_type.find(type_name);
  if (found == definitions_->by_type.end()) {
    return std::nullopt;
  }
  const auto ros2msg = found->second.find("ros2msg");
  if (ros2msg != found->second.end() && !ros2msg->second.empty()) {
    return ros2msg->second;
  }
  return std::nullopt;
}

std::optional<std::string> Ros2Db3Adapter::message_definition_encoding(
    const std::string& type_name) const {
  if (definitions_ == nullptr) {
    return std::nullopt;
  }
  const auto found = definitions_->by_type.find(type_name);
  if (found == definitions_->by_type.end()) {
    return std::nullopt;
  }
  const auto ros2msg = found->second.find("ros2msg");
  if (ros2msg != found->second.end() && !ros2msg->second.empty()) {
    return std::string("ros2msg");
  }
  for (const auto& item : found->second) {
    if (!item.second.empty()) {
      return item.first;
    }
  }
  return std::nullopt;
}

ReadResult Ros2Db3Adapter::next(InputRecord* record) {
  if (!opened_) {
    return ReadResult::Error("bag is not open");
  }
  if (!pending_error_.empty()) {
    const std::string error = pending_error_;
    pending_error_.clear();
    return ReadResult::Error(error);
  }
  Shard* chosen = nullptr;
  std::size_t chosen_index = 0;
  for (std::size_t i = 0; i < shards_.size(); ++i) {
    Shard* shard = shards_[i].get();
    if (!shard->has_row) {
      continue;
    }
    if (chosen == nullptr || shard->timestamp < chosen->timestamp ||
        (shard->timestamp == chosen->timestamp && i < chosen_index)) {
      chosen = shard;
      chosen_index = i;
    }
  }
  if (chosen == nullptr) {
    return ReadResult::Done();
  }

  const auto topic = chosen->topics.find(chosen->topic_id);
  const void* blob = sqlite3_column_blob(chosen->stmt, 3);
  const int blob_size = sqlite3_column_bytes(chosen->stmt, 3);
  std::vector<uint8_t> bytes;
  if (blob != nullptr && blob_size > 0) {
    const auto* data = static_cast<const uint8_t*>(blob);
    bytes.assign(data, data + blob_size);
  }
  const int64_t timestamp = chosen->timestamp;
  const std::string shard_path = chosen->path;

  std::string pull_error;
  const bool pulled = chosen->pull(&pull_error);
  if (!pulled) {
    pending_error_ = shard_path + ": " + pull_error;
  }
  if (topic == chosen->topics.end()) {
    return ReadResult::Error("unknown topic id in " + shard_path);
  }

  record->topic = topic->second.name;
  record->type_name = topic->second.type_name;
  record->serialization = topic->second.serialization;
  record->log_time_ns = timestamp;
  record->bytes = std::move(bytes);
  return ReadResult::Record();
}

}  // namespace rrd_convert
