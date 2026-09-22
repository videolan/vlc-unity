#include "../linux/LinuxVideoOutput.h"
#include <future>
#include <iostream>

namespace {
LinuxVideoOutput::Callbacks installed;
int failures = 0;
int setupToken, sessionToken;
int calls = 0;

void check(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool setup(void** opaque, const libvlc_video_setup_device_cfg_t*, libvlc_video_setup_device_info_t*)
{
    check(*opaque == &setupToken, "backend gets initial opaque");
    *opaque = &sessionToken;
    ++calls;
    return true;
}
void cleanup(void* opaque) { check(opaque == &sessionToken, "cleanup gets session opaque"); ++calls; }
bool resize(void* opaque, const libvlc_video_render_cfg_t*, libvlc_video_output_cfg_t*)
{ check(opaque == &sessionToken, "resize gets session opaque"); ++calls; return true; }
void swap(void* opaque) { check(opaque == &sessionToken, "swap gets session opaque"); ++calls; }
bool makeCurrent(void* opaque, bool) { check(opaque == &sessionToken, "makeCurrent gets session opaque"); ++calls; return true; }
void* getProc(void* opaque, const char*) { check(opaque == &sessionToken, "getProc gets session opaque"); ++calls; return &sessionToken; }
}

// Capture registration without creating a real player, display, or GPU context.
extern "C" bool libvlc_video_set_output_callbacks(libvlc_media_player_t*, libvlc_video_engine_t engine,
    libvlc_video_output_setup_cb setupCb, libvlc_video_output_cleanup_cb cleanupCb,
    libvlc_video_output_set_window_cb, libvlc_video_update_output_cb resizeCb,
    libvlc_video_swap_cb swapCb, libvlc_video_makeCurrent_cb currentCb,
    libvlc_video_getProcAddress_cb procCb, libvlc_video_frameMetadata_cb,
    libvlc_video_output_select_plane_cb, void* opaque)
{
    check(engine == libvlc_video_engine_opengl, "OpenGL output installed before readiness");
    installed = {setupCb, cleanupCb, resizeCb, swapCb, currentCb, procCb, opaque};
    return true;
}

int main()
{
    LinuxVideoOutput output;
    check(output.install(nullptr), "install stable callbacks");
    check(!output.ready(), "installation does not claim backend readiness");
    LinuxVideoOutput::Callbacks callbacks;
    check(!output.wait(callbacks, std::chrono::milliseconds(0)), "uninitialized backend times out");

    // Exercise the real setup trampoline waiting concurrently with publish.
    void* opaque = installed.opaque;
    auto pending = std::async(std::launch::async, [&] { return installed.setup(&opaque, nullptr, nullptr); });
    check(pending.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout,
          "setup remains pending before render-thread initialization");
    output.configure({setup, cleanup, resize, swap, makeCurrent, getProc, &setupToken});
    check(pending.get(), "pending setup resumes when the backend is ready");
    check(output.ready(), "backend readiness is published");
    installed.resize(opaque, nullptr, nullptr);
    installed.swap(opaque);
    installed.makeCurrent(opaque, true);
    check(installed.getProc(opaque, "test") == &sessionToken, "getProc return is forwarded");
    installed.cleanup(opaque);
    check(calls == 6, "all callbacks forwarded exactly once");

    // Each replay gets a fresh backend opaque, not the last session's value.
    opaque = installed.opaque;
    check(installed.setup(&opaque, nullptr, nullptr), "setup supports replay");
    installed.cleanup(opaque);

    LinuxVideoOutput cancelled;
    auto waiting = std::async(std::launch::async, [&] {
        LinuxVideoOutput::Callbacks ignored;
        return cancelled.wait(ignored, std::chrono::seconds(5));
    });
    check(waiting.wait_for(std::chrono::milliseconds(10)) == std::future_status::timeout,
          "setup is waiting before disposal");
    cancelled.cancel();
    check(waiting.wait_for(std::chrono::seconds(1)) == std::future_status::ready,
          "disposal wakes pending setup without a Unity render event");
    check(!waiting.get(), "cancelled setup fails closed");
    cancelled.configure({setup, cleanup, resize, swap, makeCurrent, getProc, &setupToken});
    check(!cancelled.ready(), "late initialization cannot revive a disposed output");
    return failures ? 1 : 0;
}
