# Engine tests (M1.4): self-registering test.h harness + CTest. Suite sources are
# compiled STRAIGHT INTO each test exe — never into a static library — because the
# linker discards object files with no referenced symbols, which would silently drop
# the TEST() registrars (see the LANDMINE note in tests/test.h). test_main.cpp owns
# main() and runs the registered cases. ${CMAKE_CURRENT_SOURCE_DIR} on the include
# path so every suite finds "test.h".

# --- Headless aggregate: the non-window/non-Vulkan engine libs the test binary links.
# eng_platform is here ONLY for its OS page-allocator + file-I/O backends (plat_mem_*,
# platform_file_*, the OS deps core's arenas and the file suites need — ADR-0005);
# those live in their own TUs (win32_mem.cpp/win32_file.cpp/win32_log.cpp, G14), so
# the window TU is never pulled into the test executables. As sim/net/assets land
# they join this group, with their OS impls kept in the app layer (ADR-0006) so
# headless testing holds.
add_library(engine_core_group INTERFACE)
target_link_libraries(engine_core_group INTERFACE
    eng::core eng::math eng::serialize eng::assets eng::sim eng::platform)

# --- engine_tests: the headless aggregate suite (mem + math + containers) ---
add_executable(engine_tests
    test_main.cpp
    mem/mem_tests.cpp
    math/math_tests.cpp
    containers/container_tests.cpp
    assets/asset_id_tests.cpp
    assets/asset_registry_tests.cpp
    assets/content_tests.cpp
    assets/mba_tests.cpp
    platform/platform_tests.cpp
    serialize/serialize_tests.cpp
    sim/component_pool_tests.cpp
    sim/components_tests.cpp
    sim/deferred_destroy_tests.cpp
    sim/entity_tests.cpp
    sim/events_tests.cpp
    sim/sim_config_tests.cpp
    sim/sim_hash_tests.cpp
    sim/replay_tests.cpp
    sim/schedule_tests.cpp
    sim/sim_tests.cpp
    sim/systems_tests.cpp
    present/present_tests.cpp          # M3.3: cadence integration + snapshot/present glue
    render/render_tests.cpp
    tga/tga_tests.cpp
    wav/wav_tests.cpp)
target_include_directories(engine_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/engine/render/src
    ${CMAKE_SOURCE_DIR}/engine/platform/src/win32)         # Vulkan-free/internal boundaries under test
# The aggregate suite links only the Vulkan-free renderer core; the null seam has its
# own executable below, and neither test binary initializes Vulkan.
target_link_libraries(engine_tests PRIVATE engine_core_group eng_render_common eng::game moba_warnings moba_options)

add_executable(render_null_tests test_main.cpp render/render_null_tests.cpp)
target_include_directories(render_null_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/engine/render/src)
target_link_libraries(render_null_tests PRIVATE eng::render_null moba_warnings moba_options)

