#pragma once 

#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include "EngineObject.h"
#include "MathTypes.h"
#include "../AssetManager/AssetTypes.h"
#include "ECS/ECS.h"

class EngineLevel : public EngineObject
{
public:

	struct ObjectInstance
	{
		std::shared_ptr<EngineObject> object;
		Transform transform;
		std::string groupID;
	};

	struct group
	{
#define NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(group, id, objects, transforms, isInstanced)
		std::string id;
		std::vector<std::shared_ptr<EngineObject>> objects;
		std::vector<Transform> transforms;
		bool isInstanced{ false };
	};

	EngineLevel();
	~EngineLevel();

	bool enableInstancing(const std::string& groupID);
	void disableInstancing(const std::string& groupID);

	bool createGroup(const std::string& groupID, bool instanced = false);
	void deleteGroup(const std::string& groupID);

	bool addObjectToGroup(const std::string& groupID, const std::shared_ptr<EngineObject>& object, const Transform& transform);
	bool removeObjectFromGroup(const std::string& groupID, const std::shared_ptr<EngineObject>& object);

	std::vector<std::shared_ptr<ObjectInstance>>& getWorldObjects();
	std::vector<group>& getGroups();

	bool registerObject(const std::shared_ptr<EngineObject>& object, Transform transform, const std::string& groupID);
	bool unregisterObject(const std::shared_ptr<EngineObject>& object);
	bool setObjectTransform(const std::string& objectPath, const Transform& transform);

	void setLevelData(const json& data);
	const json& getLevelData() const;
	bool prepareEcs();
	json serializeEcsEntities() const;
	const std::vector<ECS::Entity>& getEntities() const;


private:

	std::vector<std::shared_ptr<ObjectInstance>> Objects;
	std::vector<group> m_groups;
	std::vector<ECS::Entity> m_entities;
	json m_levelData;
	ECS::ECSManager* m_ecs{ nullptr };
	bool m_ecsPrepared{ false };
};