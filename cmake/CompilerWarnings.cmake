# moba_warnings — INTERFACE target carrying the project's warning posture.
# Linked PRIVATE to our OWN targets only, NEVER to Vulkan/third-party (ADR-0006, §3.3).
# /WX is opt-in via MOBA_WERROR (lenient locally so iteration isn't blocked; ON in CI/test).

add_library(moba_warnings INTERFACE)

if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(moba_warnings INTERFACE
        /W4                 # high warning level
        /permissive-        # strict standard conformance
        /Zc:__cplusplus     # report the real __cplusplus value
        /utf-8              # source + execution charset = UTF-8
        /diagnostics:caret  # point at the exact token
        /wd4201             # allow nameless struct/union (used by the math types)
    )

    if(MOBA_WERROR)
        target_compile_options(moba_warnings INTERFACE /WX)
    endif()
endif()

# clang-cl 19 implements a conforming preprocessor by default and diagnoses
# /Zc:preprocessor as unused under /WX. cl.exe still needs the explicit switch.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(moba_warnings INTERFACE /Zc:preprocessor)
endif()
