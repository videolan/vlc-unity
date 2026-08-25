#include "RenderAPIRegistry.h"

#include <algorithm>

bool RenderAPIRegistry::insert(libvlc_media_player_t* player,
                               std::unique_ptr<RenderAPI> renderer)
{
    if (!player || !renderer)
        return false;
    RenderAPIEntryPtr entry = std::make_shared<RenderAPIEntry>(
        player, std::move(renderer));
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_active.emplace(player, std::move(entry)).second;
}

RenderAPIEntryPtr RenderAPIRegistry::findActive(
    libvlc_media_player_t* player) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_active.find(player);
    return found == m_active.end() ? nullptr : found->second;
}

RenderAPIEntryPtr RenderAPIRegistry::beginRetirement(
    libvlc_media_player_t* player)
{
    std::lock_guard<std::mutex> registryLock(m_mutex);
    const auto found = m_active.find(player);
    if (found == m_active.end())
        return nullptr;

    const RenderAPIEntryPtr entry = found->second;
    std::lock_guard<std::mutex> entryLock(entry->callMutex);
    if (entry->state != RenderAPIEntryState::Active)
        return nullptr;
    entry->state = RenderAPIEntryState::Retiring;
    m_active.erase(found);
    m_retired.push_back(entry);
    return entry;
}

void RenderAPIRegistry::removeRetired(const RenderAPIEntryPtr& entry)
{
    if (!entry)
        return;
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = std::find(m_retired.begin(), m_retired.end(), entry);
    if (found != m_retired.end())
        m_retired.erase(found);
}

std::vector<RenderAPIEntryPtr> RenderAPIRegistry::activeSnapshot() const
{
    std::vector<RenderAPIEntryPtr> result;
    std::lock_guard<std::mutex> lock(m_mutex);
    result.reserve(m_active.size());
    for (const auto& item : m_active)
        result.push_back(item.second);
    return result;
}

std::vector<RenderAPIEntryPtr> RenderAPIRegistry::retiredSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_retired;
}

std::vector<RenderAPIEntryPtr> RenderAPIRegistry::takeAll()
{
    std::vector<RenderAPIEntryPtr> result;
    std::lock_guard<std::mutex> lock(m_mutex);
    result.reserve(m_active.size() + m_retired.size());
    for (auto& item : m_active)
        result.push_back(std::move(item.second));
    for (auto& entry : m_retired)
        result.push_back(std::move(entry));
    m_active.clear();
    m_retired.clear();
    return result;
}

size_t RenderAPIRegistry::activeCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_active.size();
}

bool RenderAPIRegistry::hasRetired() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_retired.empty();
}
