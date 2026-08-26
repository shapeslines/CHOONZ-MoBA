if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()
include("${SOURCE_DIR}/cmake/AssetContent.cmake")

set(valid_paths
    "uv_test.tga"
    "audio/ui-confirm_1.wav"
    "textures/arena.floor-01.tga")
foreach(path IN LISTS valid_paths)
    moba_asset_path_is_canonical("${path}" accepted)
    if(NOT accepted)
        message(FATAL_ERROR "canonical asset path rejected: '${path}'")
    endif()
endforeach()

set(invalid_paths
    ""
    "UV_TEST.TGA"
    "../escape.tga"
    "textures//escape.tga"
    "textures/./escape.tga"
    "textures/con.tga"
    "textures/name./escape.tga"
    "unsupported.png"
    "p]]) file(WRITE [[moba-cmake-injected.txt]] [[owned]]) #/escape.tga")
foreach(path IN LISTS invalid_paths)
    moba_asset_path_is_canonical("${path}" accepted)
    if(accepted)
        message(FATAL_ERROR "unsafe asset path accepted: '${path}'")
    endif()
endforeach()

file(READ "${SOURCE_DIR}/tools/cooker/CMakeLists.txt" cooker_cmake)
if(cooker_cmake MATCHES "file\\(GENERATE" OR
   cooker_cmake MATCHES "_moba_prepare_script" OR
   cooker_cmake MATCHES "cmake-injected")
    message(FATAL_ERROR "cooker build still generates executable CMake from asset paths")
endif()
message(STATUS "asset content path policy rejects executable-path injection")
