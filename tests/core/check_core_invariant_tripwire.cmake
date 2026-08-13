if(NOT DEFINED TRIPWIRE_EXE)
    message(FATAL_ERROR "TRIPWIRE_EXE is required")
endif()
if(NOT EXISTS "${TRIPWIRE_EXE}")
    message(FATAL_ERROR "core invariant tripwire executable does not exist: ${TRIPWIRE_EXE}")
endif()

set(cases
    "array-length|Array length overflow"
    "array-capacity|Array capacity overflow"
    "array-bytes|Array allocation size overflow"
    "hashmap-capacity|HashMap capacity overflow"
    "hashmap-bytes|HashMap allocation size overflow"
    "arena-init-state|arena committed exceeds reserved budget"
    "arena-init-address|arena reserved address range overflow"
    "arena-uninitialized|arena is not initialized"
    "arena-state-order|arena offset exceeds reserved budget"
    "arena-address|arena reserved address range overflow"
    "arena-alignment|arena alignment arithmetic overflow"
    "arena-end|arena push exceeds reserved budget"
    "arena-rounding|arena commit rounding overflow")

foreach(case IN LISTS cases)
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 mode)
    list(GET fields 1 expected)
    execute_process(
        COMMAND "${TRIPWIRE_EXE}" "${mode}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    set(combined "${stdout}\n${stderr}")
    if(result EQUAL 0)
        message(FATAL_ERROR "${mode} returned success; invariant did not halt")
    endif()
    string(FIND "${combined}" "${expected}" diagnostic_at)
    if(diagnostic_at EQUAL -1)
        message(FATAL_ERROR
            "${mode} failed without expected diagnostic '${expected}':\n${combined}")
    endif()
endforeach()

list(LENGTH cases case_count)
message(STATUS "core invariant tripwires: ${case_count} expected failures passed")
