if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED COMPILE_COMMANDS)
    message(FATAL_ERROR "COMPILE_COMMANDS is required")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR is required")
endif()

file(READ "${COMPILE_COMMANDS}" clean_db)
set(checker "${SOURCE_DIR}/tests/sim/check_sim_compiler_policy.cmake")

function(expect_policy_failure fixture_name fixture_text expected_diagnostic)
    set(fixture_path "${WORK_DIR}/${fixture_name}.json")
    file(WRITE "${fixture_path}" "${fixture_text}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                "-DSOURCE_DIR=${SOURCE_DIR}"
                "-DCOMPILE_COMMANDS=${fixture_path}"
                -P "${checker}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    file(REMOVE "${fixture_path}")
    if(result EQUAL 0)
        message(FATAL_ERROR "${fixture_name} unexpectedly passed")
    endif()
    set(combined "${stdout}\n${stderr}")
    string(FIND "${combined}" "${expected_diagnostic}" diagnostic_at)
    if(diagnostic_at EQUAL -1)
        message(FATAL_ERROR
            "${fixture_name} failed without '${expected_diagnostic}':\n${combined}")
    endif()
endfunction()

string(REPLACE "-DMOBA_SIM_DETERMINISTIC=1" "-DMOBA_SIM_POLICY_MISSING=1"
    missing_marker_db "${clean_db}")
expect_policy_failure(
    missing_policy_marker
    "${missing_marker_db}"
    "missing the deterministic policy marker")

string(REPLACE "/fp:precise" "/fp:fast" conflicting_fp_db "${clean_db}")
expect_policy_failure(
    conflicting_fp_option
    "${conflicting_fp_db}"
    "must receive exactly one /fp:precise option")

string(REPLACE "/fp:precise" "/fp:precise /fp:fast" duplicate_fp_db "${clean_db}")
expect_policy_failure(
    duplicate_fp_option
    "${duplicate_fp_db}"
    "must receive exactly one /fp:precise option")

string(REPLACE "/nologo" "/nologo -IC:/forbidden/engine/platform/include"
    forbidden_include_db "${clean_db}")
expect_policy_failure(
    forbidden_include_path
    "${forbidden_include_db}"
    "has forbidden include path")

string(REPLACE "/engine/sim/CMakeFiles/eng_sim.dir/"
    "/tools/sandbox/CMakeFiles/sandbox.dir/" accidental_recompile_db "${clean_db}")
expect_policy_failure(
    accidental_source_recompile
    "${accidental_recompile_db}"
    "is compiled outside eng_sim")

message(STATUS
    "eng_sim compiler policy self-test: marker, option, include, and source-owner drift rejected")
