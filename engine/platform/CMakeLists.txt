# eng_platform — the OS seam (ADR-0005). Win32 impl in src/win32/. The page
# allocator, file I/O, and logging are separate TUs (G14) so the headless test group
# links eng_platform without pulling the window TU into the test executables.
add_library(eng_platform STATIC
    src/win32/win32_platform.cpp     # window/input, clock, Vulkan loader + surface
    src/win32/win32_mem.cpp          # OS page allocator + page-backed arenas
    src/win32/win32_file.cpp         # UTF-8 file I/O (atomic .tmp writes)
    src/win32/win32_log.cpp)         # platform_log / platform_fatal
add_library(eng::platform ALIAS eng_platform)

target_include_directories(eng_platform
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(eng_platform
    PUBLIC  eng::core
    PRIVATE moba_warnings moba_options
            user32 gdi32 winmm)   # window/input, GDI, timeBeginPeriod

# Vulkan surface creation (vkCreateWin32SurfaceKHR) lives in the platform (ADR-0005).
# Headers only, and only when the SDK is present — the headless/CI build stays
# Vulkan-free (platform_vk_create_surface is then simply not compiled).
if(TARGET Vulkan::Headers)
    target_link_libraries(eng_platform PRIVATE Vulkan::Headers)
    target_compile_definitions(eng_platform PRIVATE MOBA_HAVE_VULKAN=1)
endif()
