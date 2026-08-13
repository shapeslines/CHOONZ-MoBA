if(NOT DEFINED TRIPWIRE_EXE)
    message(FATAL_ERROR "TRIPWIRE_EXE is required")
endif()
if(NOT EXISTS "${TRIPWIRE_EXE}")
    message(FATAL_ERROR "UBSan tripwire executable does not exist: ${TRIPWIRE_EXE}")
endif()

execute_process(
    COMMAND "${TRIPWIRE_EXE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
set(combined "${stdout}\n${stderr}")
if(result EQUAL 0)
    message(FATAL_ERROR "UBSan tripwire returned success; instrumentation did not halt:\n${combined}")
endif()
string(FIND "${combined}" "runtime error: signed integer overflow" diagnostic_at)
if(diagnostic_at EQUAL -1)
    message(FATAL_ERROR
        "UBSan tripwire failed without the expected signed-overflow diagnostic:\n${combined}")
endif()

message(STATUS "clang-cl UBSan tripwire emitted the expected signed-overflow diagnostic")
