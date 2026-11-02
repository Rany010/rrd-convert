#ifndef RRD_CONVERT_SRC_APP_RUN_CLI_HPP_
#define RRD_CONVERT_SRC_APP_RUN_CLI_HPP_

#include "app/convert_app.hpp"

#include <functional>
#include <iosfwd>
#include <string>

namespace rrd_convert {

struct ArgParseResult {
  bool ok = false;
  int exit_code = kExitUsage;
  ConvertOptions options;
  std::string message;
};

using RegisterFn = std::function<void(Registry&)>;

ArgParseResult parse_args(int argc, char** argv);

// extra runs after the built-in registration. out and err default to
// stdout and stderr.
int run_cli(int argc, char** argv, RegisterFn extra = {},
            std::ostream* out = nullptr, std::ostream* err = nullptr);

}  // namespace rrd_convert

#endif  // RRD_CONVERT_SRC_APP_RUN_CLI_HPP_
