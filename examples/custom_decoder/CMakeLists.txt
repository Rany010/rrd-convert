cmake_minimum_required(VERSION 3.16)

project(custom_decoder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Installed converter package:
#   cmake --install <rrd-convert-build> --prefix /opt/rrd-convert
#   cmake -S examples/custom_decoder -B build \
#     -DCMAKE_PREFIX_PATH=/opt/rrd-convert
#   cmake --build build
# The link needs the system SQLite library (libsqlite3-dev).
find_package(rrd_convert CONFIG REQUIRED)

add_executable(custom-decoder main.cpp)
target_link_libraries(custom-decoder PRIVATE rrd_convert::app)
target_compile_options(custom-decoder PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
