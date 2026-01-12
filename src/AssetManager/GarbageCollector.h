#pragma once
#include <vector>
#include <memory>
#include "../Basics/EngineObject.h"

class GarbageCollector
{
public:
    GarbageCollector() = default;
    ~GarbageCollector() = default;

    void collect();
    bool registerResource(const std::shared_ptr<EngineObject>& resource);

    std::vector<std::shared_ptr<EngineObject>> getAliveResources() const;
    const std::vector<std::weak_ptr<EngineObject>>& getTrackedResourcesRef() const;

private:
    std::vector<std::weak_ptr<EngineObject>> m_trackedResources;
};
