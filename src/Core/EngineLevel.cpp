#include "EngineLevel.h"

#include <algorithm>

EngineLevel::EngineLevel()
{
	this->setAssetType(AssetType::Level);
}

EngineLevel::~EngineLevel()
{

}

void EngineLevel::setLevelData(const json& data)
{
	m_levelData = data;
	m_ecsPrepared = false;
}

const json& EngineLevel::getLevelData() const
{
	return m_levelData;
}

const std::vector<ECS::Entity>& EngineLevel::getEntities() const
{
	return m_entities;
}

static json serializeFloat3(const float values[3])
{
	return json::array({ values[0], values[1], values[2] });
}

static void deserializeFloat3(const json& value, float outValues[3])
{
	if (!value.is_array() || value.size() < 3)
	{
		return;
	}
	for (size_t i = 0; i < 3; ++i)
	{
		outValues[i] = value.at(i).get<float>();
	}
}

static json serializeTransformComponent(const ECS::TransformComponent& component)
{
	return json{
		{"position", serializeFloat3(component.position)},
		{"rotation", serializeFloat3(component.rotation)},
		{"scale", serializeFloat3(component.scale)}
	};
}

static void deserializeTransformComponent(const json& value, ECS::TransformComponent& component)
{
	if (!value.is_object())
	{
		return;
	}
	if (value.contains("position"))
	{
		deserializeFloat3(value.at("position"), component.position);
	}
	if (value.contains("rotation"))
	{
		deserializeFloat3(value.at("rotation"), component.rotation);
	}
	if (value.contains("scale"))
	{
		deserializeFloat3(value.at("scale"), component.scale);
	}
}

static json serializeMeshComponent(const ECS::MeshComponent& component)
{
	return json{ {"meshAssetPath", component.meshAssetPath} };
}

static void deserializeMeshComponent(const json& value, ECS::MeshComponent& component)
{
	if (value.is_object() && value.contains("meshAssetPath"))
	{
		component.meshAssetPath = value.at("meshAssetPath").get<std::string>();
	}
}

static json serializeMaterialComponent(const ECS::MaterialComponent& component)
{
	return json{ {"materialAssetPath", component.materialAssetPath} };
}

static void deserializeMaterialComponent(const json& value, ECS::MaterialComponent& component)
{
	if (value.is_object() && value.contains("materialAssetPath"))
	{
		component.materialAssetPath = value.at("materialAssetPath").get<std::string>();
	}
}

static json serializeLightComponent(const ECS::LightComponent& component)
{
	return json{
		{"type", static_cast<int>(component.type)},
		{"color", serializeFloat3(component.color)},
		{"intensity", component.intensity},
		{"range", component.range},
		{"spotAngle", component.spotAngle}
	};
}

static void deserializeLightComponent(const json& value, ECS::LightComponent& component)
{
	if (!value.is_object())
	{
		return;
	}
	if (value.contains("type"))
	{
		component.type = static_cast<ECS::LightComponent::LightType>(value.at("type").get<int>());
	}
	if (value.contains("color"))
	{
		deserializeFloat3(value.at("color"), component.color);
	}
	if (value.contains("intensity"))
	{
		component.intensity = value.at("intensity").get<float>();
	}
	if (value.contains("range"))
	{
		component.range = value.at("range").get<float>();
	}
	if (value.contains("spotAngle"))
	{
		component.spotAngle = value.at("spotAngle").get<float>();
	}
}

static json serializeCameraComponent(const ECS::CameraComponent& component)
{
	return json{ {"fov", component.fov}, {"nearClip", component.nearClip}, {"farClip", component.farClip} };
}

static void deserializeCameraComponent(const json& value, ECS::CameraComponent& component)
{
	if (!value.is_object())
	{
		return;
	}
	if (value.contains("fov"))
	{
		component.fov = value.at("fov").get<float>();
	}
	if (value.contains("nearClip"))
	{
		component.nearClip = value.at("nearClip").get<float>();
	}
	if (value.contains("farClip"))
	{
		component.farClip = value.at("farClip").get<float>();
	}
}

static json serializePhysicsComponent(const ECS::PhysicsComponent& component)
{
	return json{
		{"colliderType", static_cast<int>(component.colliderType)},
		{"isStatic", component.isStatic},
		{"mass", component.mass}
	};
}

static void deserializePhysicsComponent(const json& value, ECS::PhysicsComponent& component)
{
	if (!value.is_object())
	{
		return;
	}
	if (value.contains("colliderType"))
	{
		component.colliderType = static_cast<ECS::PhysicsComponent::ColliderType>(value.at("colliderType").get<int>());
	}
	if (value.contains("isStatic"))
	{
		component.isStatic = value.at("isStatic").get<bool>();
	}
	if (value.contains("mass"))
	{
		component.mass = value.at("mass").get<float>();
	}
}

