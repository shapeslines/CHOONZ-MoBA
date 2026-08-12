# eng_sim — deterministic fixed-point simulation. Platform, renderer, wall-clock,
# and presentation dependencies are forbidden by the link graph.
add_library(eng_sim STATIC src/sim.cpp)
add_library(eng::sim ALIAS eng_sim)

target_include_directories(eng_sim
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_sim
    PUBLIC  eng::core eng::math eng::serialize
    PRIVATE moba_warnings moba_options)

target_compile_options(eng_sim PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/fp:precise>)
