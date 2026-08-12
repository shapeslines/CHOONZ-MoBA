# Engine tests (M1.4): self-registering test.h harness + CTest. Suite sources are
# compiled STRAIGHT INTO each test exe — never into a static library — because the
# linker discards object files with no referenced symbols, which would silently drop
# the TEST() registrars (see the LANDMINE note in tests/test.h). test_main.cpp owns
# main() and runs the registered cases. ${CMAKE_CURRENT_SOURCE_DIR} on the include
# path so every suite finds "test.h".

# --- Headless aggregate: the non-window/non-Vulkan engine libs the test binary links.
# eng_platform is here ONLY for its OS page-allocator backend (plat_mem_*, the one OS
# dependency core's arenas need — ADR-0005); its window/input code is never called by
# tests. As sim/net/assets land they join this group, with their OS impls kept in the
# app layer (ADR-0006) so headless testing holds. Splitting the page backend out of
# the Win32 window TU so this group is literally window-free is a later cleanup.
add_library(engine_core_group INTERFACE)
target_link_libraries(engine_core_group INTERFACE eng::core eng::math eng::platform)

# --- engine_tests: the headless aggregate suite (mem + math + containers) ---
add_executable(engine_tests
    test_main.cpp
    mem/mem_tests.cpp
    math/math_tests.cpp
    containers/container_tests.cpp
    platform/platform_tests.cpp
    render/render_tests.cpp
    tga/tga_tests.cpp
    ${CMAKE_SOURCE_DIR}/tools/sandbox/src/tga_direct.cpp)   # app-layer decoder under test (M2.2; moves to eng_assets at M4.0)
target_include_directories(engine_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/tools/sandbox/src
    ${CMAKE_SOURCE_DIR}/engine/render/src)                  # Vulkan-free render internals under test
# The aggregate suite links only the Vulkan-free renderer core; the null seam has its
# own executable below, and neither test binary initializes Vulkan.
target_link_libraries(engine_tests PRIVATE engine_core_group eng_render_common moba_warnings moba_options)

add_executable(render_null_tests test_main.cpp render/render_null_tests.cpp)
target_include_directories(render_null_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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

# --- CTest registration: one entry per suite so a red test names the right module.
# enable_testing() lives at the top level (required for add_test to register).
add_test(NAME mem         COMMAND engine_tests --suite mem)
add_test(NAME math        COMMAND engine_tests --suite math)
add_test(NAME containers  COMMAND engine_tests --suite containers)
add_test(NAME platform    COMMAND engine_tests --suite platform)  # file I/O (M2.1)
add_test(NAME render      COMMAND engine_tests --suite render)    # cache-blob checker (M2.1)
add_test(NAME render_null COMMAND render_null_tests --suite render_null)
add_test(NAME tga         COMMAND engine_tests --suite tga)       # direct TGA decode (M2.2)
add_test(NAME det_precise COMMAND det_tests)        # /fp:precise golden hash
add_test(NAME det_fast    COMMAND det_tests_fastfp) # /fp:fast    golden hash (must match)
