#include "bag_fixture.hpp"

#include "cdr_writer.hpp"

#include <sqlite3.h>

#include <cstdio>
#include <map>
#include <utility>

namespace rrd_convert {
namespace {

std::string exec_sql(sqlite3* db, const char* sql) {
  char* error = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
  if (rc == SQLITE_OK) {
    return {};
  }
  std::string message = error != nullptr ? error : sqlite3_errmsg(db);
  sqlite3_free(error);
  return message;
}

}  // namespace

std::string write_ros2_db3(const std::string& path,
                           const std::vector<FixtureMessage>& messages,
                           const std::vector<FixtureDefinition>& definitions,
                           bool include_definition_table) {
  std::remove(path.c_str());
  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    std::string message = db != nullptr ? sqlite3_errmsg(db) : "sqlite open failed";
    sqlite3_close(db);
    return path + ": " + message;
  }

  std::string error = exec_sql(
      db,
      "CREATE TABLE topics("
      "id INTEGER PRIMARY KEY,"
      "name TEXT NOT NULL,"
      "type TEXT NOT NULL,"
      "serialization_format TEXT NOT NULL);");
  if (error.empty()) {
    error = exec_sql(
        db,
        "CREATE TABLE messages("
        "id INTEGER PRIMARY KEY,"
        "topic_id INTEGER NOT NULL,"
        "timestamp INTEGER NOT NULL,"
        "data BLOB NOT NULL);");
  }
  if (error.empty() && include_definition_table) {
    error = exec_sql(
        db,
        "CREATE TABLE message_definitions("
        "topic_type TEXT NOT NULL,"
        "encoding TEXT NOT NULL,"
        "encoded_message_definition TEXT NOT NULL);");
  }
  if (!error.empty()) {
    sqlite3_close(db);
    return path + ": " + error;
  }

  sqlite3_stmt* insert_topic = nullptr;
  sqlite3_prepare_v2(
      db, "INSERT INTO topics(name, type, serialization_format) VALUES(?,?,?)",
      -1, &insert_topic, nullptr);
  sqlite3_stmt* insert_message = nullptr;
  sqlite3_prepare_v2(
      db, "INSERT INTO messages(topic_id, timestamp, data) VALUES(?,?,?)", -1,
      &insert_message, nullptr);
  sqlite3_stmt* insert_definition = nullptr;
  if (include_definition_table) {
    sqlite3_prepare_v2(
        db,
        "INSERT INTO message_definitions("
        "topic_type, encoding, encoded_message_definition) VALUES(?,?,?)",
        -1, &insert_definition, nullptr);
  }

  std::map<std::string, int> topic_ids;
  for (const FixtureMessage& message : messages) {
    if (topic_ids.find(message.topic) == topic_ids.end()) {
      sqlite3_bind_text(insert_topic, 1, message.topic.c_str(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(insert_topic, 2, message.type_name.c_str(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(insert_topic, 3, message.serialization.c_str(), -1,
                        SQLITE_TRANSIENT);
      if (sqlite3_step(insert_topic) != SQLITE_DONE) {
        error = sqlite3_errmsg(db);
        break;
      }
      topic_ids[message.topic] =
          static_cast<int>(sqlite3_last_insert_rowid(db));
      sqlite3_reset(insert_topic);
      sqlite3_clear_bindings(insert_topic);
    }
    if (!error.empty()) {
      break;
    }
    sqlite3_bind_int(insert_message, 1, topic_ids[message.topic]);
    sqlite3_bind_int64(insert_message, 2, message.log_time_ns);
    const int size = static_cast<int>(message.bytes.size());
    sqlite3_bind_blob(insert_message, 3,
                      message.bytes.empty() ? nullptr : message.bytes.data(),
                      size, SQLITE_TRANSIENT);
    if (sqlite3_step(insert_message) != SQLITE_DONE) {
      error = sqlite3_errmsg(db);
      break;
    }
    sqlite3_reset(insert_message);
    sqlite3_clear_bindings(insert_message);
  }

  if (error.empty() && insert_definition != nullptr) {
    for (const FixtureDefinition& definition : definitions) {
      sqlite3_bind_text(insert_definition, 1, definition.type_name.c_str(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(insert_definition, 2, definition.encoding.c_str(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(insert_definition, 3, definition.text.c_str(), -1,
                        SQLITE_TRANSIENT);
      if (sqlite3_step(insert_definition) != SQLITE_DONE) {
        error = sqlite3_errmsg(db);
        break;
      }
      sqlite3_reset(insert_definition);
      sqlite3_clear_bindings(insert_definition);
    }
  }

  sqlite3_finalize(insert_topic);
  sqlite3_finalize(insert_message);
  sqlite3_finalize(insert_definition);
  sqlite3_close(db);
  if (!error.empty()) {
    return path + ": " + error;
  }
  return {};
}

std::string write_sample_fixture(const std::string& path) {
  CdrWriter chatter;
  chatter.write_string("hello");
  CdrWriter sample;
  sample.write_u32(7);

  const std::vector<FixtureMessage> messages = {
      {"/chatter", "std_msgs/msg/String", "cdr", 1000, chatter.bytes()},
      {"/sample", "demo_msgs/msg/Sample", "cdr", 2000, sample.bytes()},
  };
  const std::vector<FixtureDefinition> definitions = {
      {"demo_msgs/msg/Sample", "ros2msg", "uint32 value\n"},
  };
  return write_ros2_db3(path, messages, definitions, true);
}

}  // namespace rrd_convert
