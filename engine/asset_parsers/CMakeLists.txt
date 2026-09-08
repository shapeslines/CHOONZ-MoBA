# eng_asset_parsers - POD-only source parser and baked-container boundary shared by
# the runtime and offline tools. It remains OS-, GPU-, and STL-free.
add_library(eng_asset_parsers STATIC
    src/asset_id.cpp
    src/content.cpp
    src/mba.cpp
    src/tga.cpp)
add_library(eng::asset_parsers ALIAS eng_asset_parsers)

target_include_directories(eng_asset_parsers
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_asset_parsers
    PUBLIC  eng::core eng::serialize
    PRIVATE moba_warnings moba_options)
