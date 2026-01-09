#include "GarbageCollector.h"
#include <algorithm>

void GarbageCollector::collect()
{
    // remove invalid entries (should be rare with shared_ptr storage)
    m_trackedResources.erase(
        std::remove_if(m_trackedResources.begin(), m_trackedResources.end(),
            [](const std::shared_ptr<EngineObject>& p) { return p == nullptr; }),
        m_trackedResources.end());
}

bool GarbageCollector::registerResource(const std::shared_ptr<EngineObject>& resource)
{
    if (!resource)
    {
        return false;
    }

    auto it = std::find_if(m_trackedResources.begin(), m_trackedResources.end(),
        [&](const std::shared_ptr<EngineObject>& p)
        {
            return p && p.get() == resource.get();
        });

    if (it != m_trackedResources.end())
    {
        // already tracked
        return false;
    }

    m_trackedResources.push_back(resource);
    return true;
}

const std::vector<std::shared_ptr<EngineObject>>& GarbageCollector::getTrackedResourcesRef() const
{
    return m_trackedResources;
}

bool GarbageCollector::isRelevant(const std::shared_ptr<EngineObject>& resource) const
{
    return resource != nullptr;
}
