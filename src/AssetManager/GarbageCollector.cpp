#include "GarbageCollector.h"
#include <algorithm>

void GarbageCollector::collect()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // remove expired entries and keep the pointer set in sync
    m_trackedResources.erase(
        std::remove_if(m_trackedResources.begin(), m_trackedResources.end(),
            [this](const std::weak_ptr<EngineObject>& p)
            {
                if (p.expired())
                {
                    // Cannot recover the raw pointer from an expired weak_ptr,
                    // so rebuild the set after erasure.
                    return true;
                }
                return false;
            }),
        m_trackedResources.end());

    // Rebuild the fast-lookup set from the surviving entries
    m_registeredPtrs.clear();
    m_registeredPtrs.reserve(m_trackedResources.size());
    for (const auto& w : m_trackedResources)
    {
        if (auto sp = w.lock())
            m_registeredPtrs.insert(sp.get());
    }
}

void GarbageCollector::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_trackedResources.clear();
    m_registeredPtrs.clear();
}

bool GarbageCollector::registerResource(const std::shared_ptr<EngineObject>& resource)
{
    if (!resource)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // O(1) duplicate check instead of O(n) linear scan
    if (!m_registeredPtrs.insert(resource.get()).second)
    {
        return false;
    }

    m_trackedResources.push_back(resource);
    return true;
}

std::vector<std::shared_ptr<EngineObject>> GarbageCollector::getAliveResources() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<std::shared_ptr<EngineObject>> out;
    out.reserve(m_trackedResources.size());

    for (const auto& w : m_trackedResources)
    {
        if (auto sp = w.lock())
        {
            out.push_back(std::move(sp));
        }
    }

    return out;
}

const std::vector<std::weak_ptr<EngineObject>>& GarbageCollector::getTrackedResourcesRef() const
{
    return m_trackedResources;
}
