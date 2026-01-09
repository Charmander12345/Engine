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

    const std::vector<std::shared_ptr<EngineObject>>& getTrackedResourcesRef() const;

private:
    bool isRelevant(const std::shared_ptr<EngineObject>& resource) const;

    std::vector<std::shared_ptr<EngineObject>> m_trackedResources;
};
