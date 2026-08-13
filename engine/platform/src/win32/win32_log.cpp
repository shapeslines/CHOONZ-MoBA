// Win32 diagnostics (platform_log / platform_fatal). Split out of
// win32_platform.cpp (G14) so the headless test group and the file/mem TUs can use
// logging without pulling the window TU.
#include <windows.h>
#include <cstdio>
#include <cstdarg>

#include "platform/platform.h"

void platform_log(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    fputs(buf, stderr);
}
[[noreturn]] void platform_fatal(const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA("FATAL: "); OutputDebugStringA(buf);
    fputs("FATAL: ", stderr); fputs(buf, stderr);
#if defined(MOBA_DEBUG)
    if (IsDebuggerPresent()) __debugbreak();
#endif
    ExitProcess(3u);
}
