#pragma once 

#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include "EngineObject.h"
#include "MathTypes.h"
#include "../AssetManager/AssetTypes.h"

class EngineLevel : public EngineObject
{
public:
	EngineLevel();
	~EngineLevel();

	std::vector<std::shared_ptr<EngineObject>>& getWorldObjects();
	const std::vector<std::shared_ptr<EngineObject>>& getWorldObjects() const;

	// store instance transforms (by object path)
	const std::unordered_map<std::string, Transform>& getWorldObjectTransforms() const;

	// Legacy: kept for compatibility with older code paths, but world objects are authoritative.
	const std::unordered_map<std::string, std::shared_ptr<EngineObject>>& getLoadedDependencies() const;
	void clearLoadedDependencies();
	void setLoadedDependency(const std::string& path, const std::shared_ptr<EngineObject>& obj);

	bool registerObject(const std::shared_ptr<EngineObject>& object);
	bool unregisterObject(const std::shared_ptr<EngineObject>& object);
	bool setObjectTransform(const std::string& objectPath, const Transform& transform);


private:

	std::vector<std::shared_ptr<EngineObject>> Objects;
	std::unordered_map<std::string, Transform> m_objectTransforms;
	std::unordered_map<std::string, std::shared_ptr<EngineObject>> m_loadedDependencies;

};