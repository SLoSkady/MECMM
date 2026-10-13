#include "core/log.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

void Log(const char* fmt, ...) {
    static std::mutex mutex;
    static FILE* file = [] {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p(path);
        p = p.substr(0, p.find_last_of(L"\\/") + 1) + L"mecmm.log";
        return _wfsopen(p.c_str(), L"w", _SH_DENYWR);
    }();
    if (!file) return;

    std::lock_guard lock(mutex);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(file, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);
    fputc('\n', file);
    fflush(file);
}
