# eng_sim — deterministic fixed-point simulation. Platform, renderer, wall-clock,
# and presentation dependencies are forbidden by the link graph.
add_library(eng_sim STATIC
    src/combat.cpp
    src/command.cpp
    src/component_pool.cpp
    src/components.cpp
    src/entity.cpp
    src/events.cpp
    src/hero.cpp
    src/map.cpp
    src/oracle.cpp
    src/replay.cpp
    src/sim.cpp
    src/sim_hash.cpp
    src/systems.cpp)
add_library(eng::sim ALIAS eng_sim)

target_include_directories(eng_sim
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_sim
    PUBLIC  eng::core eng::math eng::serialize
    PRIVATE moba_warnings moba_options moba::sim_determinism)

moba_enforce_sim_target(eng_sim "${CMAKE_CURRENT_SOURCE_DIR}")
