# eng_game — the game/present glue (G37, M3.3): the SIM/PRESENTATION boundary owner.
# Owns arena-backed previous/current snapshots, interpolation, and the single
# fixed->float conversion. Platform owns fixed-step cadence. eng_game links eng_sim
# only to read SimWorld during extraction; it never calls sim_tick. It also links
# eng_render_common (the Vulkan-free DrawItem/renderer_types seam). It NEVER links
# the real Vulkan backend; the renderer never sees SimWorld.
add_library(eng_game STATIC src/present.cpp)
add_library(eng::game ALIAS eng_game)

target_include_directories(eng_game
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_game
    PUBLIC  eng::core eng::math eng::sim eng_render_common
    PRIVATE moba_warnings moba_options)
