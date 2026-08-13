# add_shader_library(TARGET SOURCES a.vert b.frag ...) — GLSL -> SPIR-V offline
# via glslc, with -MD/-MF depfiles so editing an included .glsl retriggers dependents
# (ADR-0008, §3.4). Output .spv lands in ${CMAKE_BINARY_DIR}/shaders, located at
# runtime via the MOBA_SHADER_DIR compile def. SPIR-V is loaded directly through the
# platform file API, NOT wrapped in the asset system.
#
# Requires the Vulkan SDK (Vulkan_GLSLC_EXECUTABLE). Not called by the M0.2 spine
# (no shaders yet); it fails loudly if invoked without glslc.

function(add_shader_library TARGET)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    if(NOT Vulkan_GLSLC_EXECUTABLE)
        message(FATAL_ERROR "add_shader_library(${TARGET}): glslc not found. "
                            "Install the Vulkan SDK (find_package(Vulkan)).")
    endif()
    # Outputs are PER-CONFIG: the glslc flags differ by config (-g vs -O), and with a
    # multi-config generator every config emits a build edge for the OUTPUT path — a
    # shared path means config switches ping-pong-rebuild and clobber each other's
    # flavor (and the runtime loads whichever was built last). Consumers must bake
    # MOBA_SHADER_DIR with the same $<CONFIG> genex so each binary loads its own.
    set(out_dir "${CMAKE_BINARY_DIR}/shaders/$<CONFIG>")
    if(CMAKE_CONFIGURATION_TYPES)
        foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
            file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders/${cfg}")
        endforeach()
    else()
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders/${CMAKE_BUILD_TYPE}")
    endif()
    set(spv_files "")
    set(shader_names "")
    file(REAL_PATH "${CMAKE_SOURCE_DIR}" source_root)
    foreach(src IN LISTS ARG_SOURCES)
        get_filename_component(shader_source "${src}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
        if(NOT EXISTS "${shader_source}" OR IS_DIRECTORY "${shader_source}")
            message(FATAL_ERROR
                "add_shader_library(${TARGET}): shader source is missing or a directory: ${src}")
        endif()
        file(REAL_PATH "${shader_source}" shader_source_real)
        file(RELATIVE_PATH shader_relative "${source_root}" "${shader_source_real}")
        if(shader_relative MATCHES "^\\.\\.(/|\\\\|$)" OR IS_ABSOLUTE "${shader_relative}")
            message(FATAL_ERROR
                "add_shader_library(${TARGET}): source must be under CMAKE_SOURCE_DIR: ${src}")
        endif()
        get_filename_component(name "${shader_source_real}" NAME)
        list(FIND shader_names "${name}" duplicate_index)
        if(NOT duplicate_index EQUAL -1)
            message(FATAL_ERROR
                "add_shader_library(${TARGET}): duplicate shader basename '${name}' from '${src}'")
        endif()
        list(APPEND shader_names "${name}")
        set(spv "${out_dir}/${name}.spv")
        # One $<IF:> so the flag is always exactly ONE argument — split genexes leave
        # an EMPTY "" arg in the non-matching config, which glslc reads as an extra
        # input file ("linking multiple files is not supported").
        add_custom_command(OUTPUT "${spv}"
            COMMAND ${Vulkan_GLSLC_EXECUTABLE}
                    "$<IF:$<CONFIG:Debug>,-g,-O>"
                    --target-env=vulkan1.3 -MD -MF "${spv}.d"
                    "${shader_source_real}" -o "${spv}"
            DEPENDS "${shader_source_real}"
            DEPFILE "${spv}.d"
            VERBATIM)
        list(APPEND spv_files "${spv}")
    endforeach()
    add_custom_target(${TARGET} ALL DEPENDS ${spv_files})
    set_target_properties(${TARGET} PROPERTIES SHADER_OUTPUT_DIR "${out_dir}")
endfunction()
