# moba_options — the ONE flag string for the whole project (ADR-0009, §3.3).
# No exceptions, no RTTI; per-config defines; explicit Release whole-program opt.
# Linked PRIVATE to every one of our targets.

add_library(moba_options INTERFACE)

# No RTTI, no exceptions (ADR-0009). _HAS_EXCEPTIONS=0 stops the STL dragging in
# exception machinery under /EHs-c-.
target_compile_options(moba_options INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/GR- /EHs-c->)

target_compile_definitions(moba_options INTERFACE
    _HAS_EXCEPTIONS=0
    _CRT_SECURE_NO_WARNINGS
    $<$<CONFIG:Debug>:MOBA_DEBUG=1>
    $<$<CONFIG:RelWithDebInfo>:MOBA_DEV=1>
    $<$<CONFIG:Release>:MOBA_RELEASE=1>)

# Release whole-program optimization must be explicit AND uniform: CMake's default
# MSVC Release has neither /GL nor /LTCG, and /GL requires matching /LTCG at link
# (and at the static-lib archive step) or LTO silently disengages / fails to link.
target_compile_options(moba_options INTERFACE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /GL>)
target_link_options(moba_options INTERFACE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/LTCG>)
set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG"
    CACHE STRING "" FORCE)

# Control-flow guard (G35): /guard:cf on the optimized configs — CFG is meaningful
# where code is actually optimized. Compile AND link flags must match.
target_compile_options(moba_options INTERFACE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:RelWithDebInfo>>:/guard:cf>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/guard:cf>)
target_link_options(moba_options INTERFACE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:RelWithDebInfo>>:/guard:cf>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/guard:cf>)

# Debug-ASan (G10, the M1.0 promise): /fsanitize=address activates the arena
# poison/unpoison hooks in arena.cpp (they key off __SANITIZE_ADDRESS__, which MSVC
# defines when this flag is on). INTERFACE propagation reaches every target's link
# line, so the test executables get the runtime too. Configure with the
# `debug-asan` preset (build-asan dir); the README records the workflow.
if(MOBA_ASAN)
    target_compile_options(moba_options INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/fsanitize=address>)
    target_link_options(moba_options INTERFACE
        $<$<CXX_COMPILER_ID:MSVC>:/fsanitize=address>)
endif()

# CTest can normalize a Windows environment that contains both `Path` and `PATH`,
# dropping the compiler bin directory that owns MSVC's ASan runtime. Stage the DLL
# app-locally in each executable directory that calls this helper; Windows then finds
# it without relying on a developer-shell search path.
function(moba_stage_asan_runtime)
    if(NOT (MOBA_ASAN AND MSVC))
        return()
    endif()
    get_filename_component(_moba_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_moba_asan_name "clang_rt.asan_dynamic-x86_64.dll")
    else()
        set(_moba_asan_name "clang_rt.asan_dynamic-i386.dll")
    endif()
    set(_moba_asan_dll "${_moba_compiler_dir}/${_moba_asan_name}")
    if(NOT EXISTS "${_moba_asan_dll}")
        message(FATAL_ERROR "MOBA_ASAN runtime not found: ${_moba_asan_dll}")
    endif()

    if(CMAKE_CONFIGURATION_TYPES)
        foreach(_moba_config IN LISTS CMAKE_CONFIGURATION_TYPES)
            file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_moba_config}")
            file(COPY_FILE "${_moba_asan_dll}"
                 "${CMAKE_CURRENT_BINARY_DIR}/${_moba_config}/${_moba_asan_name}"
                 ONLY_IF_DIFFERENT)
        endforeach()
    else()
        file(COPY_FILE "${_moba_asan_dll}"
             "${CMAKE_CURRENT_BINARY_DIR}/${_moba_asan_name}"
             ONLY_IF_DIFFERENT)
    endif()
endfunction()
