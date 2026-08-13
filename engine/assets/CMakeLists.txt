# eng_assets - stable asset identity, arena-backed registry, and lightweight
# runtime parsers. Vulkan stays behind the injected renderer upload seam.
add_library(eng_assets STATIC
    src/asset_id.cpp
    src/assets.cpp
    src/tga.cpp
    src/wav.cpp)
add_library(eng::assets ALIAS eng_assets)

target_include_directories(eng_assets
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_assets
    PUBLIC  eng::core eng::platform
    PRIVATE moba_warnings moba_options)
