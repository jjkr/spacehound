include(FetchContent)
include(GoogleTest)

set(INSTALL_GTEST OFF CACHE BOOL "Disable googletest install rules." FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "Use shared CRT on Windows." FORCE)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.zip
  URL_HASH SHA256=63b9c77751a5b8f492486005f67533fdc58682b67476fcb91650be5958d5195a
)
FetchContent_MakeAvailable(googletest)

function(spacehound_add_gtest target_name test_label)
  add_executable("${target_name}" ${ARGN})
  target_link_libraries("${target_name}" PRIVATE GTest::gtest_main)
  target_compile_features("${target_name}" PRIVATE cxx_std_23)
  target_include_directories(
    "${target_name}"
    PRIVATE
      ${PROJECT_SOURCE_DIR}/lib
  )
  set_target_properties(
    "${target_name}"
    PROPERTIES
      CXX_EXTENSIONS OFF
  )
  spacehound_apply_warnings("${target_name}")
  spacehound_enable_coverage("${target_name}")
  gtest_discover_tests(
    "${target_name}"
    TEST_PREFIX "${target_name}."
    DISCOVERY_TIMEOUT 30
    PROPERTIES
      LABELS "${test_label}"
  )
endfunction()

spacehound_add_gtest(
  spacehound_library_tests
  unit
  library_tests.cpp
)
target_link_libraries(spacehound_library_tests PRIVATE spacehound::core)

spacehound_add_gtest(
  spacehound_ax_tests
  unit
  ax_tests.cpp
)
target_link_libraries(
  spacehound_ax_tests
  PRIVATE
    spacehound::core
)

spacehound_add_gtest(
  spacehound_cgs_tests
  unit
  cgs_tests.cpp
)
target_link_libraries(
  spacehound_cgs_tests
  PRIVATE
    spacehound::core
)

spacehound_add_gtest(
  spacehound_cf_tests
  unit
  cf_tests.cpp
)
target_link_libraries(
  spacehound_cf_tests
  PRIVATE
    spacehound::core
)

spacehound_add_gtest(
  spacehound_cg_tests
  unit
  cg_tests.cpp
)
target_link_libraries(
  spacehound_cg_tests
  PRIVATE
    spacehound::core
)

spacehound_add_gtest(
  spacehound_gesture_tests
  unit
  gesture_tests.cpp
)
target_link_libraries(
  spacehound_gesture_tests
  PRIVATE
    spacehound::core
)

spacehound_add_gtest(
  spacehound_dispatch_tests
  unit
  dispatch_tests.cpp
)
target_link_libraries(
  spacehound_dispatch_tests
  PRIVATE
    spacehound::core
)

spacehound_add_gtest(
  spacehound_ns_tests
  unit
  ns_tests.cpp
)
target_link_libraries(
  spacehound_ns_tests
  PRIVATE
    spacehound::core
)

spacehound_add_gtest(
  spacehound_settings_tests
  unit
  settings_tests.cpp
)
target_link_libraries(
  spacehound_settings_tests
  PRIVATE
    spacehound::core
)

spacehound_add_gtest(
  spacehound_control_tests
  unit
  control_tests.cpp
)
target_link_libraries(
  spacehound_control_tests
  PRIVATE
    spacehound::core
)

spacehound_add_gtest(
  spacehound_daemon_tests
  unit
  daemon_tests.cpp
)
target_link_libraries(
  spacehound_daemon_tests
  PRIVATE
    spacehound::core
)

if(SPACEHOUND_ENABLE_LIVE_SYSTEM_TESTS)
  spacehound_add_gtest(
    spacehound_live_system_tests
    live
    live_system_tests.cpp
  )
  target_link_libraries(
    spacehound_live_system_tests
    PRIVATE
      spacehound::core
  )
endif()

if(SPACEHOUND_ENABLE_COVERAGE)
  execute_process(
    COMMAND xcrun --find llvm-profdata
    OUTPUT_VARIABLE SPACEHOUND_LLVM_PROFDATA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SPACEHOUND_LLVM_PROFDATA_RESULT
  )
  if(NOT SPACEHOUND_LLVM_PROFDATA_RESULT EQUAL 0 OR
     SPACEHOUND_LLVM_PROFDATA STREQUAL "")
    message(FATAL_ERROR "Failed to locate llvm-profdata via xcrun.")
  endif()

  execute_process(
    COMMAND xcrun --find llvm-cov
    OUTPUT_VARIABLE SPACEHOUND_LLVM_COV
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SPACEHOUND_LLVM_COV_RESULT
  )
  if(NOT SPACEHOUND_LLVM_COV_RESULT EQUAL 0 OR
     SPACEHOUND_LLVM_COV STREQUAL "")
    message(FATAL_ERROR "Failed to locate llvm-cov via xcrun.")
  endif()

  add_custom_target(
    coverage
    COMMAND
      "${CMAKE_COMMAND}"
      -DPROJECT_BINARY_DIR=${PROJECT_BINARY_DIR}
      -DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}
      -DLLVM_PROFDATA=${SPACEHOUND_LLVM_PROFDATA}
      -DLLVM_COV=${SPACEHOUND_LLVM_COV}
      "-DCOVERAGE_OBJECTS=$<TARGET_FILE:spacehound_library_tests>;$<TARGET_FILE:spacehound_ax_tests>;$<TARGET_FILE:spacehound_cgs_tests>;$<TARGET_FILE:spacehound_cf_tests>;$<TARGET_FILE:spacehound_cg_tests>;$<TARGET_FILE:spacehound_gesture_tests>;$<TARGET_FILE:spacehound_dispatch_tests>;$<TARGET_FILE:spacehound_ns_tests>;$<TARGET_FILE:spacehound_settings_tests>;$<TARGET_FILE:spacehound_control_tests>"
      -P
      "${PROJECT_SOURCE_DIR}/cmake/RunCoverage.cmake"
    DEPENDS
      spacehound_library_tests
      spacehound_ax_tests
      spacehound_cgs_tests
      spacehound_cf_tests
      spacehound_cg_tests
      spacehound_gesture_tests
      spacehound_dispatch_tests
      spacehound_ns_tests
      spacehound_settings_tests
      spacehound_control_tests
      spacehound_daemon_tests
    USES_TERMINAL
    VERBATIM
  )
endif()
