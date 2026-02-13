#include "GarbageCollector.h"
#include <algorithm>

void GarbageCollector::collect()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // remove expired entries
    m_trackedResources.erase(
        std::remove_if(m_trackedResources.begin(), m_trackedResources.end(),
            [](const std::weak_ptr<EngineObject>& p) { return p.expired(); }),
        m_trackedResources.end());
}

void GarbageCollector::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_trackedResources.clear();
}

bool GarbageCollector::registerResource(const std::shared_ptr<EngineObject>& resource)
{
    if (!resource)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = std::find_if(m_trackedResources.begin(), m_trackedResources.end(),
        [&](const std::weak_ptr<EngineObject>& p)
        {
            auto sp = p.lock();
            return sp && sp.get() == resource.get();
        });

    if (it != m_trackedResources.end())
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