# --- determinism: the SAME source built twice under different float models. The
#     golden hash is integer-only, so /fp:precise and /fp:fast must agree (ADR-0002).
add_executable(det_tests test_main.cpp det/det_tests.cpp)
target_include_directories(det_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(det_tests PRIVATE eng::math moba_warnings moba_options)
target_compile_options(det_tests PRIVATE /fp:precise)

add_executable(det_tests_fastfp test_main.cpp det/det_tests.cpp)
target_include_directories(det_tests_fastfp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(det_tests_fastfp PRIVATE eng::math moba_warnings moba_options)
target_compile_options(det_tests_fastfp PRIVATE /fp:fast)

# M3.0 integration proof links eng_sim directly, so the executable itself demonstrates
# that the 10,000-tick oracle needs no platform, window, renderer, or Vulkan target.
add_executable(sim_determinism_tests test_main.cpp sim/sim_determinism_tests.cpp)
target_include_directories(sim_determinism_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sim_determinism_tests PRIVATE eng::sim moba_warnings moba_options)

add_executable(sim_oracle_probe sim/sim_oracle_probe.cpp)
target_link_libraries(sim_oracle_probe PRIVATE eng::sim moba_warnings moba_options)

add_executable(core_invariant_tripwire core/core_invariant_tripwire.cpp)
target_link_libraries(core_invariant_tripwire PRIVATE eng::core moba_warnings moba_options)

if(MOBA_UBSAN)
    add_executable(sim_ubsan_tripwire sim/sim_ubsan_tripwire.cpp)
    target_link_libraries(sim_ubsan_tripwire PRIVATE moba_warnings moba_options)
endif()

# App-local MSVC ASan runtime for every test executable in this output directory.
moba_stage_asan_runtime()

# --- CTest registration: one entry per suite so a red test names the right module.
# enable_testing() lives at the top level (required for add_test to register).
add_test(NAME mem         COMMAND engine_tests --suite mem)
add_test(NAME math        COMMAND engine_tests --suite math)
add_test(NAME containers  COMMAND engine_tests --suite containers)
add_test(NAME asset_id    COMMAND engine_tests --suite asset_id)
add_test(NAME assets      COMMAND engine_tests --suite assets)
add_test(NAME mba         COMMAND engine_tests --suite mba)
add_test(NAME content     COMMAND engine_tests --suite content)   # ADR-0016 typed records
add_test(NAME assets_boundary
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/assets/check_assets_boundary.cmake")
add_test(NAME asset_parsers_boundary
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/assets/check_asset_parsers_boundary.cmake")
add_test(NAME core_invariant_tripwire
    COMMAND ${CMAKE_COMMAND}
            -DTRIPWIRE_EXE=$<TARGET_FILE:core_invariant_tripwire>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/core/check_core_invariant_tripwire.cmake")
add_test(NAME platform    COMMAND engine_tests --suite platform)  # file I/O (M2.1)
add_test(NAME serialize   COMMAND engine_tests --suite serialize)
add_test(NAME sim_component_pool COMMAND engine_tests --suite sim_component_pool)
add_test(NAME sim_components COMMAND engine_tests --suite sim_components)
add_test(NAME sim_destroy COMMAND engine_tests --suite sim_destroy)
add_test(NAME sim_entity  COMMAND engine_tests --suite sim_entity)
add_test(NAME sim_events  COMMAND engine_tests --suite sim_events)
add_test(NAME sim         COMMAND engine_tests --suite sim)
add_test(NAME sim_schedule COMMAND engine_tests --suite sim_schedule)
add_test(NAME sim_systems COMMAND engine_tests --suite sim_systems)
add_test(NAME present COMMAND engine_tests --suite present)  # M3.3 cadence/snapshot/glue
add_test(NAME render      COMMAND engine_tests --suite render)    # cache-blob checker (M2.1)
add_test(NAME render_null COMMAND render_null_tests --suite render_null)
add_test(NAME tga         COMMAND engine_tests --suite tga)       # direct TGA decode (M2.2)
add_test(NAME wav         COMMAND engine_tests --suite wav)
add_test(NAME det_precise COMMAND det_tests)        # /fp:precise golden hash
add_test(NAME det_fast    COMMAND det_tests_fastfp) # /fp:fast    golden hash (must match)
add_test(NAME sim_determinism COMMAND sim_determinism_tests --suite sim_determinism)
set_tests_properties(sim_determinism PROPERTIES
    PASS_REGULAR_EXPRESSION "controlled divergence tick=4321 field=position_x entity=7")

add_test(NAME sim_binary_parity
    COMMAND ${CMAKE_COMMAND}
            -DTEST_EXE=$<TARGET_FILE:sim_oracle_probe>
            -DGAME_EXE=$<TARGET_FILE:sandbox>
            -DGAME_NULL_EXE=$<TARGET_FILE:sandbox_null>
            -P "${CMAKE_CURRENT_SOURCE_DIR}/sim/check_sim_binary_parity.cmake")

if(MOBA_UBSAN)
    add_test(NAME sim_ubsan_oracle
        COMMAND sim_oracle_probe --sim-self-check)
    set_tests_properties(sim_ubsan_oracle PROPERTIES
        PASS_REGULAR_EXPRESSION
            "sim_oracle ticks=10000 commands=923 final=0x637628abff59c823 stream=0x6f381609f7e59f0c logic=0xab96814425ba80a4")
    add_test(NAME sim_ubsan_tripwire
        COMMAND ${CMAKE_COMMAND}
                -DTRIPWIRE_EXE=$<TARGET_FILE:sim_ubsan_tripwire>
                -P "${CMAKE_CURRENT_SOURCE_DIR}/sim/check_sim_ubsan_tripwire.cmake")
endif()

add_test(NAME sim_boundary
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/sim/check_sim_boundary.cmake")

add_test(NAME sim_compiler_policy
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DCOMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json
            -P "${CMAKE_CURRENT_SOURCE_DIR}/sim/check_sim_compiler_policy.cmake")

add_test(NAME sim_compiler_policy_selftest
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DCOMPILE_COMMANDS=${CMAKE_BINARY_DIR}/compile_commands.json
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/sim/check_sim_compiler_policy_selftest.cmake")

add_test(NAME sim_isolation_selftest
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/sim/check_sim_isolation_selftest.cmake")

add_test(NAME present_boundary
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/present/check_present_boundary.cmake")

add_test(NAME sandbox_smoke_classifier
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/platform/sandbox_smoke_classifier_tests.ps1"
            -Classifier "${CMAKE_SOURCE_DIR}/tools/classify-sandbox-smoke.ps1"
            -WorkDir "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(sandbox_smoke_classifier PROPERTIES
    PASS_REGULAR_EXPRESSION "sandbox smoke classifier: 15 adversarial cases passed")

add_test(NAME sandbox_cli_errors
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/sandbox_cli_errors.ps1"
            -Exe $<TARGET_FILE:sandbox_null>
            -WorkDir "${CMAKE_CURRENT_BINARY_DIR}")

add_test(NAME test_harness_cli_errors
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/harness_cli_errors.ps1"
            -Exe $<TARGET_FILE:engine_tests>
            -WorkDir "${CMAKE_CURRENT_BINARY_DIR}")

# --- replay CLI: one fixture records a real file; consumers independently inspect,
#     verify, and exercise exact public exit classes before cleanup runs.
set(REPLAY_FIXTURE_FILE "${CMAKE_CURRENT_BINARY_DIR}/moba_replay_fixture.mbr")
add_test(NAME replay_record
    COMMAND $<TARGET_FILE:moba_replay> record --out "${REPLAY_FIXTURE_FILE}"
            --ticks 10000 --seed 1 --players 2)
set_tests_properties(replay_record PROPERTIES
    FIXTURES_SETUP replay_file
    PASS_REGULAR_EXPRESSION "recorded ticks=10000")

add_test(NAME replay_inspect
    COMMAND $<TARGET_FILE:moba_replay> inspect "${REPLAY_FIXTURE_FILE}")
set_tests_properties(replay_inspect PROPERTIES
    FIXTURES_REQUIRED replay_file
    PASS_REGULAR_EXPRESSION "logic_hash=0xab96814425ba80a4")

add_test(NAME replay_verify
    COMMAND $<TARGET_FILE:moba_replay> verify "${REPLAY_FIXTURE_FILE}")
set_tests_properties(replay_verify PROPERTIES
    FIXTURES_REQUIRED replay_file
    PASS_REGULAR_EXPRESSION "verified ticks=10000")

add_test(NAME replay_cli_errors
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/replay/replay_cli_errors.ps1"
            -Exe $<TARGET_FILE:moba_replay>
            -Replay "${REPLAY_FIXTURE_FILE}"
            -WorkDir "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(replay_cli_errors PROPERTIES FIXTURES_REQUIRED replay_file)

add_test(NAME replay_cleanup
    COMMAND ${CMAKE_COMMAND} -E rm -f "${REPLAY_FIXTURE_FILE}")
set_tests_properties(replay_cleanup PROPERTIES FIXTURES_CLEANUP replay_file)

add_test(NAME fresh_walk_path_guard
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/fresh_walk_path_guard.ps1"
            -Script "${CMAKE_SOURCE_DIR}/tools/fresh-walk.ps1"
            -GuardScript "${CMAKE_SOURCE_DIR}/tools/fresh-walk-guard.ps1"
            -OutsideDir "${CMAKE_SOURCE_DIR}")

add_test(NAME ci_workflow_contract
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/ci_workflow_contract.ps1"
            -Workflow "${CMAKE_SOURCE_DIR}/.github/workflows/ci.yml")

add_test(NAME local_ci_runner_contract
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/local_ci_runner_contract.ps1"
            -Runner "${CMAKE_SOURCE_DIR}/tools/local-ci.ps1"
            -Gate "${CMAKE_SOURCE_DIR}/tools/hooks/ctest-gate.bat"
            -Readme "${CMAKE_SOURCE_DIR}/README.md"
            -PullRequestTemplate "${CMAKE_SOURCE_DIR}/.github/PULL_REQUEST_TEMPLATE.md")

add_test(NAME shader_source_contract
    COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/shader/check_shader_source_contract.cmake")

add_test(NAME cooker_cli
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/assets/cooker_cli_tests.ps1"
            -Exe $<TARGET_FILE:cooker>
            -ContentDir "${CMAKE_SOURCE_DIR}/assets/content"
            -WorkDir "${CMAKE_CURRENT_BINARY_DIR}")

# This is the M4.1 vertical proof: invoke CMake's content target, then compare its
# sandbox artifact to repeated direct bakes of the checked-in source texture.
add_test(NAME sandbox_baked_content
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/assets/sandbox_baked_content_tests.ps1"
            -Cmake "${CMAKE_COMMAND}"
            -BuildDir "${CMAKE_BINARY_DIR}"
            -Configuration "$<CONFIG>"
            -Cooker $<TARGET_FILE:cooker>
            -Source "${CMAKE_SOURCE_DIR}/assets/uv_test.tga"
            -Baked "${CMAKE_BINARY_DIR}/content/$<CONFIG>/uv_test.mba"
            -RecordSource "${CMAKE_SOURCE_DIR}/assets/content/hero_test.txt"
            -RecordBaked "${CMAKE_BINARY_DIR}/content/$<CONFIG>/hero_test.mba"
            -RecordGolden "${CMAKE_SOURCE_DIR}/assets/content/golden/hero_test.mba"
            -WorkDir "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(sandbox_baked_content PROPERTIES
    PASS_REGULAR_EXPRESSION "sandbox baked content: CMake output matches deterministic cooker bytes")
