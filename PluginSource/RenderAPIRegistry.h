#pragma once

#include "RenderAPI.h"

#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

enum class RenderAPIEntryState
{
    Active,
    Retiring,
    Retired,
    Destroying,
};

// A strong renderer reference plus the lock that serializes every call into
// that renderer. State is accessed only while callMutex is held.
struct RenderAPIEntry
{
    RenderAPIEntry(libvlc_media_player_t* player,
                   std::unique_ptr<RenderAPI> rendererValue)
        : mediaPlayer(player), renderer(std::move(rendererValue))
    {
    }

    libvlc_media_player_t* mediaPlayer = nullptr;
    std::unique_ptr<RenderAPI> renderer;
    std::mutex callMutex;
    RenderAPIEntryState state = RenderAPIEntryState::Active;
};

using RenderAPIEntryPtr = std::shared_ptr<RenderAPIEntry>;

// Registry operations are short. Renderer and driver calls happen after a
// strong-reference snapshot has been taken and the registry lock released.
class RenderAPIRegistry
{
public:
    bool insert(libvlc_media_player_t* player,
                std::unique_ptr<RenderAPI> renderer);
    RenderAPIEntryPtr findActive(libvlc_media_player_t* player) const;
    RenderAPIEntryPtr beginRetirement(libvlc_media_player_t* player);
    void removeRetired(const RenderAPIEntryPtr& entry);

    std::vector<RenderAPIEntryPtr> activeSnapshot() const;
    std::vector<RenderAPIEntryPtr> retiredSnapshot() const;
    std::vector<RenderAPIEntryPtr> takeAll();

    size_t activeCount() const;
    bool hasRetired() const;

private:
    mutable std::mutex m_mutex;
    std::map<libvlc_media_player_t*, RenderAPIEntryPtr> m_active;
    std::vector<RenderAPIEntryPtr> m_retired;
};
