#include "GarbageCollector.h"
#include <algorithm>

void GarbageCollector::collect()
{
    // remove expired entries
    m_trackedResources.erase(
        std::remove_if(m_trackedResources.begin(), m_trackedResources.end(),
            [](const std::weak_ptr<EngineObject>& p) { return p.expired(); }),
        m_trackedResources.end());
}

bool GarbageCollector::registerResource(const std::shared_ptr<EngineObject>& resource)
{
    if (!resource)
    {
        return false;
    }

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
