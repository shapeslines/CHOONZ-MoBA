# tools/sandbox — day-one bring-up exe (console subsystem so stdout is visible).
# M0.3: window + message loop. M2.0: also brings up the renderer (Vulkan or null).
# M4.0: consumes the promoted eng_assets parser; the next vertical slice routes
# texture identity and lifetime through AssetRegistry as well.
add_executable(sandbox src/main.cpp)
target_link_libraries(sandbox PRIVATE eng::assets eng::platform eng::render eng::game
    moba::content moba_warnings moba_options)

add_executable(sandbox_null src/main.cpp)
target_link_libraries(sandbox_null PRIVATE eng::assets eng::platform eng::render_null eng::game
    moba::content moba_warnings moba_options)

# Both runnable variants participate in the M3.4 binary-parity test under the
# Debug-ASan preset, so stage the dynamic sanitizer runtime beside their shared
# output directory just as the test and replay executables do.
moba_stage_asan_runtime()
