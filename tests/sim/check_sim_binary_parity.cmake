foreach(required IN ITEMS TEST_EXE GAME_EXE GAME_NULL_EXE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
    if(NOT EXISTS "${${required}}")
        message(FATAL_ERROR "${required} does not exist: ${${required}}")
    endif()
endforeach()

function(run_oracle_probe label executable out_variable)
    execute_process(
        COMMAND "${executable}" --sim-self-check
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "${label} oracle failed with exit ${result}:\nstdout=${stdout}\nstderr=${stderr}")
    endif()
    string(REPLACE "\r\n" "\n" stdout "${stdout}")
    string(STRIP "${stdout}" stdout)
    set(${out_variable} "${stdout}" PARENT_SCOPE)
endfunction()

run_oracle_probe("test" "${TEST_EXE}" test_output)
run_oracle_probe("game" "${GAME_EXE}" game_output)
run_oracle_probe("game-null" "${GAME_NULL_EXE}" game_null_output)

if(NOT test_output STREQUAL game_output)
    message(FATAL_ERROR "test/game oracle mismatch:\ntest=${test_output}\ngame=${game_output}")
endif()
if(NOT test_output STREQUAL game_null_output)
    message(FATAL_ERROR
        "test/game-null oracle mismatch:\ntest=${test_output}\ngame-null=${game_null_output}")
endif()

foreach(required_field IN ITEMS
        "ticks=10000"
        "commands=923"
        "final=0x36e6de56cb662dba"
        "stream=0xb6067f3f0955b292"
        "logic=0x5b47e648953a63fc")
    string(FIND "${test_output}" "${required_field}" field_at)
    if(field_at EQUAL -1)
        message(FATAL_ERROR "oracle output is missing ${required_field}: ${test_output}")
    endif()
endforeach()

message(STATUS "sim binary parity clean: ${test_output}")
