foreach(required IN ITEMS SOURCE_DIR WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

include("${SOURCE_DIR}/cmake/SimIsolation.cmake")

set(sim_root "C:/fixture/engine/sim")
set(valid_sources "src/a.cpp;src/b.cpp")
set(valid_links
    "eng::core;eng::math;eng::serialize;moba_warnings;moba_options;moba::sim_determinism")
set(valid_interface_links
    "eng::core;eng::math;eng::serialize;$<LINK_ONLY:moba_warnings>;$<LINK_ONLY:moba_options>;$<LINK_ONLY:moba::sim_determinism>")
set(valid_includes "${sim_root}/include;${sim_root}/src")

moba_collect_sim_contract_errors(clean_errors "${sim_root}"
    "${valid_sources}" "${valid_links}" "${valid_interface_links}" "${valid_includes}")
if(clean_errors)
    list(JOIN clean_errors "\n  " formatted)
    message(FATAL_ERROR "valid sim target contract failed:\n  ${formatted}")
endif()

function(expect_contract_error fixture_name expected sources links interface_links includes)
    moba_collect_sim_contract_errors(errors "${sim_root}"
        "${sources}" "${links}" "${interface_links}" "${includes}")
    list(JOIN errors "\n" combined)
    string(FIND "${combined}" "${expected}" diagnostic_at)
    if(diagnostic_at EQUAL -1)
        message(FATAL_ERROR
            "${fixture_name} did not report '${expected}':\n${combined}")
    endif()
endfunction()

expect_contract_error(forbidden_source "forbidden eng_sim implementation source"
    "src/a.cpp;src/../platform/time.cpp" "${valid_links}"
    "${valid_interface_links}" "${valid_includes}")
expect_contract_error(forbidden_link "forbidden direct link dependency 'eng::platform'"
    "${valid_sources}" "${valid_links};eng::platform"
    "${valid_interface_links}" "${valid_includes}")
expect_contract_error(forbidden_export "forbidden exported link dependency 'moba_options'"
    "${valid_sources}" "${valid_links}"
    "${valid_interface_links};moba_options" "${valid_includes}")
expect_contract_error(forbidden_include "forbidden direct include directory"
    "${valid_sources}" "${valid_links}" "${valid_interface_links}"
    "${valid_includes};C:/fixture/engine/render/include")

set(fixture_root "${WORK_DIR}/sim-isolation-source-fixture")
file(REMOVE_RECURSE "${fixture_root}")
file(MAKE_DIRECTORY "${fixture_root}/engine/sim/src")
file(WRITE "${fixture_root}/engine/sim/src/forbidden.cpp"
    "#include \"platform/window.h\"\n")
file(WRITE "${fixture_root}/engine/sim/CMakeLists.txt"
    "target_link_libraries(eng_sim PUBLIC eng::core eng::math eng::serialize)\n")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${fixture_root}"
            -P "${SOURCE_DIR}/tests/sim/check_sim_boundary.cmake"
    RESULT_VARIABLE boundary_result
    OUTPUT_VARIABLE boundary_stdout
    ERROR_VARIABLE boundary_stderr)
file(REMOVE_RECURSE "${fixture_root}")
if(boundary_result EQUAL 0)
    message(FATAL_ERROR "forbidden source import unexpectedly passed")
endif()
set(boundary_output "${boundary_stdout}\n${boundary_stderr}")
string(FIND "${boundary_output}" "platform/window.h" import_at)
if(import_at EQUAL -1)
    message(FATAL_ERROR
        "forbidden source import failed without actionable diagnostic:\n${boundary_output}")
endif()

message(STATUS
    "eng_sim isolation self-test: source, direct include, direct/exported link, and implementation ownership drift rejected")
