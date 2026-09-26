cmake_minimum_required(VERSION 3.16)
project(flat_infinite_worldgen VERSION 1.0.0 LANGUAGES CXX)

option(FLAT_INFINITE_BUILD_TESTS "Build the test executable" ON)

add_library(flat_infinite
    src/noise.cpp
    src/geometry.cpp
    src/terrain.cpp
    src/rivers.cpp
)
target_include_directories(flat_infinite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(flat_infinite PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(flat_infinite PRIVATE /W4 /permissive-)
else()
    target_compile_options(flat_infinite PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(flat_infinite_cli src/main.cpp)
target_link_libraries(flat_infinite_cli PRIVATE flat_infinite)

if(APPLE)
    enable_language(OBJCXX)
    target_sources(flat_infinite_cli PRIVATE src/visualizer_macos.mm)
    target_link_libraries(flat_infinite_cli PRIVATE "-framework Cocoa")
    target_compile_options(flat_infinite_cli PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
else()
    target_sources(flat_infinite_cli PRIVATE src/visualizer_stub.cpp)
endif()

find_package(ZLIB)
if(ZLIB_FOUND)
    # Paper figure and metrics exporter (see metrics/README.md).
    add_executable(flat_infinite_export tools/export_figure.cpp)
    target_link_libraries(flat_infinite_export PRIVATE flat_infinite ZLIB::ZLIB)
endif()

if(FLAT_INFINITE_BUILD_TESTS)
    enable_testing()
    add_executable(flat_infinite_tests tests/test_flat_infinite.cpp)
    target_link_libraries(flat_infinite_tests PRIVATE flat_infinite)
    add_test(NAME flat_infinite_tests COMMAND flat_infinite_tests)
endif()
