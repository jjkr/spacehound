if(DEFINED binary_path)
  set(executable_path "${binary_path}")
elseif(DEFINED daemon_path)
  set(executable_path "${daemon_path}")
else()
  message(FATAL_ERROR "binary_path or daemon_path must be provided.")
endif()

if(NOT DEFINED expected_result)
  message(FATAL_ERROR "expected_result must be provided.")
endif()

execute_process(
  COMMAND "${executable_path}" ${daemon_args}
  RESULT_VARIABLE daemon_result
  OUTPUT_VARIABLE daemon_stdout
  ERROR_VARIABLE daemon_stderr
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)

if(NOT daemon_result EQUAL expected_result)
  message(
    FATAL_ERROR
    "Expected exit code ${expected_result}, got ${daemon_result}. stdout='${daemon_stdout}' stderr='${daemon_stderr}'"
  )
endif()

if(DEFINED expected_stdout AND NOT daemon_stdout STREQUAL expected_stdout)
  message(
    FATAL_ERROR
    "Expected stdout '${expected_stdout}', got '${daemon_stdout}'."
  )
endif()

if(DEFINED expected_stderr AND NOT daemon_stderr STREQUAL expected_stderr)
  message(
    FATAL_ERROR
    "Expected stderr '${expected_stderr}', got '${daemon_stderr}'."
  )
endif()

if(DEFINED expected_stdout_contains)
  string(FIND "${daemon_stdout}" "${expected_stdout_contains}" stdout_match_index)
  if(stdout_match_index EQUAL -1)
    message(
      FATAL_ERROR
      "Expected stdout to contain '${expected_stdout_contains}', got '${daemon_stdout}'."
    )
  endif()
endif()

if(DEFINED expected_stderr_contains)
  string(FIND "${daemon_stderr}" "${expected_stderr_contains}" stderr_match_index)
  if(stderr_match_index EQUAL -1)
    message(
      FATAL_ERROR
      "Expected stderr to contain '${expected_stderr_contains}', got '${daemon_stderr}'."
    )
  endif()
endif()
