if(NOT DEFINED source_binary_dir)
  message(FATAL_ERROR "source_binary_dir is required.")
endif()

if(NOT DEFINED consumer_source_dir)
  message(FATAL_ERROR "consumer_source_dir is required.")
endif()

if(NOT DEFINED consumer_binary_dir)
  message(FATAL_ERROR "consumer_binary_dir is required.")
endif()

if(NOT DEFINED install_prefix)
  message(FATAL_ERROR "install_prefix is required.")
endif()

if(NOT DEFINED link_target)
  message(FATAL_ERROR "link_target is required.")
endif()

if(NOT DEFINED enable_coverage)
  set(enable_coverage OFF)
endif()

file(REMOVE_RECURSE "${consumer_binary_dir}" "${install_prefix}")

set(install_command
  "${CMAKE_COMMAND}"
  --install "${source_binary_dir}"
  --prefix "${install_prefix}"
)

if(DEFINED build_type AND NOT build_type STREQUAL "")
  list(APPEND install_command --config "${build_type}")
endif()

execute_process(
  COMMAND ${install_command}
  RESULT_VARIABLE install_result
)

if(NOT install_result EQUAL 0)
  message(FATAL_ERROR "Failed to install spacerabbit into ${install_prefix}.")
endif()

set(configure_command
  "${CMAKE_COMMAND}"
  -S "${consumer_source_dir}"
  -B "${consumer_binary_dir}"
  "-DCMAKE_PREFIX_PATH=${install_prefix}"
  "-DSPACERABBIT_TEST_LINK_TARGET=${link_target}"
  "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0"
)

if(DEFINED generator AND NOT generator STREQUAL "")
  list(APPEND configure_command -G "${generator}")
endif()

if(DEFINED build_type AND NOT build_type STREQUAL "")
  list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${build_type}")
endif()

if(enable_coverage)
  list(APPEND configure_command
    "-DCMAKE_CXX_FLAGS=-fprofile-instr-generate -fcoverage-mapping"
    "-DCMAKE_EXE_LINKER_FLAGS=-fprofile-instr-generate -fcoverage-mapping"
  )
endif()

execute_process(
  COMMAND ${configure_command}
  RESULT_VARIABLE configure_result
)

if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Failed to configure the package consumer for ${link_target}.")
endif()

set(build_command "${CMAKE_COMMAND}" --build "${consumer_binary_dir}")

if(DEFINED build_type AND NOT build_type STREQUAL "")
  list(APPEND build_command --config "${build_type}")
endif()

execute_process(
  COMMAND ${build_command}
  RESULT_VARIABLE build_result
)

if(NOT build_result EQUAL 0)
  message(FATAL_ERROR "Failed to build the package consumer for ${link_target}.")
endif()

set(consumer_executable "${consumer_binary_dir}/spacerabbit_package_consumer")

if(DEFINED build_type AND NOT build_type STREQUAL "")
  set(multi_config_candidate "${consumer_binary_dir}/${build_type}/spacerabbit_package_consumer")
  if(EXISTS "${multi_config_candidate}")
    set(consumer_executable "${multi_config_candidate}")
  endif()
endif()

execute_process(
  COMMAND "${consumer_executable}"
  RESULT_VARIABLE run_result
)

if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "Installed package consumer failed for ${link_target}.")
endif()
