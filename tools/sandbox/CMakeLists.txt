# tools/sandbox — day-one bring-up exe (console subsystem so stdout is visible).
# M0.3: window + message loop. M2.0: also brings up the renderer (Vulkan or null).
# M2.2: loads the UV-test TGA (tga_direct — app-layer by design; the asset module
# replaces it in M4.0) from MOBA_ASSET_DIR and uploads it for the textured quad.
add_executable(sandbox src/main.cpp src/tga_direct.cpp)
target_link_libraries(sandbox PRIVATE eng::platform eng::render moba_warnings moba_options)
target_compile_definitions(sandbox PRIVATE "MOBA_ASSET_DIR=\"${CMAKE_SOURCE_DIR}/assets\"")

add_executable(sandbox_null src/main.cpp src/tga_direct.cpp)
target_link_libraries(sandbox_null PRIVATE eng::platform eng::render_null moba_warnings moba_options)
target_compile_definitions(sandbox_null PRIVATE "MOBA_ASSET_DIR=\"${CMAKE_SOURCE_DIR}/assets\"")
