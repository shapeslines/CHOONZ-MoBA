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

# This intentionally contains only the M4.1 sandbox proof. Future asset kinds
# extend this target instead of teaching the runtime about source formats.
add_custom_target(content DEPENDS "${SANDBOX_BAKED_TEXTURE}")

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
