# tools/sandbox — day-one bring-up exe (console subsystem so stdout is visible).
# M0.3: window + message loop. M2.0: also brings up the renderer (Vulkan or null).
# M4.1: the single sandbox texture is cooked as a deterministic `.mba` before
# either runnable variant can be built. The baked directory is the registry root.
set(SANDBOX_BAKED_ASSET_DIR "${CMAKE_BINARY_DIR}/content/$<CONFIG>")
set(SANDBOX_BAKED_TEXTURE "${SANDBOX_BAKED_ASSET_DIR}/uv_test.mba")

add_custom_command(
    OUTPUT "${SANDBOX_BAKED_TEXTURE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SANDBOX_BAKED_ASSET_DIR}"
    COMMAND $<TARGET_FILE:cooker>
            --input "${CMAKE_SOURCE_DIR}/assets/uv_test.tga"
            --asset "uv_test.mba"
            --output "${SANDBOX_BAKED_TEXTURE}"
    DEPENDS cooker "${CMAKE_SOURCE_DIR}/assets/uv_test.tga"
    COMMENT "Cooking sandbox texture: ${SANDBOX_BAKED_TEXTURE}"
    VERBATIM)

# ADR-0016: one typed content record (HeroDef) is cooked from its authored text
# source beside the texture so the sandbox proves the record path end to end.
set(SANDBOX_BAKED_HERO "${SANDBOX_BAKED_ASSET_DIR}/hero_test.mba")
add_custom_command(
    OUTPUT "${SANDBOX_BAKED_HERO}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SANDBOX_BAKED_ASSET_DIR}"
    COMMAND $<TARGET_FILE:cooker>
            --kind hero
            --input "${CMAKE_SOURCE_DIR}/assets/content/hero_test.txt"
            --asset "hero_test.mba"
            --output "${SANDBOX_BAKED_HERO}"
    DEPENDS cooker "${CMAKE_SOURCE_DIR}/assets/content/hero_test.txt"
    COMMENT "Cooking sandbox hero record: ${SANDBOX_BAKED_HERO}"
    VERBATIM)

# Future asset kinds extend this target instead of teaching the runtime about
# source formats.
add_custom_target(content DEPENDS "${SANDBOX_BAKED_TEXTURE}" "${SANDBOX_BAKED_HERO}")

add_executable(sandbox src/main.cpp)
target_link_libraries(sandbox PRIVATE eng::assets eng::platform eng::render eng::game moba_warnings moba_options)
target_compile_definitions(sandbox PRIVATE "MOBA_ASSET_DIR=\"${SANDBOX_BAKED_ASSET_DIR}\"")
add_dependencies(sandbox content)

add_executable(sandbox_null src/main.cpp)
target_link_libraries(sandbox_null PRIVATE eng::assets eng::platform eng::render_null eng::game moba_warnings moba_options)
target_compile_definitions(sandbox_null PRIVATE "MOBA_ASSET_DIR=\"${SANDBOX_BAKED_ASSET_DIR}\"")
add_dependencies(sandbox_null content)

# Both runnable variants participate in the M3.4 binary-parity test under the
# Debug-ASan preset, so stage the dynamic sanitizer runtime beside their shared
# output directory just as the test and replay executables do.
moba_stage_asan_runtime()
