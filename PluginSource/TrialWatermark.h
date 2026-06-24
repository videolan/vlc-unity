#pragma once

#include <stdint.h>

#if defined(SHOW_WATERMARK)
extern "C" bool libvlc_unity_trial_tick();
extern "C" uint32_t libvlc_unity_trial_seconds_remaining();
extern "C" bool libvlc_unity_trial_is_paused();
extern "C" bool libvlc_unity_trial_is_stopped();
#endif
