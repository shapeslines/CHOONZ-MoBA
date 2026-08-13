# moba_options — the ONE flag string for the whole project (ADR-0009, §3.3).
# No exceptions, no RTTI; per-config defines; explicit Release whole-program opt.
# Linked PRIVATE to every one of our targets.

add_library(moba_options INTERFACE)

# The deterministic island has one additional, deliberately narrower policy owner.
# Only targets that compile authoritative simulation implementation sources may link
# this target. The marker lets generated-build checks prove the policy arrived via
# this named seam instead of an executable adding an equivalent option ad hoc.
add_library(moba_sim_determinism INTERFACE)
add_library(moba::sim_determinism ALIAS moba_sim_determinism)
target_compile_definitions(moba_sim_determinism INTERFACE
    MOBA_SIM_DETERMINISTIC=1)
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(moba_sim_determinism INTERFACE /fp:precise)
endif()

# No RTTI, no exceptions (ADR-0009). _HAS_EXCEPTIONS=0 stops the STL dragging in
# exception machinery under /EHs-c-.
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(moba_options INTERFACE /GR- /EHs-c-)
endif()

target_compile_definitions(moba_options INTERFACE
    _HAS_EXCEPTIONS=0
    _CRT_SECURE_NO_WARNINGS
    $<$<CONFIG:Debug>:MOBA_DEBUG=1>
    $<$<CONFIG:RelWithDebInfo>:MOBA_DEV=1>
    $<$<CONFIG:Release>:MOBA_RELEASE=1>)

# Stretch-only second-toolchain gate. The script in tools/ first proves that the
# installed clang-cl runtime catches a real tripwire; this configure check then
# prevents an ON build from silently accepting but failing to link the flags.
if(MOBA_UBSAN)
    if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
            CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
        message(FATAL_ERROR "MOBA_UBSAN requires clang-cl (Clang with the MSVC frontend)")
    endif()
    # Visual Studio's clang_rt.ubsan_standalone libraries are built against the
    # static release CRT. Keep this isolated stretch build ABI-compatible; normal
    # MSVC/clang-cl builds retain CMake's default runtime selection.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(
        "-fsanitize=undefined;-fno-sanitize-recover=undefined"
        MOBA_CLANG_CL_UBSAN_FLAGS_SUPPORTED)
    if(NOT MOBA_CLANG_CL_UBSAN_FLAGS_SUPPORTED)
        message(FATAL_ERROR
            "clang-cl cannot compile/link the requested UBSan flags and runtime")
    endif()

    target_compile_options(moba_options INTERFACE
        -fsanitize=undefined
        -fno-sanitize-recover=undefined)
    target_compile_definitions(moba_options INTERFACE MOBA_UBSAN_ENABLED=1)
    message(STATUS "MOBA UBSan capability: clang-cl compile/link flags supported")
endif()

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
