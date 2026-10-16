// vkfw patch: MSVC CRT functions the SDK's host code uses, for building it with GCC/Clang.
//
// Force-included (-include) into every SDK translation unit on non-Windows hosts, and only there --
// nothing here is visible to code that merely includes the SDK's public headers.
#pragma once

#if !defined(_WIN32)

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>

#ifdef __cplusplus
#include <codecvt>
#include <locale>
#include <string>

#ifndef _countof
template <typename T, size_t N>
char (&ffx_countof_helper(T (&)[N]))[N];
#define _countof(a) (sizeof(ffx_countof_helper(a)))
#endif

inline int wcscpy_s(wchar_t* dst, size_t size, const wchar_t* src)
{
    if (!dst || !size)
        return 22;
    if (!src) { dst[0] = 0; return 22; }
    std::wcsncpy(dst, src, size - 1);
    dst[size - 1] = 0;
    return 0;
}
template <size_t N>
inline int wcscpy_s(wchar_t (&dst)[N], const wchar_t* src) { return wcscpy_s(dst, N, src); }

inline int wcscat_s(wchar_t* dst, size_t size, const wchar_t* src)
{
    const size_t len = std::wcslen(dst);
    return len < size ? wcscpy_s(dst + len, size - len, src) : 34;
}
template <size_t N>
inline int wcscat_s(wchar_t (&dst)[N], const wchar_t* src) { return wcscat_s(dst, N, src); }

inline int strcpy_s(char* dst, size_t size, const char* src)
{
    if (!dst || !size)
        return 22;
    if (!src) { dst[0] = 0; return 22; }
    std::strncpy(dst, src, size - 1);
    dst[size - 1] = 0;
    return 0;
}
template <size_t N>
inline int strcpy_s(char (&dst)[N], const char* src) { return strcpy_s(dst, N, src); }

inline int sprintf_s(char* dst, size_t size, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int r = std::vsnprintf(dst, size, fmt, args);
    va_end(args);
    return r;
}
template <size_t N, typename... Args>
inline int sprintf_s(char (&dst)[N], const char* fmt, Args... args) { return std::snprintf(dst, N, fmt, args...); }

inline int swprintf_s(wchar_t* dst, size_t size, const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const int r = std::vswprintf(dst, size, fmt, args);
    va_end(args);
    return r;
}
template <size_t N, typename... Args>
inline int swprintf_s(wchar_t (&dst)[N], const wchar_t* fmt, Args... args) { return std::swprintf(dst, N, fmt, args...); }
#endif // __cplusplus

#endif // !_WIN32
