if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(parser_dir "${SOURCE_DIR}/engine/asset_parsers")
file(GLOB_RECURSE parser_files LIST_DIRECTORIES false
    "${parser_dir}/*.h" "${parser_dir}/*.cpp" "${parser_dir}/CMakeLists.txt")
if(NOT parser_files)
    message(FATAL_ERROR "eng_asset_parsers source inventory is empty")
endif()

foreach(path IN LISTS parser_files)
    file(READ "${path}" text)
    if(text MATCHES "std::" OR text MATCHES "#[ \t]*include[ \t]*<(vector|string|map|memory|filesystem)>" OR
       text MATCHES "#[ \t]*include[ \t]*[<\"](platform/|render/|sim/|assets/assets\.h)")
        message(FATAL_ERROR "asset parser boundary is not POD/engine-tool safe: ${path}")
    endif()
endforeach()

file(READ "${parser_dir}/CMakeLists.txt" parser_cmake)
if(NOT parser_cmake MATCHES "add_library\\(eng_asset_parsers" OR
   NOT parser_cmake MATCHES "moba_options" OR
   parser_cmake MATCHES "eng::(platform|render|sim|assets)")
    message(FATAL_ERROR "eng_asset_parsers target contract is incomplete")
endif()

message(STATUS "asset parser boundary: POD-only and OS/GPU/STL-free")
