if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(GLOB_RECURSE GAME_SOURCES LIST_DIRECTORIES false
    "${SOURCE_DIR}/engine/game/*.h"
    "${SOURCE_DIR}/engine/game/*.hpp"
    "${SOURCE_DIR}/engine/game/*.c"
    "${SOURCE_DIR}/engine/game/*.cc"
    "${SOURCE_DIR}/engine/game/*.cpp")

set(violations "")
foreach(path IN LISTS GAME_SOURCES)
    file(READ "${path}" source)
    foreach(pattern IN ITEMS
            "const_cast[ \t\r\n]*<"
            "present_advance[ \t\r\n]*\\("
            "#[ \t]*include[ \t]*[<\"]platform/"
            "(^|[^A-Za-z0-9_])sim_tick[ \t\r\n]*\\("
            "(^|[^A-Za-z0-9_])(malloc|calloc|realloc|free)[ \t\r\n]*\\(")
        if(source MATCHES "${pattern}")
            list(APPEND violations "${path}: ${CMAKE_MATCH_0}")
        endif()
    endforeach()
endforeach()

file(READ "${SOURCE_DIR}/engine/game/include/game/present.h" present_header)
foreach(required IN ITEMS
        "present_memory_required"
        "present_init"
        "snapshot_extract"
        "present_capture"
        "present_build_draw_items")
    string(FIND "${present_header}" "${required}" required_at)
    if(required_at EQUAL -1)
        list(APPEND violations "game/present.h: missing public API ${required}")
    endif()
endforeach()
if(present_header MATCHES "accumulator|present_advance|frame_arena")
    list(APPEND violations "game/present.h: stale cadence/arena API ${CMAKE_MATCH_0}")
endif()

file(READ "${SOURCE_DIR}/engine/game/CMakeLists.txt" game_cmake)
foreach(required IN ITEMS "eng::core" "eng::math" "eng::sim" "eng_render_common")
    string(FIND "${game_cmake}" "${required}" required_at)
    if(required_at EQUAL -1)
        list(APPEND violations "engine/game/CMakeLists.txt: missing ${required}")
    endif()
endforeach()
if(game_cmake MATCHES "eng::platform|eng::render([^_A-Za-z0-9]|$)|Vulkan::|user32|gdi32|winmm")
    list(APPEND violations "engine/game/CMakeLists.txt: forbidden dependency ${CMAKE_MATCH_0}")
endif()

file(READ "${SOURCE_DIR}/engine/platform/include/platform/platform_fixed_step.h" fixed_header)
file(READ "${SOURCE_DIR}/engine/platform/src/platform_fixed_step.cpp" fixed_source)
set(fixed_combined "${fixed_header}\n${fixed_source}")
if(fixed_combined MATCHES "#[ \t]*include[ \t]*[<\"](game|sim)/")
    list(APPEND violations "platform fixed-step: forbidden game/sim include ${CMAKE_MATCH_0}")
endif()
file(READ "${SOURCE_DIR}/engine/platform/CMakeLists.txt" platform_cmake)
if(platform_cmake MATCHES "eng::game|eng::sim")
    list(APPEND violations "engine/platform/CMakeLists.txt: forbidden game/sim dependency ${CMAKE_MATCH_0}")
endif()

file(GLOB_RECURSE RENDER_SOURCES LIST_DIRECTORIES false
    "${SOURCE_DIR}/engine/render/*.h"
    "${SOURCE_DIR}/engine/render/*.hpp"
    "${SOURCE_DIR}/engine/render/*.c"
    "${SOURCE_DIR}/engine/render/*.cc"
    "${SOURCE_DIR}/engine/render/*.cpp")
foreach(path IN LISTS RENDER_SOURCES)
    file(READ "${path}" source)
    if(source MATCHES "#[ \t]*include[ \t]*[<\"]sim/")
        list(APPEND violations "${path}: renderer includes sim via ${CMAKE_MATCH_0}")
    endif()
endforeach()

file(READ "${SOURCE_DIR}/tools/sandbox/src/main.cpp" sandbox_source)
foreach(required IN ITEMS
        "platform_fixed_step_tick_owed"
        "sim_tick"
        "present_capture"
        "platform_fixed_step_consume_tick")
    string(FIND "${sandbox_source}" "${required}" required_at)
    if(required_at EQUAL -1)
        list(APPEND violations "sandbox: missing fixed-tick loop call ${required}")
    endif()
endforeach()
if(sandbox_source MATCHES "present_advance[ \t\r\n]*\\(")
    list(APPEND violations "sandbox: stale present-owned cadence call ${CMAKE_MATCH_0}")
endif()

if(violations)
    list(JOIN violations "\n  " formatted)
    message(FATAL_ERROR "M3.3 presentation boundary violated:\n  ${formatted}")
endif()

list(LENGTH GAME_SOURCES game_source_count)
message(STATUS "M3.3 boundary clean across ${game_source_count} game files; cadence=platform, snapshots=game, renderer=sim-free")
