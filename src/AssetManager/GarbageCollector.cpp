#include "GarbageCollector.h"
#include <algorithm>

void GarbageCollector::collect()
{
    // remove expired entries
    m_trackedResources.erase(
        std::remove_if(m_trackedResources.begin(), m_trackedResources.end(),
            [](const std::weak_ptr<EngineObject>& w) { return w.expired(); }),
        m_trackedResources.end());
}

bool GarbageCollector::registerResource(const std::shared_ptr<EngineObject>& resource)
{
    if (!resource)
    {
        return false;
    }

    // cleanup expired first
    collect();

    auto it = std::find_if(m_trackedResources.begin(), m_trackedResources.end(),
        [&](const std::weak_ptr<EngineObject>& w)
        {
            auto locked = w.lock();
            return locked && locked.get() == resource.get();
        });

    if (it != m_trackedResources.end())
    {
        // already tracked
        return false;
    }

    m_trackedResources.push_back(resource);
    return true;
}

bool GarbageCollector::isRelevant(const std::shared_ptr<EngineObject>& resource) const
{
    return resource != nullptr;
}
