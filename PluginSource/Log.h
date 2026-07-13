#ifndef LOG_H_
#define LOG_H_

#include <stdarg.h>
#include <stdint.h>

// DEBUG - always enabled, use for errors, warnings, and important state changes
#include "Unity/IUnityInterface.h"

#define DEBUG(fmt, ...) debugmsg( 0xD2D2D2FF, "[VLC-Unity] " fmt, ## __VA_ARGS__ )

// DEBUG_VERBOSE - disabled in release builds (NDEBUG), use for per-frame/high-frequency logs
#ifdef NDEBUG
#define DEBUG_VERBOSE(fmt, ...) ((void)0)
#else
#define DEBUG_VERBOSE(fmt, ...) debugmsg( 0xD2D2D2FF, "[VLC-Unity] " fmt, ## __VA_ARGS__ )
#endif

#ifdef __cplusplus
extern "C" {
#endif
    typedef void (*LogCallbackFunc)(const char* message, uint32_t hexColor);
    void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API SetLogCallback(LogCallbackFunc callback);
#ifdef __cplusplus
}
#endif

void debugmsg(uint32_t hexColor, const char* fmt, ...);
#if defined(UNITY_WIN)
void windows_print(const char* fmt, va_list args);
#endif
#endif
