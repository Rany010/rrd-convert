#include "core/status.hpp"

#include <utility>

namespace rrd_convert {

Status::Status(bool ok, std::string path, std::string reason)
    : ok_(ok), path_(std::move(path)), reason_(std::move(reason)) {}

Status Status::Ok() { return Status(true, {}, {}); }

Status Status::Error(std::string path, std::string reason) {
  return Status(false, std::move(path), std::move(reason));
}

bool Status::ok() const { return ok_; }

const std::string& Status::path() const { return path_; }

const std::string& Status::reason() const { return reason_; }

std::string Status::message() const {
  if (ok_ || (path_.empty() && reason_.empty())) {
    return {};
  }
  if (path_.empty()) {
    return reason_;
  }
  if (reason_.empty()) {
    return path_;
  }
  return path_ + ": " + reason_;
}

}  // namespace rrd_convert
