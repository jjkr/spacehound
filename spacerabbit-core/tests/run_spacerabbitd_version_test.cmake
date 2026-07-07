if(DEFINED binary_path)
  set(executable_path "${binary_path}")
elseif(DEFINED daemon_path)
  set(executable_path "${daemon_path}")
else()
  message(FATAL_ERROR "binary_path or daemon_path must be provided.")
endif()

if(NOT DEFINED expected_version)
  message(FATAL_ERROR "expected_version must be provided.")
endif()

if(DEFINED binary_name)
  set(executable_name "${binary_name}")
else()
  set(executable_name "spacerabbitd")
endif()

execute_process(
  COMMAND "${executable_path}" --version
  RESULT_VARIABLE daemon_result
  OUTPUT_VARIABLE daemon_stdout
  ERROR_VARIABLE daemon_stderr
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)

if(NOT daemon_result EQUAL 0)
  message(
    FATAL_ERROR
    "${executable_name} --version failed with exit code ${daemon_result}: ${daemon_stderr}"
  )
endif()

if(NOT daemon_stdout STREQUAL expected_version)
  message(
    FATAL_ERROR
    "Expected ${executable_name} version '${expected_version}', got '${daemon_stdout}'."
  )
endif()
