cmake_minimum_required(VERSION 3.30)

set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum supported macOS version" FORCE)

project(spacerabbit VERSION 0.3.0 LANGUAGES CXX OBJCXX)

if(NOT APPLE)
  message(FATAL_ERROR "spacerabbit currently supports macOS only.")
endif()

include(CTest)
include(FetchContent)
find_package(Git QUIET)

option(SPACERABBIT_BUILD_TESTS "Build spacerabbit tests." ON)
option(SPACERABBIT_BUILD_EXAMPLES "Build spacerabbit examples." ON)
option(SPACERABBIT_BUILD_LEGACY_EXAMPLES "Build the legacy C prototype examples." OFF)
option(SPACERABBIT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors." OFF)
option(SPACERABBIT_ENABLE_COVERAGE "Enable LLVM source-based coverage for in-tree targets." OFF)
option(
  SPACERABBIT_ENABLE_LIVE_SYSTEM_TESTS
  "Build opt-in live system tests that can post events to the current process."
  OFF
)

set(SPACERABBIT_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/generated/include")

set(SPACERABBIT_VERSION_BUILD_METADATA "")
set(SPACERABBIT_VERSION_FULL "${PROJECT_VERSION}")
set(SPACERABBIT_VERSION_IS_RELEASE_BUILD_LITERAL true)

if(Git_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" rev-parse --short=12 HEAD
    OUTPUT_VARIABLE SPACERABBIT_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SPACERABBIT_GIT_SHA_RESULT
  )
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" status --porcelain --untracked-files=no
    OUTPUT_VARIABLE SPACERABBIT_GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SPACERABBIT_GIT_STATUS_RESULT
  )
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" tag --points-at HEAD
    OUTPUT_VARIABLE SPACERABBIT_GIT_TAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SPACERABBIT_GIT_TAGS_RESULT
  )

  if(SPACERABBIT_GIT_SHA_RESULT EQUAL 0 AND NOT SPACERABBIT_GIT_SHA STREQUAL "")
    set(SPACERABBIT_VERSION_IS_RELEASE_BUILD_LITERAL false)

    set(SPACERABBIT_GIT_IS_DIRTY false)
    if(NOT SPACERABBIT_GIT_STATUS_RESULT EQUAL 0 OR NOT SPACERABBIT_GIT_STATUS STREQUAL "")
      set(SPACERABBIT_GIT_IS_DIRTY true)
    endif()

    if(SPACERABBIT_GIT_TAGS_RESULT EQUAL 0)
      string(REPLACE "\n" ";" SPACERABBIT_GIT_TAG_LIST "${SPACERABBIT_GIT_TAGS}")
      list(FIND SPACERABBIT_GIT_TAG_LIST "v${PROJECT_VERSION}" SPACERABBIT_VERSION_TAG_INDEX)
      list(FIND SPACERABBIT_GIT_TAG_LIST "${PROJECT_VERSION}" SPACERABBIT_VERSION_PLAIN_TAG_INDEX)
      if((SPACERABBIT_VERSION_TAG_INDEX GREATER -1 OR SPACERABBIT_VERSION_PLAIN_TAG_INDEX GREATER -1) AND
         NOT SPACERABBIT_GIT_IS_DIRTY)
        set(SPACERABBIT_VERSION_IS_RELEASE_BUILD_LITERAL true)
      endif()
    endif()

    if(NOT SPACERABBIT_VERSION_IS_RELEASE_BUILD_LITERAL)
      set(SPACERABBIT_VERSION_BUILD_METADATA "git.${SPACERABBIT_GIT_SHA}")
      if(SPACERABBIT_GIT_IS_DIRTY)
        string(APPEND SPACERABBIT_VERSION_BUILD_METADATA ".dirty")
      endif()
      set(SPACERABBIT_VERSION_FULL "${PROJECT_VERSION}+${SPACERABBIT_VERSION_BUILD_METADATA}")
    endif()
  endif()
endif()

file(MAKE_DIRECTORY "${SPACERABBIT_GENERATED_INCLUDE_DIR}/spacerabbit")
configure_file(
  "${PROJECT_SOURCE_DIR}/include/spacerabbit/version.hpp.in"
  "${SPACERABBIT_GENERATED_INCLUDE_DIR}/spacerabbit/version.hpp"
  @ONLY
)

find_library(
  SPACERABBIT_APPLICATIONSERVICES_FRAMEWORK
  NAMES ApplicationServices
  REQUIRED
)

find_library(
  SPACERABBIT_APPKIT_FRAMEWORK
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
  SPACERABBIT_SKYLIGHT_FRAMEWORK
  NAMES SkyLight
  PATHS /System/Library/PrivateFrameworks
  NO_DEFAULT_PATH
)

