# moba_cooker - offline source parser and deterministic .mba/catalog publisher.
# STL is private to this tool; all shared parser/codec headers remain POD-only.
add_executable(moba_cooker src/main.cpp)
target_link_libraries(moba_cooker PRIVATE
    eng::asset_parsers eng::platform moba_warnings moba_options)

# Configuration-local content is a first-class build product. The generated
# catalog is the custom command's commit marker; the cooker publishes every .mba
# first and writes this header only after all asset files succeed.
set(MOBA_BAKED_ROOT "${CMAKE_BINARY_DIR}/baked/$<CONFIG>")
set(MOBA_GENERATED_ROOT "${CMAKE_BINARY_DIR}/generated/$<CONFIG>")
set(MOBA_GENERATED_ASSET_HEADER
    "${MOBA_GENERATED_ROOT}/assets/asset_ids.gen.h")

set(_moba_content_sources "")
set(_moba_baked_outputs "")
set(_moba_content_directories
    "${MOBA_BAKED_ROOT}"
    "${MOBA_GENERATED_ROOT}/assets")
set(_moba_baked_parents "")
foreach(asset_path IN LISTS MOBA_CONTENT_SOURCE_FILES)
    list(APPEND _moba_content_sources "${CMAKE_SOURCE_DIR}/assets/${asset_path}")
    list(APPEND _moba_baked_outputs "${MOBA_BAKED_ROOT}/${asset_path}.mba")
    get_filename_component(asset_parent "${asset_path}" DIRECTORY)
    if(asset_parent)
        list(APPEND _moba_baked_parents "${asset_parent}")
    endif()
endforeach()
list(REMOVE_DUPLICATES _moba_baked_parents)
foreach(asset_parent IN LISTS _moba_baked_parents)
    list(APPEND _moba_content_directories
        "${MOBA_BAKED_ROOT}/${asset_parent}")
endforeach()
list(REMOVE_DUPLICATES _moba_content_directories)

add_custom_command(
    OUTPUT "${MOBA_GENERATED_ASSET_HEADER}"
    BYPRODUCTS ${_moba_baked_outputs}
    COMMAND "${CMAKE_COMMAND}" -E make_directory ${_moba_content_directories}
    COMMAND $<TARGET_FILE:moba_cooker>
            --source-root "${CMAKE_SOURCE_DIR}/assets"
            --manifest "${MOBA_ASSET_MANIFEST}"
            --out-root "${MOBA_BAKED_ROOT}"
            --generated-root "${MOBA_GENERATED_ROOT}"
    DEPENDS moba_cooker "${MOBA_ASSET_MANIFEST}" ${_moba_content_sources}
    COMMENT "Cooking deterministic $<CONFIG> asset catalog"
    VERBATIM)
add_custom_target(content DEPENDS "${MOBA_GENERATED_ASSET_HEADER}")

# Consumers link this interface to inherit both the generation order and the
# exact config-local catalog include/root. A future game executable uses the same
# gate as both sandbox variants without duplicating path policy.
add_library(moba_content INTERFACE)
add_library(moba::content ALIAS moba_content)
add_dependencies(moba_content content)
target_include_directories(moba_content INTERFACE "${MOBA_GENERATED_ROOT}")
target_compile_definitions(moba_content INTERFACE
    "MOBA_ASSET_DIR=\"${MOBA_BAKED_ROOT}\"")

moba_stage_asan_runtime()
