# eng_serialize — allocation-free, bounded little-endian byte I/O shared by replay,
# networking, and binary assets (ADR-0006).
add_library(eng_serialize STATIC src/byte_io.cpp)
add_library(eng::serialize ALIAS eng_serialize)

target_include_directories(eng_serialize
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_serialize
    PUBLIC  eng::core
    PRIVATE moba_warnings moba_options)
