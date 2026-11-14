cmake_minimum_required(VERSION 3.16)
project(jsonser VERSION 0.1.0 LANGUAGES CXX)

set(JSONSER_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(JSONSER_TOP_LEVEL ON)
endif()

option(JSONSER_BUILD_TESTS "Build jsonser tests" ${JSONSER_TOP_LEVEL})
option(JSONSER_BUILD_EXAMPLES "Build jsonser examples and the demo app" ${JSONSER_TOP_LEVEL})
option(JSONSER_INSTALL "Generate install rules (find_package(jsonser))" ${JSONSER_TOP_LEVEL})

# The one dependency: nlohmann/json (used for reading). Get it from vcpkg - vcpkg.json lists it,
# and the vcpkg toolchain (see CMakePresets.json) installs it automatically.
find_package(nlohmann_json 3.11 CONFIG REQUIRED)

add_library(jsonser INTERFACE)
add_library(jsonser::jsonser ALIAS jsonser)
target_include_directories(jsonser INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(jsonser INTERFACE cxx_std_17)
target_link_libraries(jsonser INTERFACE nlohmann_json::nlohmann_json)
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

if(JSONSER_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(TARGETS jsonser EXPORT jsonserTargets)
  install(EXPORT jsonserTargets NAMESPACE jsonser:: DESTINATION ${CMAKE_INSTALL_DATADIR}/jsonser)
  configure_package_config_file(cmake/jsonserConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/jsonserConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/jsonser)
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/jsonserConfigVersion.cmake
    COMPATIBILITY SameMinorVersion ARCH_INDEPENDENT)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/jsonserConfig.cmake
                ${CMAKE_CURRENT_BINARY_DIR}/jsonserConfigVersion.cmake
          DESTINATION ${CMAKE_INSTALL_DATADIR}/jsonser)
  install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/jsonser RENAME copyright)
endif()
