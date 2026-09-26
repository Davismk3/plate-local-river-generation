cmake_minimum_required(VERSION 3.16)
project(sphere_finite VERSION 1.0.0 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FINITE_WORLD_BUILD_TESTS "Build the test executable" ON)

add_library(finite_world
    src/noise.cpp
    src/terrain.cpp
    src/rivers.cpp
    src/render.cpp
)
target_include_directories(finite_world PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(finite_world PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(finite_world PRIVATE /W4 /permissive-)
else()
    target_compile_options(finite_world PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE finite_world)

if(APPLE)
    enable_language(OBJCXX)
    target_sources(${PROJECT_NAME} PRIVATE src/viewer_macos.mm)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Cocoa")
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
else()
    target_sources(${PROJECT_NAME} PRIVATE src/viewer_stub.cpp)
endif()

if(FINITE_WORLD_BUILD_TESTS)
    enable_testing()
    add_executable(finite_world_tests tests/test_finite_world.cpp)
    target_link_libraries(finite_world_tests PRIVATE finite_world)
    add_test(NAME finite_world_tests COMMAND finite_world_tests)
endif()
