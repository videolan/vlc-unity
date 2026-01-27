#include <stdarg.h>

#ifndef LOG_H_
#define LOG_H_

// DEBUG - always enabled, use for errors, warnings, and important state changes
#define DEBUG(fmt, ...) debugmsg( "[VLC-Unity] " fmt, ## __VA_ARGS__ )

// DEBUG_VERBOSE - disabled in release builds (NDEBUG), use for per-frame/high-frequency logs
#ifdef NDEBUG
#define DEBUG_VERBOSE(fmt, ...) ((void)0)
#else
#define DEBUG_VERBOSE(fmt, ...) debugmsg( "[VLC-Unity] " fmt, ## __VA_ARGS__ )
#endif

void debugmsg( const char* fmt, ...);
#if defined(UNITY_WIN)
void windows_print(const char* fmt, va_list args);
#endif
#endif
