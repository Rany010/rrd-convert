#include "bag_fixture.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: make_fixture_bag <output.db3>\n";
    return 2;
  }
  const std::string error = rrd_convert::write_sample_fixture(argv[1]);
  if (!error.empty()) {
    std::cerr << error << "\n";
    return 1;
  }
  return 0;
}
