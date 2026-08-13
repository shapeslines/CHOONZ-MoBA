file(GLOB_RECURSE parser_files LIST_DIRECTORIES false
    "${SOURCE_DIR}/engine/asset_parsers/include/*"
    "${SOURCE_DIR}/engine/asset_parsers/src/*")
if(NOT parser_files)
    message(FATAL_ERROR "eng_asset_parsers has no source files")
endif()

set(forbidden_patterns
    "#include[ \t]*[<\"]platform/"
    "#include[ \t]*[<\"]render/"
    "#include[ \t]*[<\"]sim/"
    "#include[ \t]*[<\"]game/"
    "#include[ \t]*[<\"]windows.h[>\"]"
    "#include[ \t]*<(vector|string|map|unordered_map|unordered_set|filesystem|memory)>"
    "std::(vector|string|map|unordered_map|unordered_set|filesystem)"
    "(^|[^A-Za-z0-9_])(malloc|calloc|realloc|free)[ \t\r\n]*\\(")

foreach(path IN LISTS parser_files)
    file(READ "${path}" text)
    foreach(pattern IN LISTS forbidden_patterns)
        if(text MATCHES "${pattern}")
            message(FATAL_ERROR "eng_asset_parsers boundary violation in ${path}: ${pattern}")
        endif()
    endforeach()
endforeach()

file(READ "${SOURCE_DIR}/engine/asset_parsers/CMakeLists.txt" target_file)
if(NOT target_file MATCHES "PUBLIC[ \t\r\n]+eng::core[ \t\r\n]+eng::serialize")
    message(FATAL_ERROR "eng_asset_parsers must link exactly through core + serialize")
endif()
foreach(forbidden_target IN ITEMS eng::platform eng::render eng::game eng::sim eng::assets)
    if(target_file MATCHES "${forbidden_target}")
        message(FATAL_ERROR "eng_asset_parsers has forbidden dependency ${forbidden_target}")
    endif()
endforeach()

message(STATUS "eng_asset_parsers POD/dependency boundary clean")
