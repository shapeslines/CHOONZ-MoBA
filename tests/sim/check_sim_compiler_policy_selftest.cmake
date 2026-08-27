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

function(expect_policy_success fixture_name fixture_text)
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
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "${fixture_name} unexpectedly failed:\n${stdout}\n${stderr}")
    endif()
endfunction()

function(make_sim_outputs_relative input_db out_variable)
    string(JSON entry_count LENGTH "${input_db}")
    set(relative_db "${input_db}")
    set(changed FALSE)
    if(entry_count GREATER 0)
        math(EXPR entry_last "${entry_count} - 1")
        foreach(entry_index RANGE 0 ${entry_last})
            string(JSON entry_file GET "${relative_db}" ${entry_index} file)
            file(TO_CMAKE_PATH "${entry_file}" entry_file)
            if(NOT entry_file MATCHES "/engine/sim/src/[^/]+\\.cpp$")
                continue()
            endif()

            string(JSON entry_directory GET "${relative_db}" ${entry_index} directory)
            file(TO_CMAKE_PATH "${entry_directory}" entry_directory)
            string(JSON entry_output GET "${relative_db}" ${entry_index} output)
            file(TO_CMAKE_PATH "${entry_output}" entry_output)
            if(IS_ABSOLUTE "${entry_output}")
                file(RELATIVE_PATH relative_output "${entry_directory}" "${entry_output}")
            else()
                set(relative_output "${entry_output}")
            endif()
            file(TO_CMAKE_PATH "${relative_output}" relative_output)
            string(JSON relative_db SET "${relative_db}" ${entry_index} output "\"${relative_output}\"")
            set(changed TRUE)
        endforeach()
    endif()
    if(NOT changed)
        message(FATAL_ERROR "compile command database has no eng_sim entries to relativize")
    endif()
    set(${out_variable} "${relative_db}" PARENT_SCOPE)
endfunction()

function(move_first_sim_output_outside_target input_db out_variable)
    string(JSON entry_count LENGTH "${input_db}")
    set(wrong_target_db "${input_db}")
    set(changed FALSE)
    if(entry_count GREATER 0)
        math(EXPR entry_last "${entry_count} - 1")
        foreach(entry_index RANGE 0 ${entry_last})
            string(JSON entry_file GET "${wrong_target_db}" ${entry_index} file)
            file(TO_CMAKE_PATH "${entry_file}" entry_file)
            if(NOT entry_file MATCHES "/engine/sim/src/[^/]+\\.cpp$")
                continue()
            endif()

            set(wrong_output "tools/sandbox/CMakeFiles/sandbox.dir/Debug/src/foreign.obj")
            string(JSON wrong_target_db SET "${wrong_target_db}" ${entry_index} output "\"${wrong_output}\"")
            set(changed TRUE)
            break()
        endforeach()
    endif()
    if(NOT changed)
        message(FATAL_ERROR "compile command database has no eng_sim entry to move")
    endif()
    set(${out_variable} "${wrong_target_db}" PARENT_SCOPE)
endfunction()

make_sim_outputs_relative("${clean_db}" relative_output_db)
expect_policy_success(relative_output_acceptance "${relative_output_db}")

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

string(REPLACE "/nologo" "/nologo -I C:/forbidden/engine/platform/include"
    forbidden_include_db "${clean_db}")
expect_policy_failure(
    forbidden_include_path
    "${forbidden_include_db}"
    "has forbidden include path")

string(REPLACE "/nologo" "/nologo /external:IC:/forbidden/engine/render/include"
    forbidden_system_include_db "${clean_db}")
expect_policy_failure(
    forbidden_system_include_path
    "${forbidden_system_include_db}"
    "has forbidden include path")

move_first_sim_output_outside_target("${clean_db}" accidental_recompile_db)
expect_policy_failure(
    accidental_source_recompile
    "${accidental_recompile_db}"
    "is compiled outside eng_sim")

message(STATUS
    "eng_sim compiler policy self-test: relative output accepted; marker, option, include, and source-owner drift rejected")
