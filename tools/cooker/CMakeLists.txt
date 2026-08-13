# moba_cooker - offline source parser and deterministic .mba/catalog publisher.
# STL is private to this tool; all shared parser/codec headers remain POD-only.
add_executable(moba_cooker src/main.cpp)
target_link_libraries(moba_cooker PRIVATE
    eng::asset_parsers eng::platform moba_warnings moba_options)

moba_stage_asan_runtime()
