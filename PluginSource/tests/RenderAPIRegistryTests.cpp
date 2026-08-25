#include "../RenderAPIRegistry.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class FakeRenderAPI final : public RenderAPI
{
public:
    explicit FakeRenderAPI(std::atomic<unsigned>& calls) : m_calls(calls) {}

    void ProcessDeviceEvent(UnityGfxDeviceEventType,
                            IUnityInterfaces*) override {}
    void performRenderThreadWork() override { ++m_calls; }

private:
    std::atomic<unsigned>& m_calls;
};

libvlc_media_player_t* player(uintptr_t value)
{
    return reinterpret_cast<libvlc_media_player_t*>(value);
}

void testRetirementKeepsSnapshotsAlive()
{
    std::atomic<unsigned> calls { 0 };
    RenderAPIRegistry registry;
    check(registry.insert(
              player(1), std::unique_ptr<RenderAPI>(new FakeRenderAPI(calls))),
          "registry must accept a unique active player");
    check(!registry.insert(
              player(1), std::unique_ptr<RenderAPI>(new FakeRenderAPI(calls))),
          "registry must reject duplicate players");

    RenderAPIEntryPtr active = registry.findActive(player(1));
    check(active != nullptr, "active lookup must return a strong reference");
    RenderAPIEntryPtr retired = registry.beginRetirement(player(1));
    check(retired == active, "retirement must preserve the renderer entry");
    check(!registry.findActive(player(1)),
          "retiring renderer must disappear from active lookup atomically");
    {
        std::lock_guard<std::mutex> lock(retired->callMutex);
        check(retired->state == RenderAPIEntryState::Retiring,
              "beginRetirement must block render work until detach finishes");
        retired->state = RenderAPIEntryState::Retired;
    }
    check(registry.hasRetired(), "completed retirement must remain drainable");

    std::weak_ptr<RenderAPIEntry> lifetime = retired;
    {
        const auto snapshot = registry.retiredSnapshot();
        check(snapshot.size() == 1 && snapshot[0] == retired,
              "retired snapshot must retain the exact entry");
        {
            std::lock_guard<std::mutex> lock(retired->callMutex);
            retired->state = RenderAPIEntryState::Destroying;
        }
        registry.removeRetired(retired);
        check(!registry.hasRetired(), "destroying renderer must leave registry");
        retired.reset();
        active.reset();
        check(!lifetime.expired(),
              "an existing render snapshot must keep a removed entry alive");
    }
    check(lifetime.expired(),
          "renderer must be destroyed after the final snapshot is released");
}

void testConcurrentLookupAndRetirement()
{
    std::atomic<unsigned> calls { 0 };
    std::atomic<bool> keepReading { true };
    RenderAPIRegistry registry;
    check(registry.insert(
              player(2), std::unique_ptr<RenderAPI>(new FakeRenderAPI(calls))),
          "concurrency fixture must insert");

    std::thread reader([&]() {
        while (keepReading.load()) {
            const RenderAPIEntryPtr entry = registry.findActive(player(2));
            if (!entry)
                continue;
            std::lock_guard<std::mutex> lock(entry->callMutex);
            if (entry->state == RenderAPIEntryState::Active && entry->renderer)
                entry->renderer->performRenderThreadWork();
        }
    });

    const RenderAPIEntryPtr retired = registry.beginRetirement(player(2));
    check(retired != nullptr, "concurrent retirement must find the active entry");
    {
        std::lock_guard<std::mutex> lock(retired->callMutex);
        retired->state = RenderAPIEntryState::Retired;
    }
    keepReading.store(false);
    reader.join();
    check(!registry.findActive(player(2)),
          "no active lookup may succeed after retirement");
}

} // namespace

int main()
{
    testRetirementKeepsSnapshotsAlive();
    testConcurrentLookupAndRetirement();
    if (failures)
        std::cerr << failures << " test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
