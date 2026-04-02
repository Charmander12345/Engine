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
	void setLevelScriptPath(const std::string& scriptPath);
	const std::string& getLevelScriptPath() const;
	bool prepareEcs();
	void buildScriptEntityCache();
	void onEntityAdded(ECS::Entity entity);
	void onEntityRemoved(ECS::Entity entity);
	json serializeEcsEntities() const;
	void snapshotEcsState();
	bool restoreEcsSnapshot();
	const std::vector<ECS::Entity>& getEntities() const;
	const std::vector<ECS::Entity>& getScriptEntities() const;
	void registerEntityListChangedCallback(std::function<void()> callback);
	void setOnDirtyCallback(std::function<void()> callback) { m_onDirtyCallback = std::move(callback); }

	// Reset the prepared state so prepareEcs() runs again on next renderWorld cycle.
	void resetPreparedState() { m_ecsPrepared = false; }

	void setEditorCameraPosition(const Vec3& pos) { m_editorCameraPosition = pos; }
	const Vec3& getEditorCameraPosition() const { return m_editorCameraPosition; }
	void setEditorCameraRotation(const Vec2& rot) { m_editorCameraRotation = rot; }
	const Vec2& getEditorCameraRotation() const { return m_editorCameraRotation; }
	bool hasEditorCamera() const { return m_hasEditorCamera; }
	void setHasEditorCamera(bool has) { m_hasEditorCamera = has; }

	void setSkyboxPath(const std::string& path)
	{
		if (m_skyboxPath == path) return;
		m_skyboxPath = path;
		setIsSaved(false);
		if (m_onDirtyCallback) m_onDirtyCallback();
	}
	const std::string& getSkyboxPath() const { return m_skyboxPath; }

	struct EntitySnapshot
	{
		ECS::TransformComponent transform{};
		ECS::MeshComponent mesh{};
		ECS::MaterialComponent material{};
		ECS::LightComponent light{};
		ECS::CameraComponent camera{};
		ECS::PhysicsComponent physics{};
		ECS::CollisionComponent collision{};
		ECS::LogicComponent logic{};
		ECS::NameComponent name{};
		ECS::HeightFieldComponent heightField{};
		ECS::AnimationComponent animation{};
		std::bitset<ECS::MaxComponentTypes> mask{};
	};

private:

	std::vector<std::shared_ptr<ObjectInstance>> Objects;
	std::vector<group> m_groups;
	std::vector<ECS::Entity> m_entities;
	std::vector<ECS::Entity> m_scriptEntities;
	bool m_scriptEntitiesPrepared{ false };
	std::vector<std::function<void()>> m_entityListChangedCallbacks;
	std::function<void()> m_onDirtyCallback;
	bool m_suppressEntityListNotifications{ false };
	json m_levelData;
	std::string m_levelScriptPath;
	ECS::ECSManager* m_ecs{ nullptr };
	bool m_ecsPrepared{ false };
	bool m_ecsPreparing{ false };
	json m_ecsSnapshot;
	std::unordered_map<ECS::Entity, EntitySnapshot> m_componentSnapshot;
	Vec3 m_editorCameraPosition{};
	Vec2 m_editorCameraRotation{};
	bool m_hasEditorCamera{ false };
	std::string m_skyboxPath;
};