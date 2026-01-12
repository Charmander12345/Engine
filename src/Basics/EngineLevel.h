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

	std::vector<EngineObject>& getWorldObjects();

	// store instance transforms (by object path)
	const std::unordered_map<std::string, Transform>& getWorldObjectTransforms() const;

	// keep strong references to loaded dependencies while the level is active
	const std::unordered_map<std::string, std::shared_ptr<EngineObject>>& getLoadedDependencies() const;
	void clearLoadedDependencies();
	void setLoadedDependency(const std::string& path, const std::shared_ptr<EngineObject>& obj);

	bool registerObject(const EngineObject& object);
	bool unregisterObject(const EngineObject& object);
	bool setObjectTransform(const EngineObject& object, const Transform& transform);


private:

	std::vector<EngineObject> Objects;
	std::unordered_map<std::string, Transform> m_objectTransforms;
	std::unordered_map<std::string, std::shared_ptr<EngineObject>> m_loadedDependencies;

};