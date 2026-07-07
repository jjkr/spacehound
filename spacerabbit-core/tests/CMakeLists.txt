include(FetchContent)
include(GoogleTest)

set(INSTALL_GTEST OFF CACHE BOOL "Disable googletest install rules." FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "Use shared CRT on Windows." FORCE)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
  URL_HASH SHA256=40d4ec942217dcc84a9ebe2a68584ada7d4a33a8ee958755763278ea1c5e18ff
)
FetchContent_MakeAvailable(googletest)

function(spacerabbit_add_gtest target_name test_label)
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
  spacerabbit_apply_warnings("${target_name}")
  spacerabbit_enable_coverage("${target_name}")
  gtest_discover_tests(
    "${target_name}"
    TEST_PREFIX "${target_name}."
    DISCOVERY_TIMEOUT 30
    PROPERTIES
      LABELS "${test_label}"
  )
endfunction()

spacerabbit_add_gtest(
  spacerabbit_library_tests
  unit
  library_tests.cpp
)
target_link_libraries(spacerabbit_library_tests PRIVATE spacerabbit_static)

spacerabbit_add_gtest(
  spacerabbit_ax_tests
  unit
  ax_tests.cpp
)
target_link_libraries(
  spacerabbit_ax_tests
  PRIVATE
    spacerabbit_static
)

spacerabbit_add_gtest(
  spacerabbit_cgs_tests
  unit
  cgs_tests.cpp
)
target_link_libraries(
  spacerabbit_cgs_tests
  PRIVATE
    spacerabbit_static
)

spacerabbit_add_gtest(
  spacerabbit_cf_tests
  unit
  cf_tests.cpp
)
target_link_libraries(
  spacerabbit_cf_tests
  PRIVATE
    spacerabbit_static
)

spacerabbit_add_gtest(
  spacerabbit_cg_tests
  unit
  cg_tests.cpp
)
target_link_libraries(
  spacerabbit_cg_tests
  PRIVATE
    spacerabbit_static
)

spacerabbit_add_gtest(
  spacerabbit_gesture_tests
  unit
  gesture_tests.cpp
)
target_link_libraries(
  spacerabbit_gesture_tests
  PRIVATE
    spacerabbit_static
)

spacerabbit_add_gtest(
  spacerabbit_dispatch_tests
  unit
  dispatch_tests.cpp
)
target_link_libraries(
  spacerabbit_dispatch_tests
  PRIVATE
    spacerabbit_static
)

spacerabbit_add_gtest(
  spacerabbit_ns_tests
  unit
  ns_tests.cpp
)
target_link_libraries(
  spacerabbit_ns_tests
  PRIVATE
    spacerabbit_static
)

spacerabbit_add_gtest(
  spacerabbit_settings_tests
  unit
  settings_tests.cpp
)
target_link_libraries(
  spacerabbit_settings_tests
  PRIVATE
    spacerabbit_static
)

spacerabbit_add_gtest(
  spacerabbit_control_tests
  unit
  control_tests.cpp
)
target_link_libraries(
  spacerabbit_control_tests
  PRIVATE
    spacerabbit_static
)

spacerabbit_add_gtest(
  spacerabbit_daemon_tests
  unit
  daemon_tests.cpp
)
target_link_libraries(
  spacerabbit_daemon_tests
  PRIVATE
    spacerabbit_static
)

if(SPACERABBIT_ENABLE_LIVE_SYSTEM_TESTS)
  spacerabbit_add_gtest(
    spacerabbit_live_system_tests
    live
    live_system_tests.cpp
  )
  target_link_libraries(
    spacerabbit_live_system_tests
    PRIVATE
      spacerabbit_static
  )
endif()

