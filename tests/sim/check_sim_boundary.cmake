if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(GLOB_RECURSE SIM_SOURCES LIST_DIRECTORIES false
    "${SOURCE_DIR}/engine/sim/*.h"
    "${SOURCE_DIR}/engine/sim/*.hpp"
    "${SOURCE_DIR}/engine/sim/*.c"
    "${SOURCE_DIR}/engine/sim/*.cc"
    "${SOURCE_DIR}/engine/sim/*.cpp")

set(BANNED_PATTERNS
    "(^|[^A-Za-z0-9_])(float|double)([^A-Za-z0-9_]|$)"
    "#[ \t]*include[ \t]*[<\"](cmath|math\\.h)[>\"]"
    "(^|[^A-Za-z0-9_])(acos|asin|atan|atan2|cos|exp|fmod|log|pow|sin|sqrt|tan)[ \t\r\n]*\\("
    "#[ \t]*include[ \t]*[<\"]chrono[>\"]"
    "(^|[^A-Za-z0-9_])(clock|gettimeofday|QueryPerformanceCounter|platform_time_[A-Za-z0-9_]*)[ \t\r\n]*\\("
    "#[ \t]*include[ \t]*[<\"]platform/"
    "#[ \t]*include[ \t]*[<\"]render/"
    "#[ \t]*include[ \t]*[<\"](vulkan|volk)"
    "(^|[^A-Za-z0-9_])(unordered_(map|set)|HashMap)([^A-Za-z0-9_]|$)"
    "#[ \t]*include[ \t]*[<\"]core/hashmap\\.h[>\"]")

set(violations "")
foreach(path IN LISTS SIM_SOURCES)
    file(READ "${path}" source)
    foreach(pattern IN LISTS BANNED_PATTERNS)
        if(source MATCHES "${pattern}")
            list(APPEND violations "${path}: ${CMAKE_MATCH_0}")
        endif()
    endforeach()
endforeach()

file(READ "${SOURCE_DIR}/engine/sim/CMakeLists.txt" sim_cmake)
if(sim_cmake MATCHES "eng::platform|eng::render|Vulkan::|user32|gdi32|winmm")
    list(APPEND violations "engine/sim/CMakeLists.txt: forbidden link dependency ${CMAKE_MATCH_0}")
endif()
foreach(required IN ITEMS "eng::core" "eng::math" "eng::serialize")
    string(FIND "${sim_cmake}" "${required}" required_at)
    if(required_at EQUAL -1)
        list(APPEND violations "engine/sim/CMakeLists.txt: missing required dependency ${required}")
    endif()
endforeach()

if(violations)
    list(JOIN violations "\n  " formatted)
    message(FATAL_ERROR "eng_sim determinism boundary violated:\n  ${formatted}")
endif()

list(LENGTH SIM_SOURCES source_count)
message(STATUS "eng_sim boundary clean across ${source_count} source/header files; link seam is core+math+serialize")

