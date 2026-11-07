// fopen for UTF-8 paths: on Windows the narrow fopen uses the ANSI code page, which breaks
// names with accents inside a plug-in host (the stand-alone exe has a UTF-8 manifest, a DLL
// cannot), so the path is converted and _wfopen is used.
#pragma once
#include <cstdio>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
inline FILE *u8fopen(const char *path, const char *mode) {
    auto wide = [](const char *s) {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, nullptr, 0);
        if (n <= 0) return std::wstring();
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
        w.resize((size_t)n - 1);
        return w;
    };
    std::wstring wp = wide(path), wm = wide(mode);
    if (wp.empty()) return fopen(path, mode);       // not valid UTF-8: leave it to the C library
    return _wfopen(wp.c_str(), wm.c_str());
}
#else
inline FILE *u8fopen(const char *path, const char *mode) { return fopen(path, mode); }
#endif
