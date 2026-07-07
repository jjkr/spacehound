if(NOT DEFINED PROJECT_BINARY_DIR)
  message(FATAL_ERROR "PROJECT_BINARY_DIR is required.")
endif()

if(NOT DEFINED CTEST_COMMAND)
  message(FATAL_ERROR "CTEST_COMMAND is required.")
endif()

if(NOT DEFINED LLVM_PROFDATA)
  message(FATAL_ERROR "LLVM_PROFDATA is required.")
endif()

if(NOT DEFINED LLVM_COV)
  message(FATAL_ERROR "LLVM_COV is required.")
endif()

if(NOT DEFINED COVERAGE_OBJECTS OR COVERAGE_OBJECTS STREQUAL "")
  message(FATAL_ERROR "COVERAGE_OBJECTS is required.")
endif()

set(coverage_dir "${PROJECT_BINARY_DIR}/coverage")
set(profile_dir "${coverage_dir}/profiles")
set(profdata_file "${coverage_dir}/spacerabbit.profdata")
set(lcov_file "${coverage_dir}/spacerabbit.lcov")
set(ignore_regex ".*/(_deps/googletest-[^/]+|tests)/.*")

file(REMOVE_RECURSE "${coverage_dir}")
file(MAKE_DIRECTORY "${profile_dir}")

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env
    "LLVM_PROFILE_FILE=${profile_dir}/%p-%m.profraw"
    "${CTEST_COMMAND}" "--output-on-failure" "-L" "unit"
  WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
  RESULT_VARIABLE ctest_result
)

if(NOT ctest_result EQUAL 0)
  message(FATAL_ERROR "Coverage test run failed.")
endif()

file(GLOB profraw_files "${profile_dir}/*.profraw")
if(profraw_files STREQUAL "")
  message(FATAL_ERROR "No raw coverage profiles were generated.")
endif()

set(coverage_args)
foreach(object_file IN LISTS COVERAGE_OBJECTS)
  list(APPEND coverage_args -object "${object_file}")
endforeach()

execute_process(
  COMMAND "${LLVM_PROFDATA}" merge -sparse ${profraw_files} -o "${profdata_file}"
  RESULT_VARIABLE merge_result
)

if(NOT merge_result EQUAL 0)
  message(FATAL_ERROR "llvm-profdata merge failed.")
endif()

message(STATUS "Coverage summary")
execute_process(
  COMMAND
    "${LLVM_COV}" report
    -instr-profile "${profdata_file}"
    -ignore-filename-regex "${ignore_regex}"
    ${coverage_args}
  WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
  RESULT_VARIABLE report_result
)

if(NOT report_result EQUAL 0)
  message(FATAL_ERROR "llvm-cov report failed.")
endif()

execute_process(
  COMMAND
    "${LLVM_COV}" export
    -format=lcov
    -instr-profile "${profdata_file}"
    -ignore-filename-regex "${ignore_regex}"
    ${coverage_args}
  OUTPUT_FILE "${lcov_file}"
  WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
  RESULT_VARIABLE export_result
)

if(NOT export_result EQUAL 0)
  message(FATAL_ERROR "llvm-cov export failed.")
endif()

message(STATUS "Wrote coverage data to ${coverage_dir}")
message(STATUS "LCOV report: ${lcov_file}")