static json serializeScriptComponent(const ECS::ScriptComponent& component)
{
	return json{ {"scriptPath", component.scriptPath} };
}

static void deserializeScriptComponent(const json& value, ECS::ScriptComponent& component)
{
	if (value.is_object() && value.contains("scriptPath"))
	{
		component.scriptPath = value.at("scriptPath").get<std::string>();
	}
}

bool EngineLevel::prepareEcs()
{
	if (m_ecsPrepared)
	{
		return true;
	}

	m_ecs = &ECS::ECSManager::Instance();
	m_ecs->initialize({});
	m_entities.clear();

	if (!m_levelData.is_object() || !m_levelData.contains("Entities"))
	{
		m_ecsPrepared = true;
		return true;
	}

	const auto& entitiesJson = m_levelData.at("Entities");
	if (!entitiesJson.is_array())
	{
		m_ecsPrepared = true;
		return true;
	}

	for (const auto& entityJson : entitiesJson)
	{
		if (!entityJson.is_object())
		{
			continue;
		}

		ECS::Entity entity = 0;
		if (entityJson.contains("id"))
		{
			entity = entityJson.at("id").get<ECS::Entity>();
		}
		if (entity != 0)
		{
			if (!m_ecs->createEntity(entity))
			{
				entity = m_ecs->createEntity();
			}
		}
		else
		{
			entity = m_ecs->createEntity();
		}

		if (entity == 0)
		{
			continue;
		}

		if (entityJson.contains("components"))
		{
			const auto& componentsJson = entityJson.at("components");
			if (componentsJson.is_object())
			{
				if (componentsJson.contains("Transform"))
				{
					ECS::TransformComponent component;
					deserializeTransformComponent(componentsJson.at("Transform"), component);
					m_ecs->addComponent<ECS::TransformComponent>(entity, component);
				}
				if (componentsJson.contains("Mesh"))
				{
					ECS::MeshComponent component;
					deserializeMeshComponent(componentsJson.at("Mesh"), component);
					m_ecs->addComponent<ECS::MeshComponent>(entity, component);
				}
				if (componentsJson.contains("Material"))
				{
					ECS::MaterialComponent component;
					deserializeMaterialComponent(componentsJson.at("Material"), component);
					m_ecs->addComponent<ECS::MaterialComponent>(entity, component);
				}
				if (componentsJson.contains("Light"))
				{
					ECS::LightComponent component;
					deserializeLightComponent(componentsJson.at("Light"), component);
					m_ecs->addComponent<ECS::LightComponent>(entity, component);
				}
				if (componentsJson.contains("Camera"))
				{
					ECS::CameraComponent component;
					deserializeCameraComponent(componentsJson.at("Camera"), component);
					m_ecs->addComponent<ECS::CameraComponent>(entity, component);
				}
				if (componentsJson.contains("Physics"))
				{
					ECS::PhysicsComponent component;
					deserializePhysicsComponent(componentsJson.at("Physics"), component);
					m_ecs->addComponent<ECS::PhysicsComponent>(entity, component);
				}
				if (componentsJson.contains("Script"))
				{
					ECS::ScriptComponent component;
					deserializeScriptComponent(componentsJson.at("Script"), component);
					m_ecs->addComponent<ECS::ScriptComponent>(entity, component);
				}
			}
		}

		m_entities.push_back(entity);
	}

	m_ecsPrepared = true;
	return true;
}

json EngineLevel::serializeEcsEntities() const
{
	json entitiesJson = json::array();
	auto& ecs = m_ecs ? *m_ecs : ECS::ECSManager::Instance();

	for (const auto entity : m_entities)
	{
		json entityJson;
		entityJson["id"] = entity;
		json componentsJson = json::object();

		if (const auto* component = ecs.getComponent<ECS::TransformComponent>(entity))
		{
			componentsJson["Transform"] = serializeTransformComponent(*component);
		}
		if (const auto* component = ecs.getComponent<ECS::MeshComponent>(entity))
		{
			componentsJson["Mesh"] = serializeMeshComponent(*component);
		}
		if (const auto* component = ecs.getComponent<ECS::MaterialComponent>(entity))
		{
			componentsJson["Material"] = serializeMaterialComponent(*component);
		}
		if (const auto* component = ecs.getComponent<ECS::LightComponent>(entity))
		{
			componentsJson["Light"] = serializeLightComponent(*component);
		}
		if (const auto* component = ecs.getComponent<ECS::CameraComponent>(entity))
		{
			componentsJson["Camera"] = serializeCameraComponent(*component);
		}
		if (const auto* component = ecs.getComponent<ECS::PhysicsComponent>(entity))
		{
			componentsJson["Physics"] = serializePhysicsComponent(*component);
		}
		if (const auto* component = ecs.getComponent<ECS::ScriptComponent>(entity))
		{
			componentsJson["Script"] = serializeScriptComponent(*component);
		}

		entityJson["components"] = componentsJson;
		entitiesJson.push_back(entityJson);
	}

	return entitiesJson;
}

