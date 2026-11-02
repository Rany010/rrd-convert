#include "core/conversion_summary.hpp"

#include <string>
#include <utility>

namespace rrd_convert {
namespace {

void add_count(SummaryRow* row, SummaryOutcome outcome) {
  switch (outcome) {
    case SummaryOutcome::kOk:
      ++row->ok_count;
      break;
    case SummaryOutcome::kSkip:
      ++row->skip_count;
      break;
    case SummaryOutcome::kError:
      ++row->error_count;
      break;
  }
}

void append_json_string(std::string* out, const std::string& text) {
  out->push_back('"');
  for (char ch : text) {
    const unsigned char c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"':
        out->append("\\\"");
        break;
      case '\\':
        out->append("\\\\");
        break;
      case '\n':
        out->append("\\n");
        break;
      case '\r':
        out->append("\\r");
        break;
      case '\t':
        out->append("\\t");
        break;
      default:
        if (c < 0x20) {
          constexpr char kHex[] = "0123456789abcdef";
          out->append("\\u00");
          out->push_back(kHex[c >> 4]);
          out->push_back(kHex[c & 0x0f]);
        } else {
          out->push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out->push_back('"');
}

int total(const std::vector<SummaryRow>& rows, int SummaryRow::*field) {
  int sum = 0;
  for (const SummaryRow& row : rows) {
    sum += row.*field;
  }
  return sum;
}

}  // namespace

void ConversionSummary::record(const std::string& topic,
                               const std::string& type_name,
                               SummaryOutcome outcome,
                               const std::string& reason) {
  for (SummaryRow& row : rows_) {
    if (row.topic == topic && row.type_name == type_name &&
        row.reason == reason) {
      add_count(&row, outcome);
      return;
    }
  }
  SummaryRow row;
  row.topic = topic;
  row.type_name = type_name;
  row.reason = reason;
  add_count(&row, outcome);
  rows_.push_back(std::move(row));
}

int ConversionSummary::ok_count() const { return total(rows_, &SummaryRow::ok_count); }

int ConversionSummary::skip_count() const {
  return total(rows_, &SummaryRow::skip_count);
}

int ConversionSummary::error_count() const {
  return total(rows_, &SummaryRow::error_count);
}

const std::vector<SummaryRow>& ConversionSummary::rows() const { return rows_; }

std::string ConversionSummary::to_text() const {
  std::string out;
  for (const SummaryRow& row : rows_) {
    out += "topic=" + row.topic + " type=" + row.type_name;
    if (!row.reason.empty()) {
      out += " reason=" + row.reason;
    }
    out += " ok=" + std::to_string(row.ok_count);
    out += " skipped=" + std::to_string(row.skip_count);
    out += " failed=" + std::to_string(row.error_count);
    out.push_back('\n');
  }
  out += "total ok=" + std::to_string(ok_count());
  out += " skipped=" + std::to_string(skip_count());
  out += " failed=" + std::to_string(error_count());
  out.push_back('\n');
  return out;
}

std::string ConversionSummary::to_json() const {
  std::string out = "{\"ok\":" + std::to_string(ok_count()) +
                    ",\"skipped\":" + std::to_string(skip_count()) +
                    ",\"failed\":" + std::to_string(error_count()) +
                    ",\"rows\":[";
  bool first = true;
  for (const SummaryRow& row : rows_) {
    if (!first) {
      out.push_back(',');
    }
    first = false;
    out += "{\"topic\":";
    append_json_string(&out, row.topic);
    out += ",\"type\":";
    append_json_string(&out, row.type_name);
    out += ",\"reason\":";
    append_json_string(&out, row.reason);
    out += ",\"ok\":" + std::to_string(row.ok_count);
    out += ",\"skipped\":" + std::to_string(row.skip_count);
    out += ",\"failed\":" + std::to_string(row.error_count);
    out.push_back('}');
  }
  out += "]}";
  return out;
}

}  // namespace rrd_convert
