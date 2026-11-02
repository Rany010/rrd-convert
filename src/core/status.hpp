#ifndef RRD_CONVERT_SRC_CORE_STATUS_HPP_
#define RRD_CONVERT_SRC_CORE_STATUS_HPP_

#include <string>

namespace rrd_convert {

// Success, or a failure that names the path and the reason.
class Status {
 public:
  static Status Ok();
  static Status Error(std::string path, std::string reason);

  bool ok() const;
  const std::string& path() const;
  const std::string& reason() const;
  std::string message() const;

 private:
  Status(bool ok, std::string path, std::string reason);

  bool ok_;
  std::string path_;
  std::string reason_;
};

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_CORE_STATUS_HPP_