bool EngineLevel::registerObject(const std::shared_ptr<EngineObject>& object, Transform transform, const std::string& groupID)
{
	if (!object || object->getPath().empty())
	{
		return false;
	}
	Objects.push_back(std::make_shared<ObjectInstance>(ObjectInstance{ object, transform, groupID }));
	return true;
}

bool EngineLevel::unregisterObject(const std::shared_ptr<EngineObject>& object)
{
	if (!object || object->getPath().empty())
	{
		return false;
	}

	for (auto it = Objects.begin(); it != Objects.end(); ++it)
	{
		if (*it && (*it)->object->getPath() == object->getPath())
		{
			Objects.erase(it);
			return true;
		}
	}

	return false;
}

std::vector<std::shared_ptr<EngineLevel::ObjectInstance>>& EngineLevel::getWorldObjects()
{
	return Objects;
}
std::vector<EngineLevel::group>& EngineLevel::getGroups()
{
	return m_groups;
}

bool EngineLevel::setObjectTransform(const std::string& objectPath, const Transform& transform)
{
	if (objectPath.empty())
	{
		return false;
	}

	for (auto& instance : Objects)
	{
		if (instance && instance->object->getPath() == objectPath)
		{
			instance->transform = transform;
			return true;
		}
	}
	return false;
}

void EngineLevel::disableInstancing(const std::string& groupID)
{
	if (groupID.empty())
	{
		return;
	}

	auto groupIt = std::find_if(m_groups.begin(), m_groups.end(), [&](const group& g) { return g.id == groupID; });
	if (groupIt == m_groups.end())
	{
		return;
	}

	// Re-create per-instance world entries so objects are rendered individually again.
	// Keep the entries in the group as well (they remain associated with groupID).
	for (size_t i = 0; i < groupIt->transforms.size(); ++i)
	{
		const Transform& t = groupIt->transforms[i];
		std::shared_ptr<EngineObject> obj;
		if (i < groupIt->objects.size())
		{
			obj = groupIt->objects[i];
		}
		else if (!groupIt->objects.empty())
		{
			// Fallback: if there are fewer objects than transforms, reuse the first.
			obj = groupIt->objects.front();
		}

		if (obj)
		{
			Objects.push_back(std::make_shared<ObjectInstance>(ObjectInstance{ obj, t, groupID }));
		}
	}

	groupIt->isInstanced = false;
}

bool EngineLevel::enableInstancing(const std::string& groupID)
{
	std::vector<ObjectInstance> objectsToInstance;
	objectsToInstance.reserve(Objects.size());

	for (const auto& obj : Objects)
	{
		if (obj && obj->groupID == groupID)
		{
			objectsToInstance.push_back(*obj);
		}
	}

	if (objectsToInstance.empty())
	{
		return false;
	}

	auto groupIt = std::find_if(m_groups.begin(), m_groups.end(), [&](const group& g) { return g.id == groupID; });
	if (groupIt == m_groups.end())
	{
		return false;
	}

	groupIt->isInstanced = true;
	groupIt->objects.clear();
	groupIt->transforms.clear();

	// Store per-instance objects and transforms (do NOT de-duplicate by asset path).
	for (const auto& inst : objectsToInstance)
	{
		groupIt->objects.push_back(inst.object);
		groupIt->transforms.push_back(inst.transform);
	}

	Objects.erase(
		std::remove_if(Objects.begin(), Objects.end(), [&](const std::shared_ptr<ObjectInstance>& inst)
			{
				return inst && inst->groupID == groupID;
			}),
		Objects.end());

	return true;
}

bool EngineLevel::createGroup(const std::string& groupID, bool instanced)
{
	for (const auto& g : m_groups)
	{
		if (g.id == groupID)
		{
			return false;
		}
	}
	group g;
	g.id = groupID;
	g.isInstanced = instanced;
	m_groups.push_back(g);
	return true;
}
void EngineLevel::deleteGroup(const std::string& groupID)
{
	for (auto it = m_groups.begin(); it != m_groups.end(); ++it)
	{
		if (it->id == groupID)
		{
			m_groups.erase(it);
			return;
		}
	}
}

