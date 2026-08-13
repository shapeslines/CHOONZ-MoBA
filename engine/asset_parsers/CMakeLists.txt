# eng_asset_parsers - POD-only source parsers, stable identity, and the shared
# allocation-free .mba codec. This module is OS/GPU-free and may be linked by both
# runtime engine code and offline tools without exposing STL types.
add_library(eng_asset_parsers STATIC
    src/asset_id.cpp
    src/mba.cpp
    src/tga.cpp
    src/wav.cpp)
add_library(eng::asset_parsers ALIAS eng_asset_parsers)

target_include_directories(eng_asset_parsers
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_asset_parsers
    PUBLIC  eng::core eng::serialize
    PRIVATE moba_warnings moba_options)
