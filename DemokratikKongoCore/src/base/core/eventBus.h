#pragma once

#include <vector>
#include <functional>
#include <mutex>
#include <type_traits>
#include <algorithm>

/*
========================================================================
    PROJECTX :: CORE :: EventBus
------------------------------------------------------------------------
    Zero-allocation, statically-dispatched event bus. Each event type has
    its own subscriber list, resolved at compile time via templates.
    No std::any, no virtual dispatch on the hot path, no per-event heap
    alloc - perfect for tick/render loops.

    Usage (publisher side):
        TickEvent ev{ partialTicks };
        EventBus::dispatch(ev);

    Usage (subscriber side, e.g. inside Module::onEnable()):
        EventBus::subscribe<TickEvent>(this, [this](TickEvent& e){ ... });

    Modules MUST call EventBus::unsubscribeAll(this) on disable / dtor.
========================================================================
*/

class IEventSubscriber {};

template <typename E>
struct EventChannel
{
    struct Slot {
        const void* owner;
        std::function<void(E&)> fn;
    };
    static inline std::vector<Slot> slots;
    // Guards slots: subscribe/unsubscribe typically run on the cheat thread
    // while dispatch runs on the render thread.
    static inline std::mutex mutex;
};

namespace EventBus
{
    template <typename E, typename Fn>
    inline void subscribe(const void* owner, Fn&& fn)
    {
        std::lock_guard<std::mutex> lock(EventChannel<E>::mutex);
        EventChannel<E>::slots.push_back({ owner, std::forward<Fn>(fn) });
    }

    template <typename E>
    inline void dispatch(E& e)
    {
        // Snapshot under lock, then dispatch the copy: handlers may
        // subscribe/unsubscribe (even self) while we iterate.
        std::vector<typename EventChannel<E>::Slot> snapshot;
        {
            std::lock_guard<std::mutex> lock(EventChannel<E>::mutex);
            snapshot = EventChannel<E>::slots;
        }
        for (auto& slot : snapshot) slot.fn(e);
    }

    template <typename E>
    inline void unsubscribe(const void* owner)
    {
        std::lock_guard<std::mutex> lock(EventChannel<E>::mutex);
        auto& slots = EventChannel<E>::slots;
        slots.erase(
            std::remove_if(slots.begin(), slots.end(),
                [&](const typename EventChannel<E>::Slot& s){ return s.owner == owner; }),
            slots.end());
    }

    // The "remove me from everything" helper. Each time a new event type is
    // added, register it here. Cheap because vectors are tiny.
    void unsubscribeAll(const void* owner);
}
