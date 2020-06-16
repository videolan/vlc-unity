#ifndef LOG_H_
#define LOG_H_

#define DEBUG(fmt, ...) debugmsg( "[VLC-Unity] " fmt, ## __VA_ARGS__ )
void debugmsg( const char* fmt, ...);

#endif
