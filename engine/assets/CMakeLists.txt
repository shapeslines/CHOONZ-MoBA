# eng_assets - arena-backed registry/lifetimes and runtime loading. Source parsers
# and the .mba codec live in the POD-only eng_asset_parsers module.
add_library(eng_assets STATIC
    src/assets.cpp
    src/wav.cpp)
add_library(eng::assets ALIAS eng_assets)

target_include_directories(eng_assets
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_assets
    PUBLIC  eng::core eng::platform eng::asset_parsers
    PRIVATE moba_warnings moba_options)
