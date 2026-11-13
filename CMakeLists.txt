cmake_minimum_required(VERSION 3.16)
project(jsonser VERSION 0.1.0 LANGUAGES CXX)

set(JSONSER_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(JSONSER_TOP_LEVEL ON)
endif()

option(JSONSER_BUILD_TESTS "Build jsonser tests" ${JSONSER_TOP_LEVEL})
option(JSONSER_BUILD_EXAMPLES "Build jsonser examples and the demo app" ${JSONSER_TOP_LEVEL})
option(JSONSER_USE_SYSTEM_NLOHMANN "Use an installed nlohmann_json instead of the vendored copy" OFF)

add_library(jsonser INTERFACE)
add_library(jsonser::jsonser ALIAS jsonser)
target_include_directories(jsonser INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(jsonser INTERFACE cxx_std_17)

if(JSONSER_USE_SYSTEM_NLOHMANN)
  find_package(nlohmann_json 3.10 REQUIRED)
  target_link_libraries(jsonser INTERFACE nlohmann_json::nlohmann_json)
else()
  target_include_directories(jsonser INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>)
endif()

if(MSVC)
  target_compile_options(jsonser INTERFACE /utf-8 /bigobj)
endif()

# Warnings for our own targets only.
function(jsonser_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  if(MINGW)
    # Don't depend on whichever libstdc++-6.dll happens to be first on PATH.
    target_link_options(${target} PRIVATE -static)
  endif()
endfunction()

if(JSONSER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(JSONSER_BUILD_EXAMPLES)
  add_subdirectory(examples)
  add_subdirectory(apps/jsonser_demo)
endif()
