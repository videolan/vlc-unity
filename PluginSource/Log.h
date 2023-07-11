#include <stdarg.h>

#ifndef LOG_H_
#define LOG_H_

#define DEBUG(fmt, ...) debugmsg( "[VLC-Unity] " fmt, ## __VA_ARGS__ )
void debugmsg( const char* fmt, ...);
#if defined(UNITY_WIN)
void windows_print(const char* fmt, va_list args);
#endif
#endif
