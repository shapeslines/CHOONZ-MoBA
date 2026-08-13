if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(READ "${SOURCE_DIR}/tools/cooker/CMakeLists.txt" target_file)
string(REGEX REPLACE "[ \t\r\n]+" " " target_normalized "${target_file}")
if(NOT target_normalized MATCHES
   "target_link_libraries\\(moba_cooker PRIVATE eng::asset_parsers eng::platform moba_warnings moba_options\\)")
    message(FATAL_ERROR "moba_cooker must link exactly asset_parsers + platform")
endif()
foreach(forbidden_target IN ITEMS eng::assets eng::render eng::render_null eng::game eng::sim)
    if(target_file MATCHES "${forbidden_target}")
        message(FATAL_ERROR "moba_cooker has forbidden dependency ${forbidden_target}")
    endif()
endforeach()

file(GLOB_RECURSE cooker_files LIST_DIRECTORIES false
    "${SOURCE_DIR}/tools/cooker/src/*.cpp"
    "${SOURCE_DIR}/tools/cooker/src/*.h")
if(NOT cooker_files)
    message(FATAL_ERROR "moba_cooker source inventory is empty")
endif()
foreach(path IN LISTS cooker_files)
    file(READ "${path}" text)
    if(text MATCHES "#[ \t]*include[ \t]*[<\"](render/|game/|sim/|assets/assets\\.h|vulkan)" OR
       text MATCHES "renderer_(create|destroy)" OR text MATCHES "Vulkan::")
        message(FATAL_ERROR "moba_cooker boundary violation in ${path}")
    endif()
endforeach()

foreach(module IN ITEMS render sim)
    file(GLOB_RECURSE module_files LIST_DIRECTORIES false
        "${SOURCE_DIR}/engine/${module}/CMakeLists.txt"
        "${SOURCE_DIR}/engine/${module}/include/*"
        "${SOURCE_DIR}/engine/${module}/src/*")
    foreach(path IN LISTS module_files)
        file(READ "${path}" text)
        if(text MATCHES "eng::assets|eng::asset_parsers" OR
           text MATCHES "#[ \t]*include[ \t]*[<\"]assets/")
            message(FATAL_ERROR "${module} gained an asset-pipeline dependency in ${path}")
        endif()
    endforeach()
endforeach()

message(STATUS "cooker boundary: asset_parsers + platform only; render/sim remain independent")
