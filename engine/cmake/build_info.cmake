execute_process(
  COMMAND git -C "${SRC}" rev-parse --short=7 HEAD
  OUTPUT_VARIABLE hash
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE res)
if(NOT res EQUAL 0 OR hash STREQUAL "")
  set(hash "")
endif()
set(content "#pragma once\n#define OMORI_COMMIT \"${hash}\"\n")
set(old "")
if(EXISTS "${OUT}")
  file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
  file(WRITE "${OUT}" "${content}")
endif()
