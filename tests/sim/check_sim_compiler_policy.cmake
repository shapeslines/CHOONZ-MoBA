if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
if(NOT DEFINED COMPILE_COMMANDS)
    message(FATAL_ERROR "COMPILE_COMMANDS is required")
endif()
if(NOT EXISTS "${COMPILE_COMMANDS}")
    message(FATAL_ERROR "compile command database not found: ${COMPILE_COMMANDS}")
endif()

file(READ "${SOURCE_DIR}/cmake/EngineOptions.cmake" engine_options)
file(READ "${SOURCE_DIR}/engine/sim/CMakeLists.txt" sim_cmake)

set(policy_errors "")
foreach(required IN ITEMS
        "add_library(moba_sim_determinism INTERFACE)"
        "add_library(moba::sim_determinism ALIAS moba_sim_determinism)"
        "MOBA_SIM_DETERMINISTIC=1"
        "/fp:precise")
    string(FIND "${engine_options}" "${required}" required_at)
    if(required_at EQUAL -1)
        list(APPEND policy_errors "EngineOptions.cmake is missing ${required}")
    endif()
endforeach()

string(REGEX MATCHALL "/fp:(precise|strict|fast)" owned_fp_options "${engine_options}")
list(LENGTH owned_fp_options owned_fp_count)
if(NOT owned_fp_count EQUAL 1 OR NOT "${owned_fp_options}" STREQUAL "/fp:precise")
    list(APPEND policy_errors
        "moba_sim_determinism must own exactly one /fp:precise option; found ${owned_fp_options}")
endif()

string(FIND "${sim_cmake}" "moba::sim_determinism" sim_policy_at)
if(sim_policy_at EQUAL -1)
    list(APPEND policy_errors "eng_sim does not link moba::sim_determinism")
endif()
if(sim_cmake MATCHES "/fp:(precise|strict|fast)")
    list(APPEND policy_errors "engine/sim/CMakeLists.txt owns a floating-point option directly")
endif()

file(READ "${COMPILE_COMMANDS}" compile_db)
string(JSON compile_count ERROR_VARIABLE json_error LENGTH "${compile_db}")
if(json_error)
    message(FATAL_ERROR "invalid compile command database: ${json_error}")
endif()

file(GLOB sim_sources LIST_DIRECTORIES false "${SOURCE_DIR}/engine/sim/src/*.cpp")
list(SORT sim_sources)
set(expected_configs Debug RelWithDebInfo Release)

foreach(source IN LISTS sim_sources)
    get_filename_component(source_name "${source}" NAME_WE)
    foreach(config IN LISTS expected_configs)
        set("seen_${source_name}_${config}" 0)
    endforeach()
endforeach()

set(sim_entry_count 0)
if(compile_count GREATER 0)
    math(EXPR compile_last "${compile_count} - 1")
    foreach(entry_index RANGE 0 ${compile_last})
        string(JSON entry_file GET "${compile_db}" ${entry_index} file)
        file(TO_CMAKE_PATH "${entry_file}" entry_file)
        if(NOT entry_file MATCHES "/engine/sim/src/[^/]+\\.cpp$")
            continue()
        endif()

        math(EXPR sim_entry_count "${sim_entry_count} + 1")
        string(JSON command GET "${compile_db}" ${entry_index} command)
        string(JSON output GET "${compile_db}" ${entry_index} output)
        file(TO_CMAKE_PATH "${output}" output)

        if(NOT output MATCHES "/engine/sim/CMakeFiles/eng_sim\\.dir/")
            list(APPEND policy_errors "${entry_file} is compiled outside eng_sim: ${output}")
        endif()
        if(NOT command MATCHES "(^|[ \\t])-DMOBA_SIM_DETERMINISTIC=1([ \\t]|$)")
            list(APPEND policy_errors "${entry_file} is missing the deterministic policy marker")
        endif()
        string(REGEX MATCHALL "(^|[ \\t])/fp:(precise|strict|fast)([ \\t]|$)" command_fp "${command}")
        list(LENGTH command_fp command_fp_count)
        if(NOT command_fp_count EQUAL 1 OR NOT "${command_fp}" MATCHES "/fp:precise")
            list(APPEND policy_errors
                "${entry_file} must receive exactly one /fp:precise option; found ${command_fp}")
        endif()

        set(entry_config "")
        foreach(config IN LISTS expected_configs)
            if(output MATCHES "/${config}/src/")
                set(entry_config "${config}")
            endif()
        endforeach()
        if(entry_config STREQUAL "")
            list(APPEND policy_errors "${entry_file} has an unknown configuration output: ${output}")
            continue()
        endif()

        get_filename_component(source_name "${entry_file}" NAME_WE)
        math(EXPR next_seen "${seen_${source_name}_${entry_config}} + 1")
        set("seen_${source_name}_${entry_config}" "${next_seen}")
    endforeach()
endif()

list(LENGTH sim_sources source_count)
list(LENGTH expected_configs config_count)
math(EXPR expected_entry_count "${source_count} * ${config_count}")
if(NOT sim_entry_count EQUAL expected_entry_count)
    list(APPEND policy_errors
        "expected ${expected_entry_count} eng_sim compile entries; found ${sim_entry_count}")
endif()

foreach(source IN LISTS sim_sources)
    get_filename_component(source_name "${source}" NAME_WE)
    foreach(config IN LISTS expected_configs)
        if(NOT seen_${source_name}_${config} EQUAL 1)
            list(APPEND policy_errors
                "${source_name}.cpp has ${seen_${source_name}_${config}} ${config} compile entries")
        endif()
    endforeach()
endforeach()

if(policy_errors)
    list(JOIN policy_errors "\n  " formatted)
    message(FATAL_ERROR "eng_sim compiler policy drift:\n  ${formatted}")
endif()

message(STATUS
    "eng_sim compiler policy clean: ${source_count} sources x ${config_count} configs; owner=moba_sim_determinism; fp=precise")
