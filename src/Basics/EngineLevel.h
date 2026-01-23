#pragma once 

#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include "EngineObject.h"
#include "Object3D.h"
#include "Object2D.h"
#include "MathTypes.h"
#include "../AssetManager/AssetTypes.h"

class EngineLevel : public EngineObject
{
public:

	void to_json(json& j, const EngineLevel& level) const
	{
		std::vector<std::string> objectPaths;
		for (const auto& objInstance : level.Objects)
		{
			objectPaths.push_back(objInstance->object->getPath());
		}

		std::vector<json> groupsJson;
		for (const auto& grp : level.m_groups)
		{
			json g;
			g["id"] = grp.id;
			std::vector<std::string> groupObjectPaths;
			for (const auto& obj : grp.objects)
			{
				groupObjectPaths.push_back(obj->getPath());
			}
			g["objects"] = groupObjectPaths;
			//g["transforms"] = grp.transforms;
			g["isInstanced"] = grp.isInstanced;
			groupsJson.push_back(g);
		}

		j = json{ {"Objects", objectPaths}, {"Groups", groupsJson} };
	}

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

	void clearLoadedDependencies();

	bool registerObject(const std::shared_ptr<EngineObject>& object, Transform transform, const std::string& groupID);
	bool unregisterObject(const std::shared_ptr<EngineObject>& object);
	bool setObjectTransform(const std::string& objectPath, const Transform& transform);


private:

	std::vector<std::shared_ptr<ObjectInstance>> Objects;
	std::vector<group> m_groups;
};