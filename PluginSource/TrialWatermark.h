#pragma once

#include <stdint.h>

#if defined(SHOW_WATERMARK)
extern "C" bool libvlc_unity_trial_tick();
extern "C" uint32_t libvlc_unity_trial_seconds_remaining();
extern "C" bool libvlc_unity_trial_is_paused();
extern "C" bool libvlc_unity_trial_is_stopped();

// Per-frame gate for producer threads. Expiry does not stop playback here;
// RenderingPlugin stops expired players centrally on Unity's render thread.
inline bool libvlc_unity_trial_allows_frame()
{
    if (libvlc_unity_trial_is_paused())
        return false;
    return libvlc_unity_trial_is_stopped() || libvlc_unity_trial_tick();
}
#endif
