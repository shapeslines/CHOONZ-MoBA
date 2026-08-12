# eng_render — the ONLY module that sees Vulkan (ADR-0004/0006). Raw Vulkan in src/vk/
# and src/renderer.cpp, behind the renderer.h seam. Hand-loaded loader: links Vulkan
# HEADERS only, never vulkan-1.lib (ADR-0004 §3.2).
add_library(eng_render_common STATIC
    src/render.cpp
    src/render_batch.cpp
    src/render_debug_draw.cpp
    src/render_handle_table.cpp
    src/pipeline_cache_check.cpp)

target_include_directories(eng_render_common
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(eng_render_common
    PUBLIC eng::core eng::math eng::platform
    PRIVATE moba_warnings moba_options)

add_library(eng_render STATIC)
add_library(eng::render ALIAS eng_render)

target_include_directories(eng_render
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_render
    PUBLIC  eng_render_common eng::platform
    PRIVATE moba_warnings moba_options)

add_library(eng_render_null STATIC src/renderer_null.cpp)
add_library(eng::render_null ALIAS eng_render_null)
target_include_directories(eng_render_null
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(eng_render_null
    PUBLIC eng_render_common eng::platform
    PRIVATE moba_warnings moba_options)

# Real Vulkan backend when the SDK headers are present; otherwise a null backend so the
# renderer.h seam still links (CI has no SDK yet). Becomes a hard requirement once the
# SDK is installed in CI (then drop the null path and make find_package REQUIRED).
if(TARGET Vulkan::Headers)
    target_sources(eng_render PRIVATE src/vk/vk.cpp src/renderer.cpp)
    target_link_libraries(eng_render PRIVATE Vulkan::Headers)

    # M2.1: offline GLSL -> SPIR-V (ADR-0008). Outputs land in ${CMAKE_BINARY_DIR}/
    # shaders; the renderer locates them via MOBA_SHADER_DIR at runtime.
    add_shader_library(shaders SOURCES
        engine/render/shaders/mesh.vert
        engine/render/shaders/mesh.frag
        engine/render/shaders/debug_world.vert
        engine/render/shaders/debug_overlay.vert
        engine/render/shaders/debug.frag)
    add_dependencies(eng_render shaders)
    # $<CONFIG> matches add_shader_library's per-config output dir, so a Debug binary
    # always loads -g shaders and a Release binary -O ones (never last-built-wins).
    target_compile_definitions(eng_render PRIVATE
        "MOBA_SHADER_DIR=\"${CMAKE_BINARY_DIR}/shaders/$<CONFIG>\"")

    message(STATUS "eng_render: Vulkan backend (SDK ${Vulkan_VERSION})")
else()
    target_sources(eng_render PRIVATE src/renderer_null.cpp)
    message(STATUS "eng_render: NULL backend (no Vulkan SDK — set VULKAN_SDK to enable)")
endif()
