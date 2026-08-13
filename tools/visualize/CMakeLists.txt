# tools/visualize — headless BMP dumps of the fixed-point math (fix_sin/cos/sqrt vs
# libm), so the determinism suite's numbers become images you can open. The fix API is
# header-only (inline + LUT), so this links eng::math mainly for its include path.
add_executable(visualize src/main.cpp)
target_link_libraries(visualize PRIVATE eng::math moba_warnings moba_options)

add_test(NAME visualize_output_path_guard
    COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/tests/visualize/visualize_output_path_guard.ps1"
            -Exe $<TARGET_FILE:visualize>
            -WorkDir "${CMAKE_CURRENT_BINARY_DIR}")
