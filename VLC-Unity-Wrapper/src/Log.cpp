#include "PlatformBase.h"

#if UNITY_WIN
#include <windows.h>
#elif UNITY_ANDROID
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

#if UNITY_WIN
    int msgsize = _vsnprintf(NULL, 0, fmt, args);
    char* buff = (char*)malloc(msgsize + 1);
    _vsnprintf(buff, msgsize + 1, fmt, args);
    buff[msgsize] = '\0';
    OutputDebugString(buff);
    free(buff);
#elif UNITY_ANDROID
    __android_log_vprint(ANDROID_LOG_INFO, "VLCUnity", fmt, args);
#else
    vfprintf(stderr, fmt, args);
    vfprintf(stderr, "\n", args);
#endif

    va_end(args);
}