set(SPACERABBIT_HEADERS
  include/spacerabbit/ax.hpp
  include/spacerabbit/cgs.hpp
  include/spacerabbit/cf.hpp
  include/spacerabbit/cg.hpp
  include/spacerabbit/control.hpp
  include/spacerabbit/daemon.hpp
  include/spacerabbit/dispatch.hpp
  include/spacerabbit/gesture.hpp
  include/spacerabbit/ns.hpp
  include/spacerabbit/settings.hpp
  "${SPACERABBIT_GENERATED_INCLUDE_DIR}/spacerabbit/version.hpp"
)

set(SPACERABBIT_SOURCES
  lib/control.cpp
  lib/daemon.cpp
  lib/display_switch.cpp
  lib/ns.mm
  lib/settings.cpp
  lib/version.cpp
)

function(spacerabbit_apply_warnings target_name)
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

  if(SPACERABBIT_WARNINGS_AS_ERRORS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
      target_compile_options("${target_name}" PRIVATE -Werror)
    endif()
  endif()
endfunction()

function(spacerabbit_enable_coverage target_name)
  if(NOT SPACERABBIT_ENABLE_COVERAGE)
    return()
  endif()

  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    message(
      FATAL_ERROR
      "SPACERABBIT_ENABLE_COVERAGE requires Clang or AppleClang."
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

function(spacerabbit_configure_library target_name)
  target_sources(
    "${target_name}"
    PRIVATE
      ${SPACERABBIT_HEADERS}
      ${SPACERABBIT_SOURCES}
  )

  target_compile_features("${target_name}" PUBLIC cxx_std_23)

  target_include_directories(
    "${target_name}"
    PUBLIC
      $<BUILD_INTERFACE:${SPACERABBIT_GENERATED_INCLUDE_DIR}>
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE
      ${PROJECT_SOURCE_DIR}/lib
      $<TARGET_PROPERTY:nlohmann_json::nlohmann_json,INTERFACE_INCLUDE_DIRECTORIES>
  )

  target_link_libraries(
    "${target_name}"
    PUBLIC
      "${SPACERABBIT_APPLICATIONSERVICES_FRAMEWORK}"
      "${SPACERABBIT_APPKIT_FRAMEWORK}"
  )

  if(SPACERABBIT_SKYLIGHT_FRAMEWORK)
    target_link_libraries(
      "${target_name}"
      PUBLIC
        "${SPACERABBIT_SKYLIGHT_FRAMEWORK}"
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
      OUTPUT_NAME spacerabbit
  )

  spacerabbit_apply_warnings("${target_name}")
endfunction()

add_library(spacerabbit_core STATIC)
add_library(spacerabbit::core ALIAS spacerabbit_core)
spacerabbit_configure_library(spacerabbit_core)
spacerabbit_enable_coverage(spacerabbit_core)

if(SPACERABBIT_BUILD_EXAMPLES)
  add_executable(
    spacerabbit_smoke_example
    examples/smoke/basic.cpp
  )
  target_link_libraries(spacerabbit_smoke_example PRIVATE spacerabbit::core)
  target_compile_features(spacerabbit_smoke_example PRIVATE cxx_std_23)
  set_target_properties(
    spacerabbit_smoke_example
    PROPERTIES
      CXX_EXTENSIONS OFF
  )
  spacerabbit_apply_warnings(spacerabbit_smoke_example)

  add_executable(
    spacerabbit_fast_swipe_example
    examples/fast_swipe.cpp
  )
  target_link_libraries(spacerabbit_fast_swipe_example PRIVATE spacerabbit::core)
  target_compile_features(spacerabbit_fast_swipe_example PRIVATE cxx_std_23)
  set_target_properties(
    spacerabbit_fast_swipe_example
    PROPERTIES
      CXX_EXTENSIONS OFF
  )
  spacerabbit_apply_warnings(spacerabbit_fast_swipe_example)

  add_executable(
    spacerabbit_fast_swipe_tap_example
    examples/fast_swipe_tap.cpp
  )
  target_link_libraries(spacerabbit_fast_swipe_tap_example PRIVATE spacerabbit::core)
  target_compile_features(spacerabbit_fast_swipe_tap_example PRIVATE cxx_std_23)
  set_target_properties(
    spacerabbit_fast_swipe_tap_example
    PROPERTIES
      CXX_EXTENSIONS OFF
  )
  spacerabbit_apply_warnings(spacerabbit_fast_swipe_tap_example)
endif()

if(SPACERABBIT_BUILD_LEGACY_EXAMPLES)
  enable_language(C)

  add_executable(
    fast_swipe
    examples/legacy/fast_swipe.c
  )
  target_link_libraries(fast_swipe PRIVATE "${SPACERABBIT_APPLICATIONSERVICES_FRAMEWORK}")

  add_executable(
    fast_swipe_tap
    examples/legacy/fast_swipe_tap.c
  )
  target_link_libraries(fast_swipe_tap PRIVATE "${SPACERABBIT_APPLICATIONSERVICES_FRAMEWORK}")
endif()

if(BUILD_TESTING AND SPACERABBIT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
