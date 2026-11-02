#ifndef RRD_CONVERT_SRC_CORE_CONVERSION_SUMMARY_HPP_
#define RRD_CONVERT_SRC_CORE_CONVERSION_SUMMARY_HPP_

#include <string>
#include <vector>

namespace rrd_convert {

enum class SummaryOutcome { kOk, kSkip, kError };

struct SummaryRow {
  std::string topic;
  std::string type_name;
  std::string reason;
  int ok_count = 0;
  int skip_count = 0;
  int error_count = 0;
};

// Counts grouped by topic, message type, and reason.
class ConversionSummary {
 public:
  void record(const std::string& topic, const std::string& type_name,
              SummaryOutcome outcome, const std::string& reason);

  int ok_count() const;
  int skip_count() const;
  int error_count() const;
  const std::vector<SummaryRow>& rows() const;

  std::string to_text() const;
  std::string to_json() const;

 private:
  std::vector<SummaryRow> rows_;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_CORE_CONVERSION_SUMMARY_HPP_
