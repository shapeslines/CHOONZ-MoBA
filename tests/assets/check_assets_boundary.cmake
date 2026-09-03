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
endforeach()

file(READ "${SOURCE_DIR}/engine/assets/src/assets.cpp" asset_runtime)
if(NOT asset_runtime MATCHES "platform_file_read_rooted" OR
   asset_runtime MATCHES "platform_file_size[ \t\r\n]*\\(" OR
   NOT asset_runtime MATCHES "AssetHandle asset_load[ \t\r\n]*\\(" OR
   NOT asset_runtime MATCHES "mba_inspect")
    message(FATAL_ERROR "asset loads must use the single-handle rooted read seam")
endif()

if(EXISTS "${SOURCE_DIR}/tools/sandbox/src/tga_direct.cpp" OR
   EXISTS "${SOURCE_DIR}/tools/sandbox/src/tga_direct.h")
    message(FATAL_ERROR "provisional sandbox TGA parser still exists")
endif()

file(READ "${SOURCE_DIR}/tools/sandbox/src/main.cpp" sandbox)
if(NOT sandbox MATCHES "asset_load[ \t\r\n]*\\(" OR
   NOT sandbox MATCHES "uv_test\\.mba" OR
   sandbox MATCHES "asset_load_texture_tga[ \t\r\n]*\\(" OR
   NOT sandbox MATCHES "asset_registry_shutdown" OR
   sandbox MATCHES "tga_decode[ \t\r\n]*\\(")
    message(FATAL_ERROR "sandbox does not exclusively use the baked AssetRegistry texture path")
endif()

file(READ "${SOURCE_DIR}/tools/sandbox/CMakeLists.txt" sandbox_cmake)
if(NOT sandbox_cmake MATCHES "add_custom_command\\(" OR
   NOT sandbox_cmake MATCHES "add_custom_target\\(content" OR
   NOT sandbox_cmake MATCHES "assets/uv_test\\.tga" OR
   NOT sandbox_cmake MATCHES "uv_test\\.mba" OR
   NOT sandbox_cmake MATCHES "\\$<TARGET_FILE:cooker>" OR
   NOT sandbox_cmake MATCHES "add_dependencies\\(sandbox content\\)" OR
   NOT sandbox_cmake MATCHES "add_dependencies\\(sandbox_null content\\)")
    message(FATAL_ERROR "sandbox lacks the CMake-baked M4.1 texture dependency")
endif()

message(STATUS "asset boundary: core/platform only, no Vulkan/render/sim dependency")
