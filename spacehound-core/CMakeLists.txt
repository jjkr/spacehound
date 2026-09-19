cmake_minimum_required(VERSION 3.30)

set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum supported macOS version" FORCE)

project(spacehound VERSION 0.3.0 LANGUAGES CXX OBJCXX)

if(NOT APPLE)
  message(FATAL_ERROR "spacehound currently supports macOS only.")
endif()

include(CTest)
include(FetchContent)
find_package(Git QUIET)

option(SPACEHOUND_BUILD_TESTS "Build spacehound tests." ON)
option(SPACEHOUND_BUILD_EXAMPLES "Build spacehound examples." ON)
option(SPACEHOUND_BUILD_LEGACY_EXAMPLES "Build the legacy C prototype examples." OFF)
option(SPACEHOUND_WARNINGS_AS_ERRORS "Treat compiler warnings as errors." OFF)
option(SPACEHOUND_ENABLE_COVERAGE "Enable LLVM source-based coverage for in-tree targets." OFF)
option(
  SPACEHOUND_ENABLE_LIVE_SYSTEM_TESTS
  "Build opt-in live system tests that can post events to the current process."
  OFF
)

set(SPACEHOUND_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/generated/include")

set(SPACEHOUND_VERSION_BUILD_METADATA "")
set(SPACEHOUND_VERSION_FULL "${PROJECT_VERSION}")
set(SPACEHOUND_VERSION_IS_RELEASE_BUILD_LITERAL true)

if(Git_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse --short=12 HEAD
    OUTPUT_VARIABLE SPACEHOUND_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SPACEHOUND_GIT_SHA_RESULT
  )
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" status --porcelain --untracked-files=no
    OUTPUT_VARIABLE SPACEHOUND_GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SPACEHOUND_GIT_STATUS_RESULT
  )
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" tag --points-at HEAD
    OUTPUT_VARIABLE SPACEHOUND_GIT_TAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SPACEHOUND_GIT_TAGS_RESULT
  )

  if(SPACEHOUND_GIT_SHA_RESULT EQUAL 0 AND NOT SPACEHOUND_GIT_SHA STREQUAL "")
    set(SPACEHOUND_VERSION_IS_RELEASE_BUILD_LITERAL false)

    set(SPACEHOUND_GIT_IS_DIRTY false)
    if(NOT SPACEHOUND_GIT_STATUS_RESULT EQUAL 0 OR NOT SPACEHOUND_GIT_STATUS STREQUAL "")
      set(SPACEHOUND_GIT_IS_DIRTY true)
    endif()

    if(SPACEHOUND_GIT_TAGS_RESULT EQUAL 0)
      string(REPLACE "\n" ";" SPACEHOUND_GIT_TAG_LIST "${SPACEHOUND_GIT_TAGS}")
      list(FIND SPACEHOUND_GIT_TAG_LIST "v${PROJECT_VERSION}" SPACEHOUND_VERSION_TAG_INDEX)
      list(FIND SPACEHOUND_GIT_TAG_LIST "${PROJECT_VERSION}" SPACEHOUND_VERSION_PLAIN_TAG_INDEX)
      if((SPACEHOUND_VERSION_TAG_INDEX GREATER -1 OR SPACEHOUND_VERSION_PLAIN_TAG_INDEX GREATER -1) AND
         NOT SPACEHOUND_GIT_IS_DIRTY)
        set(SPACEHOUND_VERSION_IS_RELEASE_BUILD_LITERAL true)
      endif()
    endif()

    if(NOT SPACEHOUND_VERSION_IS_RELEASE_BUILD_LITERAL)
      set(SPACEHOUND_VERSION_BUILD_METADATA "git.${SPACEHOUND_GIT_SHA}")
      if(SPACEHOUND_GIT_IS_DIRTY)
        string(APPEND SPACEHOUND_VERSION_BUILD_METADATA ".dirty")
      endif()
      set(SPACEHOUND_VERSION_FULL "${PROJECT_VERSION}+${SPACEHOUND_VERSION_BUILD_METADATA}")
    endif()
  endif()
endif()

file(MAKE_DIRECTORY "${SPACEHOUND_GENERATED_INCLUDE_DIR}/spacehound")
configure_file(
  "${PROJECT_SOURCE_DIR}/include/spacehound/version.hpp.in"
  "${SPACEHOUND_GENERATED_INCLUDE_DIR}/spacehound/version.hpp"
  @ONLY
)

find_library(
  SPACEHOUND_APPLICATIONSERVICES_FRAMEWORK
  NAMES ApplicationServices
  REQUIRED
)

