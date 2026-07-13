#include "PlatformBase.h"
#include "Log.h"
#include "Unity/IUnityInterface.h"

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

static LogCallbackFunc g_logCallback = nullptr;

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetLogCallback(LogCallbackFunc callback)
{
    g_logCallback = callback;
    if (callback != nullptr)
    {
        DEBUG("Native logging callback registered successfully from C++ side");
    }
}

void debugmsg(uint32_t hexColor, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    if (g_logCallback != nullptr)
    {
        va_list args_copy;
        va_copy(args_copy, args);
        int size = vsnprintf(NULL, 0, fmt, args_copy) + 1;
        va_end(args_copy);

        if (size > 0)
        {
            char* buffer = (char*)malloc(size);
            if (buffer != NULL)
            {
                vsnprintf(buffer, size, fmt, args);
                g_logCallback(buffer, hexColor);
                free(buffer);
            }
        }
    }
    else
    {
#if defined(UNITY_WIN)
    windows_print(fmt, args);
#elif defined(UNITY_ANDROID)
    __android_log_vprint(ANDROID_LOG_INFO, "VLCUnity", fmt, args);
#else
    vfprintf(stderr, fmt, args);
    vfprintf(stderr, "\n", args);
#endif
    }

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
