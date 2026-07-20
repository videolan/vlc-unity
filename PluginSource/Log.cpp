#include "PlatformBase.h"
#include "Log.h"
#include "Unity/IUnityInterface.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

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
static size_t g_activeLogCallbacks = 0;
static std::mutex g_logCallbackMutex;
static std::condition_variable g_logCallbackIdle;

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetLogCallback(LogCallbackFunc callback)
{
    {
        std::unique_lock<std::mutex> lock(g_logCallbackMutex);
        g_logCallback = callback;

        // Bounded: this runs on the main thread during shutdown and domain
        // reload. A logging thread stuck in managed code must not be able to
        // hang the editor, so give up rather than wait forever. The managed
        // side keeps the delegate rooted for the life of the domain, so a
        // straggler that outlives the wait still calls into valid memory.
        if (callback == nullptr)
            g_logCallbackIdle.wait_for(lock, std::chrono::milliseconds(250),
                [] { return g_activeLogCallbacks == 0; });
    }

    if (callback != nullptr)
    {
        DEBUG("Native logging callback registered successfully from C++ side");
    }
}

void debugmsg(uint32_t hexColor, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    LogCallbackFunc callback = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_logCallbackMutex);
        callback = g_logCallback;

        if (callback != nullptr)
            ++g_activeLogCallbacks;
    }

    if (callback != nullptr)
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
                callback(buffer, hexColor);
                free(buffer);
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_logCallbackMutex);
            --g_activeLogCallbacks;

            if (g_activeLogCallbacks == 0)
                g_logCallbackIdle.notify_all();
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
    fputc('\n', stderr);
#endif
    }

    va_end(args);
}

#if defined(UNITY_WIN)
void windows_print(const char* fmt, va_list args)
{
    va_list size_args;
    va_copy(size_args, args);
    int msgsize = vsnprintf(NULL, 0, fmt, size_args);
    va_end(size_args);

    if (msgsize < 0)
        return;

    char* buff = (char*)malloc(msgsize + 1);
    if (buff == NULL)
        return;

    va_list format_args;
    va_copy(format_args, args);
    vsnprintf(buff, msgsize + 1, fmt, format_args);
    va_end(format_args);
    buff[msgsize] = '\0';

    int len = MultiByteToWideChar (CP_UTF8, 0, buff, -1, NULL, 0);
    if (len == 0)
    {
        free(buff);
        return;
    }

    wchar_t *out = (wchar_t *)malloc (len * sizeof (wchar_t));

    if (out)
    {
        MultiByteToWideChar (CP_UTF8, 0, buff, -1, out, len);
        OutputDebugStringW(out);
        free(out);
    }

    free(buff);
}
#endif
