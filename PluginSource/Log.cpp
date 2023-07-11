#include "PlatformBase.h"
#include "Log.h"

#if defined(UNITY_WIN)
#include <windows.h>
#elif defined(UNITY_ANDROID)
#include <android/log.h>
#endif

extern "C"
{
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
}

void debugmsg( const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

#if defined(UNITY_WIN)
    windows_print(fmt, args);
#elif defined(UNITY_ANDROID)
    __android_log_vprint(ANDROID_LOG_INFO, "VLCUnity", fmt, args);
#else
    vfprintf(stderr, fmt, args);
    vfprintf(stderr, "\n", args);
#endif

    va_end(args);
}

#if defined(UNITY_WIN)
void windows_print(const char* fmt, va_list args)
{
    int msgsize = _vsnprintf(NULL, 0, fmt, args);
    char* buff = (char*)malloc(msgsize + 1);
    _vsnprintf(buff, msgsize + 1, fmt, args);
    buff[msgsize] = '\0';

    int len = MultiByteToWideChar (CP_UTF8, 0, buff, -1, NULL, 0);
    if (len == 0)
        return;

    wchar_t *out = (wchar_t *)malloc (len * sizeof (wchar_t));

    if (out)
    {
        MultiByteToWideChar (CP_UTF8, 0, buff, -1, out, len);
    }
    if(out != NULL)
    {
        OutputDebugStringW(out);
        free(out);
    }
    if(buff != NULL)
    {
        free(buff);
    }
}
#endif
