#pragma once
#include <vector>
#include <unordered_set>
#include <memory>
#include <mutex>
#include "../Core/EngineObject.h"

class GarbageCollector
{
public:
    GarbageCollector() = default;
    ~GarbageCollector() = default;

    void collect();
    void clear();
    bool registerResource(const std::shared_ptr<EngineObject>& resource);

    std::vector<std::shared_ptr<EngineObject>> getAliveResources() const;
    const std::vector<std::weak_ptr<EngineObject>>& getTrackedResourcesRef() const;

private:
    mutable std::mutex m_mutex;
    std::vector<std::weak_ptr<EngineObject>> m_trackedResources;
    std::unordered_set<const EngineObject*> m_registeredPtrs;
};
