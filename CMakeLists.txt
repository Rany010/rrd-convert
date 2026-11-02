cmake_minimum_required(VERSION 3.16)

project(rrd_convert VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

function(rrd_convert_set_warnings target)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
endfunction()

add_library(core
  src/core/status.cpp
  src/core/format_adapter.cpp
  src/core/decoder.cpp
  src/core/registry.cpp
  src/core/conversion_summary.cpp
)
target_include_directories(core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(core PUBLIC cxx_std_17)
rrd_convert_set_warnings(core)

add_library(cdr
  src/cdr/cdr_reader.cpp
  src/cdr/msg_parser.cpp
  src/cdr/dynamic_message.cpp
)
target_include_directories(cdr PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
  target_link_libraries(cdr PUBLIC stdc++fs)
endif()
rrd_convert_set_warnings(cdr)

find_package(SQLite3 REQUIRED)
add_library(format_db3
  src/format/ros2_db3_adapter.cpp
)
target_link_libraries(format_db3 PUBLIC
  core
  $<BUILD_INTERFACE:SQLite::SQLite3>
  $<INSTALL_INTERFACE:sqlite3>
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
  target_link_libraries(format_db3 PUBLIC stdc++fs)
endif()
rrd_convert_set_warnings(format_db3)

add_library(decode_builtin
  src/decode/image_decoder.cpp
  src/decode/compressed_image_decoder.cpp
  src/decode/compressed_video_decoder.cpp
  src/decode/point_cloud_decoder.cpp
  src/decode/tf_decoder.cpp
  src/decode/marker_decoder.cpp
  src/decode/register_decoders.cpp
)
target_include_directories(decode_builtin PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(decode_builtin PUBLIC core cdr)
rrd_convert_set_warnings(decode_builtin)

set(RRD_CONVERT_RERUN_URL
  "https://github.com/rerun-io/rerun/releases/download/0.28.2/rerun_cpp_sdk.zip"
  CACHE STRING "Rerun C++ SDK 0.28.2 archive URL or local zip path")
set(_rrd_rerun_url "${RRD_CONVERT_RERUN_URL}")
if(_rrd_rerun_url MATCHES "^https?://")
  set(_rrd_rerun_zip
    "${CMAKE_BINARY_DIR}/downloads/rerun_cpp_sdk-0.28.2.zip")
  if(NOT EXISTS "${_rrd_rerun_zip}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/downloads")
    find_program(RRD_CONVERT_CURL curl)
    if(RRD_CONVERT_CURL)
      message(STATUS "Downloading Rerun C++ SDK 0.28.2 with curl")
      execute_process(
        COMMAND "${RRD_CONVERT_CURL}" -L --retry 3 --retry-delay 2 --fail
                -o "${_rrd_rerun_zip}" "${_rrd_rerun_url}"
        RESULT_VARIABLE _rrd_rerun_curl_status)
      if(NOT _rrd_rerun_curl_status EQUAL 0)
        message(WARNING
          "curl could not download the Rerun SDK; FetchContent will try the URL")
        file(REMOVE "${_rrd_rerun_zip}")
      endif()
    endif()
  endif()
  if(EXISTS "${_rrd_rerun_zip}")
    set(_rrd_rerun_url "${_rrd_rerun_zip}")
  endif()
endif()
include(FetchContent)
FetchContent_Declare(
  rerun_sdk
  URL "${_rrd_rerun_url}"
)
# The SDK registers its own install() rules. They write a CMake package that
# calls find_dependency(Arrow) and records this build's library path. Plugin
# builds link the static archives copied by the install script instead.
macro(install)
  if(CMAKE_CURRENT_SOURCE_DIR MATCHES "/_deps/rerun_sdk-src(/|$)")
    return()
  endif()
  _install(${ARGV})
endmacro()
FetchContent_MakeAvailable(rerun_sdk)
get_target_property(_rrd_rerun_includes rerun_sdk INTERFACE_INCLUDE_DIRECTORIES)
if(_rrd_rerun_includes)
  set_property(TARGET rerun_sdk PROPERTY
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_rrd_rerun_includes}")
endif()

add_library(sink_rerun
  src/sink/rerun_sink.cpp
  src/sink/root_frame.cpp
)
target_include_directories(sink_rerun PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(sink_rerun PUBLIC
  core
  $<BUILD_INTERFACE:rerun_sdk>
)
rrd_convert_set_warnings(sink_rerun)

add_library(app
  src/app/run_cli.cpp
  src/app/convert_app.cpp
  src/app/register_builtins.cpp
)
target_include_directories(app PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(app PUBLIC core format_db3 decode_builtin sink_rerun)
# GCC 9 keeps std::filesystem in a separate library.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
  target_link_libraries(app PUBLIC stdc++fs)
endif()
rrd_convert_set_warnings(app)

add_executable(rrd-convert src/main.cpp)
target_link_libraries(rrd-convert PRIVATE app)
rrd_convert_set_warnings(rrd-convert)

# Arrow is linked through a directory-local alias, so it is not a target in
# this file. The install script copies every external static archive on the
# executable link line and records file names only.
configure_file(
  cmake/rrd_convertInstallPluginLibs.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/rrd_convertInstallPluginLibs.cmake"
  @ONLY)

install(TARGETS core cdr format_db3 decode_builtin sink_rerun app
  EXPORT rrd_convertTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS rrd-convert RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY src/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp")

include(CMakePackageConfigHelpers)
configure_package_config_file(
  cmake/rrd_convertConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/rrd_convertConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rrd_convert)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/rrd_convertConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/rrd_convertConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/rrd_convertConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rrd_convert)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/rrd_convertInstallPluginLibs.cmake")
install(EXPORT rrd_convertTargets
  NAMESPACE rrd_convert::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rrd_convert)

option(RRD_CONVERT_BUILD_TESTS "Build unit tests" ON)

if(RRD_CONVERT_BUILD_TESTS)
include(FetchContent)
set(RRD_CONVERT_GTEST_URL
  "https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"
  CACHE STRING "GoogleTest 1.14.0 archive URL or local tarball path")
# CMake's own downloader can time out against GitHub. Prefer curl, then
# hand the local archive to FetchContent.
set(_rrd_gtest_url "${RRD_CONVERT_GTEST_URL}")
if(_rrd_gtest_url MATCHES "^https?://")
  set(_rrd_gtest_tarball
    "${CMAKE_BINARY_DIR}/downloads/googletest-1.14.0.tar.gz")
  if(NOT EXISTS "${_rrd_gtest_tarball}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/downloads")
    find_program(RRD_CONVERT_CURL curl)
    if(RRD_CONVERT_CURL)
      message(STATUS "Downloading GoogleTest with curl")
      execute_process(
        COMMAND "${RRD_CONVERT_CURL}" -L --retry 3 --retry-delay 2 --fail
                -o "${_rrd_gtest_tarball}" "${_rrd_gtest_url}"
        RESULT_VARIABLE _rrd_curl_status)
      if(NOT _rrd_curl_status EQUAL 0)
        message(WARNING
          "curl could not download GoogleTest; FetchContent will try the URL")
        file(REMOVE "${_rrd_gtest_tarball}")
      endif()
    endif()
  endif()
  if(EXISTS "${_rrd_gtest_tarball}")
    set(_rrd_gtest_url "${_rrd_gtest_tarball}")
  endif()
endif()
FetchContent_Declare(
  googletest
  URL "${_rrd_gtest_url}"
  TIMEOUT 60
)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_library(bag_fixture
  tests/bag_fixture.cpp
  tests/cdr_writer.cpp
)
target_include_directories(bag_fixture PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(bag_fixture PUBLIC cdr SQLite::SQLite3)
rrd_convert_set_warnings(bag_fixture)

add_executable(make_fixture_bag tests/make_fixture_bag.cpp)
target_link_libraries(make_fixture_bag PRIVATE bag_fixture)
rrd_convert_set_warnings(make_fixture_bag)

add_executable(rrd_convert_tests
  tests/cli_test.cpp
  tests/core_test.cpp
  tests/cdr_reader_test.cpp
  tests/dynamic_message_test.cpp
  tests/db3_adapter_test.cpp
  tests/convert_test.cpp
  tests/decoders_test.cpp
  tests/media_decoders_test.cpp
  tests/rerun_sink_test.cpp
)
target_include_directories(rrd_convert_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_link_libraries(rrd_convert_tests PRIVATE app cdr bag_fixture GTest::gtest_main)
rrd_convert_set_warnings(rrd_convert_tests)
include(GoogleTest)
gtest_discover_tests(rrd_convert_tests)
endif()