add_test(
  NAME package_static
  COMMAND
    "${CMAKE_COMMAND}"
    -Dsource_binary_dir=${PROJECT_BINARY_DIR}
    -Dconsumer_source_dir=${PROJECT_SOURCE_DIR}/tests/package_consumer
    -Dconsumer_binary_dir=${PROJECT_BINARY_DIR}/tests/package_static
    -Dinstall_prefix=${PROJECT_BINARY_DIR}/package-static
    -Dlink_target=spacerabbit_static
    -Denable_coverage=${SPACERABBIT_ENABLE_COVERAGE}
    -Dgenerator=${CMAKE_GENERATOR}
    -Dbuild_type=$<CONFIG>
    -P
    ${PROJECT_SOURCE_DIR}/tests/run_package_test.cmake
)
set_tests_properties(package_static PROPERTIES LABELS "package")

add_test(
  NAME package_shared
  COMMAND
    "${CMAKE_COMMAND}"
    -Dsource_binary_dir=${PROJECT_BINARY_DIR}
    -Dconsumer_source_dir=${PROJECT_SOURCE_DIR}/tests/package_consumer
    -Dconsumer_binary_dir=${PROJECT_BINARY_DIR}/tests/package_shared
    -Dinstall_prefix=${PROJECT_BINARY_DIR}/package-shared
    -Dlink_target=spacerabbit_shared
    -Denable_coverage=${SPACERABBIT_ENABLE_COVERAGE}
    -Dgenerator=${CMAKE_GENERATOR}
    -Dbuild_type=$<CONFIG>
    -P
    ${PROJECT_SOURCE_DIR}/tests/run_package_test.cmake
)
set_tests_properties(package_shared PROPERTIES LABELS "package")

if(SPACERABBIT_ENABLE_COVERAGE)
  execute_process(
    COMMAND xcrun --find llvm-profdata
    OUTPUT_VARIABLE SPACERABBIT_LLVM_PROFDATA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SPACERABBIT_LLVM_PROFDATA_RESULT
  )
  if(NOT SPACERABBIT_LLVM_PROFDATA_RESULT EQUAL 0 OR
     SPACERABBIT_LLVM_PROFDATA STREQUAL "")
    message(FATAL_ERROR "Failed to locate llvm-profdata via xcrun.")
  endif()

  execute_process(
    COMMAND xcrun --find llvm-cov
    OUTPUT_VARIABLE SPACERABBIT_LLVM_COV
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SPACERABBIT_LLVM_COV_RESULT
  )
  if(NOT SPACERABBIT_LLVM_COV_RESULT EQUAL 0 OR
     SPACERABBIT_LLVM_COV STREQUAL "")
    message(FATAL_ERROR "Failed to locate llvm-cov via xcrun.")
  endif()

  add_custom_target(
    coverage
    COMMAND
      "${CMAKE_COMMAND}"
      -DPROJECT_BINARY_DIR=${PROJECT_BINARY_DIR}
      -DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}
      -DLLVM_PROFDATA=${SPACERABBIT_LLVM_PROFDATA}
      -DLLVM_COV=${SPACERABBIT_LLVM_COV}
      "-DCOVERAGE_OBJECTS=$<TARGET_FILE:spacerabbit_library_tests>;$<TARGET_FILE:spacerabbit_ax_tests>;$<TARGET_FILE:spacerabbit_cgs_tests>;$<TARGET_FILE:spacerabbit_cf_tests>;$<TARGET_FILE:spacerabbit_cg_tests>;$<TARGET_FILE:spacerabbit_gesture_tests>;$<TARGET_FILE:spacerabbit_dispatch_tests>;$<TARGET_FILE:spacerabbit_ns_tests>;$<TARGET_FILE:spacerabbit_settings_tests>;$<TARGET_FILE:spacerabbit_control_tests>"
      -P
      "${PROJECT_SOURCE_DIR}/cmake/RunCoverage.cmake"
    DEPENDS
      spacerabbit_library_tests
      spacerabbit_ax_tests
      spacerabbit_cgs_tests
      spacerabbit_cf_tests
      spacerabbit_cg_tests
      spacerabbit_gesture_tests
      spacerabbit_dispatch_tests
      spacerabbit_ns_tests
      spacerabbit_settings_tests
      spacerabbit_control_tests
      spacerabbit_daemon_tests
    USES_TERMINAL
    VERBATIM
  )
endif()
