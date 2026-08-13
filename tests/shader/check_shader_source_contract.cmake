if(NOT DEFINED SOURCE_DIR OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "SOURCE_DIR and WORK_DIR are required")
endif()

set(root "${WORK_DIR}/shader-source-contract")
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY
    "${root}/source/valid"
    "${root}/source/duplicate/a"
    "${root}/source/duplicate/b"
    "${root}/source/directory.vert")
file(WRITE "${root}/source/valid/basic.vert" "#version 450\nvoid main() {}\n")
file(WRITE "${root}/source/duplicate/a/shared.vert" "#version 450\nvoid main() {}\n")
file(WRITE "${root}/source/duplicate/b/shared.vert" "#version 450\nvoid main() {}\n")
file(WRITE "${root}/source/duplicate/b/Shared.vert" "#version 450\nvoid main() {}\n")
file(WRITE "${root}/outside.vert" "#version 450\nvoid main() {}\n")
file(WRITE "${root}/source/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.28)
project(shader_source_contract NONE)
include("${COMPILE_SHADERS_FILE}")
set(Vulkan_GLSLC_EXECUTABLE "unused-glslc")
if(CASE STREQUAL "valid")
    add_shader_library(fixture SOURCES valid/basic.vert)
elseif(CASE STREQUAL "missing")
    add_shader_library(fixture SOURCES missing.vert)
elseif(CASE STREQUAL "directory")
    add_shader_library(fixture SOURCES directory.vert)
elseif(CASE STREQUAL "outside")
    add_shader_library(fixture SOURCES "${OUTSIDE_SHADER}")
elseif(CASE STREQUAL "duplicate")
    add_shader_library(fixture SOURCES duplicate/a/shared.vert duplicate/b/shared.vert)
elseif(CASE STREQUAL "duplicate-case")
    add_shader_library(fixture SOURCES duplicate/a/shared.vert duplicate/b/Shared.vert)
else()
    message(FATAL_ERROR "unknown fixture CASE: ${CASE}")
endif()
]=])

function(run_fixture case_name should_pass expected)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                -S "${root}/source"
                -B "${root}/build-${case_name}"
                "-DCASE=${case_name}"
                "-DCOMPILE_SHADERS_FILE=${SOURCE_DIR}/cmake/CompileShaders.cmake"
                "-DOUTSIDE_SHADER=${root}/outside.vert"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    set(combined "${stdout}\n${stderr}")
    if(should_pass)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "${case_name} unexpectedly failed:\n${combined}")
        endif()
    else()
        if(result EQUAL 0)
            message(FATAL_ERROR "${case_name} unexpectedly configured successfully")
        endif()
        string(FIND "${combined}" "${expected}" diagnostic_at)
        if(diagnostic_at EQUAL -1)
            message(FATAL_ERROR
                "${case_name} missed expected diagnostic '${expected}':\n${combined}")
        endif()
    endif()
endfunction()

run_fixture(valid TRUE "")
run_fixture(missing FALSE "shader source is missing or a directory")
run_fixture(directory FALSE "shader source is missing or a directory")
run_fixture(outside FALSE "source must be under CMAKE_SOURCE_DIR")
run_fixture(duplicate FALSE "duplicate shader basename")
run_fixture(duplicate-case FALSE "duplicate shader basename")

message(STATUS "shader source contract: valid plus 5 negative fixtures passed")
