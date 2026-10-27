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

# M5.0: authored .mapdesc maps are already the runtime little-endian format; the
# content target copies them beside the baked texture (no cooker step yet - typed
# .mba wrapping is the content-typed-payloads slice).
set(SANDBOX_BAKED_MAP_DIR "${SANDBOX_BAKED_ASSET_DIR}/maps")
set(SANDBOX_BAKED_MAP "${SANDBOX_BAKED_MAP_DIR}/lane_test.mapdesc")
add_custom_command(
    OUTPUT "${SANDBOX_BAKED_MAP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SANDBOX_BAKED_MAP_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/maps/lane_test.mapdesc" "${SANDBOX_BAKED_MAP}"
    DEPENDS "${CMAKE_SOURCE_DIR}/assets/maps/lane_test.mapdesc"
    COMMENT "Staging authored map: ${SANDBOX_BAKED_MAP}"
    VERBATIM)

# Future asset kinds extend this target instead of teaching the runtime about
# source formats.
add_custom_target(content DEPENDS "${SANDBOX_BAKED_TEXTURE}" "${SANDBOX_BAKED_MAP}")

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
