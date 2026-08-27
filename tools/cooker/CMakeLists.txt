# cooker.exe - offline TGA-to-.mba baker. STL remains private to this executable;
# its only shared boundary is the POD-only eng_asset_parsers library.
add_executable(cooker src/main.cpp)
target_link_libraries(cooker PRIVATE eng::asset_parsers moba_warnings)
# The engine-wide CMake setup removes its default /EHsc so engine targets can opt
# into ADR-0009's no-exception policy. This offline-only tool is the deliberate
# exception: its private STL file I/O needs normal C++ unwind support.
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(cooker PRIVATE /EHsc)
endif()
moba_stage_asan_runtime()