find_library(
  SPACEHOUND_APPKIT_FRAMEWORK
  NAMES AppKit
  REQUIRED
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

find_library(
  SPACEHOUND_SKYLIGHT_FRAMEWORK
  NAMES SkyLight
  PATHS /System/Library/PrivateFrameworks
  NO_DEFAULT_PATH
)

set(SPACEHOUND_HEADERS
  include/spacehound/ax.hpp
  include/spacehound/cgs.hpp
  include/spacehound/cf.hpp
  include/spacehound/cg.hpp
  include/spacehound/control.hpp
  include/spacehound/daemon.hpp
  include/spacehound/dispatch.hpp
  include/spacehound/gesture.hpp
  include/spacehound/ns.hpp
  include/spacehound/settings.hpp
  "${SPACEHOUND_GENERATED_INCLUDE_DIR}/spacehound/version.hpp"
)

set(SPACEHOUND_SOURCES
  lib/control.cpp
  lib/daemon.cpp
  lib/display_switch.cpp
  lib/mission_control.cpp
  lib/ns.mm
  lib/settings.cpp
  lib/version.cpp
)

function(spacehound_apply_warnings target_name)
  if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    target_compile_options(
      "${target_name}"
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
    )
  endif()

  if(SPACEHOUND_WARNINGS_AS_ERRORS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
      target_compile_options("${target_name}" PRIVATE -Werror)
    endif()
  endif()
endfunction()

function(spacehound_enable_coverage target_name)
  if(NOT SPACEHOUND_ENABLE_COVERAGE)
    return()
  endif()

  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    message(
      FATAL_ERROR
      "SPACEHOUND_ENABLE_COVERAGE requires Clang or AppleClang."
    )
  endif()

  target_compile_options(
    "${target_name}"
    PRIVATE
      -fprofile-instr-generate
      -fcoverage-mapping
  )
  target_link_options(
    "${target_name}"
    PRIVATE
      -fprofile-instr-generate
      -fcoverage-mapping
  )
endfunction()

function(spacehound_configure_library target_name)
  target_sources(
    "${target_name}"
    PRIVATE
      ${SPACEHOUND_HEADERS}
      ${SPACEHOUND_SOURCES}
  )

  target_compile_features("${target_name}" PUBLIC cxx_std_23)

  target_include_directories(
    "${target_name}"
    PUBLIC
      $<BUILD_INTERFACE:${SPACEHOUND_GENERATED_INCLUDE_DIR}>
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
      ${PROJECT_SOURCE_DIR}/lib
      $<TARGET_PROPERTY:nlohmann_json::nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>
  )

  target_link_libraries(
    "${target_name}"
    PUBLIC
      "${SPACEHOUND_APPLICATIONSERVICES_FRAMEWORK}"
      "${SPACEHOUND_APPKIT_FRAMEWORK}"
  )

  if(SPACEHOUND_SKYLIGHT_FRAMEWORK)
    target_link_libraries(
      "${target_name}"
      PUBLIC
        "${SPACEHOUND_SKYLIGHT_FRAMEWORK}"
    )
    target_link_directories(
      "${target_name}"
      PUBLIC
        /System/Library/PrivateFrameworks
    )
  endif()

  set_target_properties(
    "${target_name}"
    PROPERTIES
      CXX_STANDARD 23
      CXX_EXTENSIONS OFF
      OBJCXX_EXTENSIONS OFF
      OBJCXX_STANDARD 23
      OUTPUT_NAME spacehound
  )

  spacehound_apply_warnings("${target_name}")
endfunction()

add_library(spacehound_core STATIC)
add_library(spacehound::core ALIAS spacehound_core)
spacehound_configure_library(spacehound_core)
spacehound_enable_coverage(spacehound_core)

if(SPACEHOUND_BUILD_EXAMPLES)
  add_executable(
    spacehound_smoke_example
    examples/smoke/basic.cpp
  )
  target_link_libraries(spacehound_smoke_example PRIVATE spacehound::core)
  target_compile_features(spacehound_smoke_example PRIVATE cxx_std_23)
  set_target_properties(
    spacehound_smoke_example
    PROPERTIES
      CXX_EXTENSIONS OFF
  )
  spacehound_apply_warnings(spacehound_smoke_example)

  add_executable(
    spacehound_fast_swipe_example
    examples/fast_swipe.cpp
  )
  target_link_libraries(spacehound_fast_swipe_example PRIVATE spacehound::core)
  target_compile_features(spacehound_fast_swipe_example PRIVATE cxx_std_23)
  set_target_properties(
    spacehound_fast_swipe_example
    PROPERTIES
      CXX_EXTENSIONS OFF
  )
  spacehound_apply_warnings(spacehound_fast_swipe_example)

  add_executable(
    spacehound_fast_swipe_tap_example
    examples/fast_swipe_tap.cpp
  )
  target_link_libraries(spacehound_fast_swipe_tap_example PRIVATE spacehound::core)
  target_compile_features(spacehound_fast_swipe_tap_example PRIVATE cxx_std_23)
  set_target_properties(
    spacehound_fast_swipe_tap_example
    PROPERTIES
      CXX_EXTENSIONS OFF
  )
  spacehound_apply_warnings(spacehound_fast_swipe_tap_example)

  add_executable(
    spacehound_mc_probe_example
    examples/mc_probe.cpp
  )
  target_link_libraries(spacehound_mc_probe_example PRIVATE spacehound::core)
  target_include_directories(spacehound_mc_probe_example PRIVATE "${PROJECT_SOURCE_DIR}/lib")
  target_compile_features(spacehound_mc_probe_example PRIVATE cxx_std_23)
  set_target_properties(
    spacehound_mc_probe_example
    PROPERTIES
      CXX_EXTENSIONS OFF
  )
  spacehound_apply_warnings(spacehound_mc_probe_example)
endif()

if(SPACEHOUND_BUILD_LEGACY_EXAMPLES)
  enable_language(C)

  add_executable(
    fast_swipe
    examples/legacy/fast_swipe.c
  )
  target_link_libraries(fast_swipe PRIVATE "${SPACEHOUND_APPLICATIONSERVICES_FRAMEWORK}")

  add_executable(
    fast_swipe_tap
    examples/legacy/fast_swipe_tap.c
  )
  target_link_libraries(fast_swipe_tap PRIVATE "${SPACEHOUND_APPLICATIONSERVICES_FRAMEWORK}")
endif()

if(BUILD_TESTING AND SPACEHOUND_BUILD_TESTS)
  add_subdirectory(tests)
endif()
