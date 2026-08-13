if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(assets_dir "${SOURCE_DIR}/engine/assets")
file(GLOB_RECURSE asset_files LIST_DIRECTORIES false
    "${assets_dir}/*.h" "${assets_dir}/*.cpp" "${assets_dir}/CMakeLists.txt")
if(NOT asset_files)
    message(FATAL_ERROR "eng_assets source inventory is empty")
endif()

foreach(path IN LISTS asset_files)
    file(READ "${path}" text)
    if(text MATCHES "#[ \t]*include[ \t]*[<\"](render/|vulkan)" OR
       text MATCHES "renderer_(create|destroy)" OR
       text MATCHES "Vulkan::" OR text MATCHES "eng::render" OR
       text MATCHES "#[ \t]*include[ \t]*[<\"]sim/")
        message(FATAL_ERROR "asset boundary violation in ${path}")
    endif()
    if(text MATCHES "#[ \t]*include[ \t]*<(vector|string|map|unordered_map|unordered_set|filesystem|memory|new)>" OR
       text MATCHES "std::" OR
       text MATCHES "(^|[^A-Za-z0-9_])(malloc|calloc|realloc|free)[ \t\r\n]*\\(" OR
       text MATCHES "(^|[^A-Za-z0-9_])new[ \t\r\n]+" OR
       text MATCHES "(^|[^A-Za-z0-9_])delete[ \t\r\n]+")
        message(FATAL_ERROR "asset runtime STL/heap boundary violation in ${path}")
    endif()
endforeach()

file(READ "${SOURCE_DIR}/engine/assets/src/assets.cpp" asset_runtime)
if(NOT asset_runtime MATCHES "platform_file_read_rooted" OR
   asset_runtime MATCHES "platform_file_size[ \t\r\n]*\\(" OR
   asset_runtime MATCHES "asset_load_(texture_tga|sound_wav)" OR
   asset_runtime MATCHES "#[ \t]*include[ \t]*[<\"]assets/(tga|wav)\\.h" OR
   asset_runtime MATCHES "(^|[^A-Za-z0-9_])(tga_|wav_)" OR
   asset_runtime MATCHES "\\.(tga|wav)[\"]" OR
   asset_runtime MATCHES "byte_(writer|reader)|serialize/" OR
   asset_runtime MATCHES "(^|[^A-Za-z0-9_])(fopen|fread|fwrite|ifstream|ofstream)[ \t\r\n]*(\\(|<)")
    message(FATAL_ERROR "asset loads must use the single-handle rooted read seam")
endif()

file(READ "${SOURCE_DIR}/engine/assets/CMakeLists.txt" target_file)
string(REGEX REPLACE "[ \t\r\n]+" " " target_normalized "${target_file}")
if(NOT target_normalized MATCHES
   "target_link_libraries\\(eng_assets PUBLIC eng::core eng::platform eng::asset_parsers PRIVATE moba_warnings moba_options\\)")
    message(FATAL_ERROR "eng_assets must link exactly core + platform + asset_parsers")
endif()

if(EXISTS "${SOURCE_DIR}/tools/sandbox/src/tga_direct.cpp" OR
   EXISTS "${SOURCE_DIR}/tools/sandbox/src/tga_direct.h")
    message(FATAL_ERROR "provisional sandbox TGA parser still exists")
endif()

file(READ "${SOURCE_DIR}/tools/sandbox/src/main.cpp" sandbox)
if(NOT sandbox MATCHES "#[ \t]*include[ \t]*[\"]assets/asset_ids\\.gen\\.h" OR
   NOT sandbox MATCHES "MOBA_ASSET_CATALOG" OR
   NOT sandbox MATCHES "ASSET_UV_TEST_TGA" OR
   NOT sandbox MATCHES "asset_load[ \t\r\n]*\\(" OR
   NOT sandbox MATCHES "asset_registry_shutdown" OR
   sandbox MATCHES "asset_register_texture[ \t\r\n]*\\(" OR
   sandbox MATCHES "asset_load_(texture_tga|sound_wav)" OR
   sandbox MATCHES "tga_decode[ \t\r\n]*\\(")
    message(FATAL_ERROR "sandbox does not exclusively use generated baked assets")
endif()

message(STATUS "asset runtime boundary: core + platform + asset_parsers, baked-only and heap-free")
