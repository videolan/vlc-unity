#pragma once

#include "Log.h"
#include <vlc/vlc.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

// Install stable callbacks before libvlc can choose a standalone video output.
// Only libvlc's setup thread waits for readiness; Unity's threads never wait.
class LinuxVideoOutput
{
public:
    struct Callbacks
    {
        libvlc_video_output_setup_cb setup = nullptr;
        libvlc_video_output_cleanup_cb cleanup = nullptr;
        libvlc_video_update_output_cb resize = nullptr;
        libvlc_video_swap_cb swap = nullptr;
        libvlc_video_makeCurrent_cb makeCurrent = nullptr;
        libvlc_video_getProcAddress_cb getProc = nullptr;
        void* opaque = nullptr;
    };

    bool install(libvlc_media_player_t* mp)
    {
        return libvlc_video_set_output_callbacks(mp, libvlc_video_engine_opengl,
            setup, cleanup, nullptr, resize, swap, makeCurrent, getProc,
            nullptr, nullptr, this);
    }

    void configure(const Callbacks& callbacks)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_cancelled) return;
        m_callbacks = callbacks;
        m_ready = true;
        m_changed.notify_all();
    }

    void cancel()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_cancelled = true;
        m_changed.notify_all();
    }

    bool ready() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_ready && !m_cancelled;
    }

    bool wait(Callbacks& callbacks, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_changed.wait_for(lock, timeout, [this] { return m_ready || m_cancelled; }) || m_cancelled)
            return false;
        callbacks = m_callbacks;
        return true;
    }

private:
    // libvlc permits setup to replace opaque. Each setup/cleanup pair owns a
    // snapshot, including any opaque replacement made by the real backend.
    static bool setup(void** opaque, const libvlc_video_setup_device_cfg_t* cfg,
                      libvlc_video_setup_device_info_t* info)
    {
        auto* output = static_cast<LinuxVideoOutput*>(*opaque);
        Callbacks callbacks;
        if (!output->wait(callbacks, std::chrono::seconds(5))) {
            DEBUG("[Linux] video output setup cancelled or Unity interop not ready within 5s");
            return false;
        }
        if (!callbacks.setup(&callbacks.opaque, cfg, info)) return false;
        *opaque = new Callbacks(callbacks);
        return true;
    }
    static void cleanup(void* opaque)
    {
        auto* callbacks = static_cast<Callbacks*>(opaque);
        callbacks->cleanup(callbacks->opaque);
        delete callbacks;
    }
    static bool resize(void* opaque, const libvlc_video_render_cfg_t* cfg,
                       libvlc_video_output_cfg_t* output)
    {
        auto& callbacks = *static_cast<Callbacks*>(opaque);
        return callbacks.resize(callbacks.opaque, cfg, output);
    }
    static void swap(void* opaque)
    {
        auto& callbacks = *static_cast<Callbacks*>(opaque);
        callbacks.swap(callbacks.opaque);
    }
    static bool makeCurrent(void* opaque, bool current)
    {
        auto& callbacks = *static_cast<Callbacks*>(opaque);
        return callbacks.makeCurrent(callbacks.opaque, current);
    }
    static void* getProc(void* opaque, const char* name)
    {
        auto& callbacks = *static_cast<Callbacks*>(opaque);
        return callbacks.getProc(callbacks.opaque, name);
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    Callbacks m_callbacks;
    bool m_ready = false;
    bool m_cancelled = false;
};
