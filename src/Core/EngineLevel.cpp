#include "EngineLevel.h"

#include <algorithm>

#include "../Logger/Logger.h"

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
	m_levelScriptPath.clear();
	m_scriptEntitiesPrepared = false;
	if (m_levelData.is_object() && m_levelData.contains("Script") && m_levelData.at("Script").is_string())
	{
		m_levelScriptPath = m_levelData.at("Script").get<std::string>();
	}
	m_hasEditorCamera = false;
	if (m_levelData.is_object() && m_levelData.contains("EditorCamera"))
	{
		const auto& cam = m_levelData.at("EditorCamera");
		if (cam.is_object())
		{
			if (cam.contains("position") && cam.at("position").is_array() && cam.at("position").size() >= 3)
			{
				const auto& p = cam.at("position");
				m_editorCameraPosition = Vec3{ p[0].get<float>(), p[1].get<float>(), p[2].get<float>() };
			}
			if (cam.contains("rotation") && cam.at("rotation").is_array() && cam.at("rotation").size() >= 2)
			{
				const auto& r = cam.at("rotation");
				m_editorCameraRotation = Vec2{ r[0].get<float>(), r[1].get<float>() };
			}
			m_hasEditorCamera = true;
		}
	}
	m_skyboxPath.clear();
	if (m_levelData.is_object() && m_levelData.contains("Skybox") && m_levelData.at("Skybox").is_string())
	{
		m_skyboxPath = m_levelData.at("Skybox").get<std::string>();
	}
	m_ecsPrepared = false;
}

const json& EngineLevel::getLevelData() const
{
	return m_levelData;
}

void EngineLevel::setLevelScriptPath(const std::string& scriptPath)
{
	m_levelScriptPath = scriptPath;
}

const std::string& EngineLevel::getLevelScriptPath() const
{
	return m_levelScriptPath;
}

const std::vector<ECS::Entity>& EngineLevel::getEntities() const
{
	return m_entities;
}

const std::vector<ECS::Entity>& EngineLevel::getScriptEntities() const
{
	return m_scriptEntities;
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
	json j = json{ {"materialAssetPath", component.materialAssetPath} };
	const auto& ov = component.overrides;
	if (ov.hasAnyOverride())
	{
		json ovJson;
		if (ov.hasColorTint)   ovJson["colorTint"]  = serializeFloat3(ov.colorTint);
		if (ov.hasMetallic)    ovJson["metallic"]    = ov.metallic;
		if (ov.hasRoughness)   ovJson["roughness"]   = ov.roughness;
		if (ov.hasShininess)   ovJson["shininess"]   = ov.shininess;
		if (ov.hasSpecularMultiplier) ovJson["specularMultiplier"] = ov.specularMultiplier;
		if (ov.hasEmissive)    ovJson["emissive"]    = serializeFloat3(ov.emissiveColor);
		j["overrides"] = ovJson;
	}
	return j;
}

static void deserializeMaterialComponent(const json& value, ECS::MaterialComponent& component)
{
	if (value.is_object() && value.contains("materialAssetPath"))
	{
		component.materialAssetPath = value.at("materialAssetPath").get<std::string>();
	}
	if (value.is_object() && value.contains("overrides"))
	{
		const auto& ov = value.at("overrides");
		if (ov.contains("colorTint"))
		{
			deserializeFloat3(ov.at("colorTint"), component.overrides.colorTint);
			component.overrides.hasColorTint = true;
		}
		if (ov.contains("metallic"))
		{
			component.overrides.metallic = ov.at("metallic").get<float>();
			component.overrides.hasMetallic = true;
		}
		if (ov.contains("roughness"))
		{
			component.overrides.roughness = ov.at("roughness").get<float>();
			component.overrides.hasRoughness = true;
		}
		if (ov.contains("shininess"))
		{
			component.overrides.shininess = ov.at("shininess").get<float>();
			component.overrides.hasShininess = true;
		}
		if (ov.contains("specularMultiplier"))
		{
			component.overrides.specularMultiplier = ov.at("specularMultiplier").get<float>();
			component.overrides.hasSpecularMultiplier = true;
		}
		if (ov.contains("emissive"))
		{
			deserializeFloat3(ov.at("emissive"), component.overrides.emissiveColor);
			component.overrides.hasEmissive = true;
		}
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

static json serializeCollisionComponent(const ECS::CollisionComponent& component)
{
	return json{
		{"colliderType", static_cast<int>(component.colliderType)},
		{"colliderSize", serializeFloat3(component.colliderSize)},
		{"colliderOffset", serializeFloat3(component.colliderOffset)},
		{"restitution", component.restitution},
		{"friction", component.friction},
		{"isSensor", component.isSensor}
	};
}

static void deserializeCollisionComponent(const json& value, ECS::CollisionComponent& component)
{
	if (!value.is_object()) return;
	if (value.contains("colliderType"))
		component.colliderType = static_cast<ECS::CollisionComponent::ColliderType>(value.at("colliderType").get<int>());
	if (value.contains("colliderSize"))
		deserializeFloat3(value.at("colliderSize"), component.colliderSize);
	if (value.contains("colliderOffset"))
		deserializeFloat3(value.at("colliderOffset"), component.colliderOffset);
	if (value.contains("restitution"))
		component.restitution = value.at("restitution").get<float>();
	if (value.contains("friction"))
		component.friction = value.at("friction").get<float>();
	if (value.contains("isSensor"))
		component.isSensor = value.at("isSensor").get<bool>();
}

static json serializePhysicsComponent(const ECS::PhysicsComponent& component)
{
	return json{
		{"motionType", static_cast<int>(component.motionType)},
		{"mass", component.mass},
		{"gravityFactor", component.gravityFactor},
		{"linearDamping", component.linearDamping},
		{"angularDamping", component.angularDamping},
		{"maxLinearVelocity", component.maxLinearVelocity},
		{"maxAngularVelocity", component.maxAngularVelocity},
		{"motionQuality", static_cast<int>(component.motionQuality)},
		{"allowSleeping", component.allowSleeping},
		{"velocity", serializeFloat3(component.velocity)},
		{"angularVelocity", serializeFloat3(component.angularVelocity)}
	};
}

static void deserializePhysicsComponent(const json& value, ECS::PhysicsComponent& component)
{
	if (!value.is_object()) return;
	if (value.contains("motionType"))
		component.motionType = static_cast<ECS::PhysicsComponent::MotionType>(value.at("motionType").get<int>());
	if (value.contains("mass"))
		component.mass = value.at("mass").get<float>();
	if (value.contains("gravityFactor"))
		component.gravityFactor = value.at("gravityFactor").get<float>();
	if (value.contains("linearDamping"))
		component.linearDamping = value.at("linearDamping").get<float>();
	if (value.contains("angularDamping"))
		component.angularDamping = value.at("angularDamping").get<float>();
	if (value.contains("maxLinearVelocity"))
		component.maxLinearVelocity = value.at("maxLinearVelocity").get<float>();
	if (value.contains("maxAngularVelocity"))
		component.maxAngularVelocity = value.at("maxAngularVelocity").get<float>();
	if (value.contains("motionQuality"))
		component.motionQuality = static_cast<ECS::PhysicsComponent::MotionQuality>(value.at("motionQuality").get<int>());
	if (value.contains("allowSleeping"))
		component.allowSleeping = value.at("allowSleeping").get<bool>();
	if (value.contains("velocity"))
		deserializeFloat3(value.at("velocity"), component.velocity);
	if (value.contains("angularVelocity"))
		deserializeFloat3(value.at("angularVelocity"), component.angularVelocity);
}

/// Backward compatibility: old "Physics" block had collision fields mixed in.
/// Splits into CollisionComponent + PhysicsComponent when old format is detected.
static bool deserializeLegacyPhysics(const json& value, ECS::CollisionComponent& cc, ECS::PhysicsComponent& pc)
{
	if (!value.is_object() || !value.contains("isStatic"))
		return false; // not legacy format

	// Collision fields
	if (value.contains("colliderType"))
		cc.colliderType = static_cast<ECS::CollisionComponent::ColliderType>(value.at("colliderType").get<int>());
	if (value.contains("colliderSize"))
		deserializeFloat3(value.at("colliderSize"), cc.colliderSize);
	if (value.contains("restitution"))
		cc.restitution = value.at("restitution").get<float>();
	if (value.contains("friction"))
		cc.friction = value.at("friction").get<float>();

	// Physics fields
	bool isStatic = value.value("isStatic", false);
	bool isKinematic = value.value("isKinematic", false);
	if (isStatic)
		pc.motionType = ECS::PhysicsComponent::MotionType::Static;
	else if (isKinematic)
		pc.motionType = ECS::PhysicsComponent::MotionType::Kinematic;
	else
		pc.motionType = ECS::PhysicsComponent::MotionType::Dynamic;

	if (value.contains("mass"))
		pc.mass = value.at("mass").get<float>();
	bool useGravity = value.value("useGravity", true);
	pc.gravityFactor = useGravity ? 1.0f : 0.0f;
	if (value.contains("velocity"))
		deserializeFloat3(value.at("velocity"), pc.velocity);
	if (value.contains("angularVelocity"))
		deserializeFloat3(value.at("angularVelocity"), pc.angularVelocity);

	return true;
}

static json serializeLogicComponent(const ECS::LogicComponent& component)
{
	return json{
		{"scriptPath", component.scriptPath},
		{"nativeClassName", component.nativeClassName}
	};
}

static void deserializeLogicComponent(const json& value, ECS::LogicComponent& component)
{
	if (value.is_object())
	{
		if (value.contains("scriptPath"))
			component.scriptPath = value.at("scriptPath").get<std::string>();
		if (value.contains("nativeClassName"))
			component.nativeClassName = value.at("nativeClassName").get<std::string>();
	}
}

static json serializeHeightFieldComponent(const ECS::HeightFieldComponent& component)
{
	return json{
		{"heights", component.heights},
		{"sampleCount", component.sampleCount},
		{"offsetX", component.offsetX},
		{"offsetY", component.offsetY},
		{"offsetZ", component.offsetZ},
		{"scaleX", component.scaleX},
		{"scaleY", component.scaleY},
		{"scaleZ", component.scaleZ}
	};
}

static void deserializeHeightFieldComponent(const json& value, ECS::HeightFieldComponent& component)
{
	if (!value.is_object()) return;
	if (value.contains("heights"))      component.heights     = value.at("heights").get<std::vector<float>>();
	if (value.contains("sampleCount"))  component.sampleCount = value.at("sampleCount").get<int>();
	if (value.contains("offsetX"))      component.offsetX     = value.at("offsetX").get<float>();
	if (value.contains("offsetY"))      component.offsetY     = value.at("offsetY").get<float>();
	if (value.contains("offsetZ"))      component.offsetZ     = value.at("offsetZ").get<float>();
	if (value.contains("scaleX"))       component.scaleX      = value.at("scaleX").get<float>();
	if (value.contains("scaleY"))       component.scaleY      = value.at("scaleY").get<float>();
	if (value.contains("scaleZ"))       component.scaleZ      = value.at("scaleZ").get<float>();
}

static json serializeLodComponent(const ECS::LodComponent& component)
{
	json levelsJson = json::array();
	for (const auto& level : component.levels)
	{
		levelsJson.push_back(json{ {"meshAssetPath", level.meshAssetPath}, {"maxDistance", level.maxDistance} });
	}
	return json{ {"levels", levelsJson} };
}

static void deserializeLodComponent(const json& value, ECS::LodComponent& component)
{
	if (!value.is_object() || !value.contains("levels")) return;
	const auto& levelsJson = value.at("levels");
	if (!levelsJson.is_array()) return;
	for (const auto& levelJson : levelsJson)
	{
		if (!levelJson.is_object()) continue;
		ECS::LodComponent::LodLevel level;
		if (levelJson.contains("meshAssetPath")) level.meshAssetPath = levelJson.at("meshAssetPath").get<std::string>();
		if (levelJson.contains("maxDistance"))   level.maxDistance    = levelJson.at("maxDistance").get<float>();
		component.levels.push_back(std::move(level));
	}
}

static json serializeNameComponent(const ECS::NameComponent& component)
{
	return json{ {"displayName", component.displayName} };
}

static json serializeAnimationComponent(const ECS::AnimationComponent& component)
{
	return json{
		{"currentClipIndex", component.currentClipIndex},
		{"speed", component.speed},
		{"playing", component.playing},
		{"loop", component.loop}
	};
}

static void deserializeAnimationComponent(const json& value, ECS::AnimationComponent& component)
{
	if (!value.is_object()) return;
	if (value.contains("currentClipIndex")) component.currentClipIndex = value.at("currentClipIndex").get<int>();
	if (value.contains("speed"))            component.speed = value.at("speed").get<float>();
	if (value.contains("playing"))          component.playing = value.at("playing").get<bool>();
	if (value.contains("loop"))             component.loop = value.at("loop").get<bool>();
}

static json serializeParticleEmitterComponent(const ECS::ParticleEmitterComponent& component)
{
	return json{
		{"maxParticles",  component.maxParticles},
		{"emissionRate",  component.emissionRate},
		{"lifetime",      component.lifetime},
		{"speed",         component.speed},
		{"speedVariance", component.speedVariance},
		{"size",          component.size},
		{"sizeEnd",       component.sizeEnd},
		{"gravity",       component.gravity},
		{"colorR",        component.colorR},
		{"colorG",        component.colorG},
		{"colorB",        component.colorB},
		{"colorA",        component.colorA},
		{"colorEndR",     component.colorEndR},
		{"colorEndG",     component.colorEndG},
		{"colorEndB",     component.colorEndB},
		{"colorEndA",     component.colorEndA},
		{"coneAngle",     component.coneAngle},
		{"enabled",       component.enabled},
		{"loop",          component.loop}
	};
}

static void deserializeParticleEmitterComponent(const json& value, ECS::ParticleEmitterComponent& component)
{
	if (!value.is_object()) return;
	if (value.contains("maxParticles"))  component.maxParticles  = value.at("maxParticles").get<int>();
	if (value.contains("emissionRate"))  component.emissionRate  = value.at("emissionRate").get<float>();
	if (value.contains("lifetime"))      component.lifetime      = value.at("lifetime").get<float>();
	if (value.contains("speed"))         component.speed         = value.at("speed").get<float>();
	if (value.contains("speedVariance")) component.speedVariance = value.at("speedVariance").get<float>();
	if (value.contains("size"))          component.size          = value.at("size").get<float>();
	if (value.contains("sizeEnd"))       component.sizeEnd       = value.at("sizeEnd").get<float>();
	if (value.contains("gravity"))       component.gravity       = value.at("gravity").get<float>();
	if (value.contains("colorR"))        component.colorR        = value.at("colorR").get<float>();
	if (value.contains("colorG"))        component.colorG        = value.at("colorG").get<float>();
	if (value.contains("colorB"))        component.colorB        = value.at("colorB").get<float>();
	if (value.contains("colorA"))        component.colorA        = value.at("colorA").get<float>();
	if (value.contains("colorEndR"))     component.colorEndR     = value.at("colorEndR").get<float>();
	if (value.contains("colorEndG"))     component.colorEndG     = value.at("colorEndG").get<float>();
	if (value.contains("colorEndB"))     component.colorEndB     = value.at("colorEndB").get<float>();
	if (value.contains("colorEndA"))     component.colorEndA     = value.at("colorEndA").get<float>();
	if (value.contains("coneAngle"))     component.coneAngle     = value.at("coneAngle").get<float>();
	if (value.contains("enabled"))       component.enabled       = value.at("enabled").get<bool>();
	if (value.contains("loop"))          component.loop          = value.at("loop").get<bool>();
}

static void deserializeNameComponent(const json& value, ECS::NameComponent& component)
{
	if (value.is_object() && value.contains("displayName"))
	{
		component.displayName = value.at("displayName").get<std::string>();
	}
}

bool EngineLevel::prepareEcs()
{
	if (m_ecsPrepared || m_ecsPreparing)
	{
		return true;
	}
	m_ecsPreparing = true;

	Logger::Instance().log(Logger::Category::Engine,
		"EngineLevel: prepareEcs start",
		Logger::LogLevel::INFO);

	m_ecs = &ECS::ECSManager::Instance();
	m_ecs->initialize({});
	m_entities.clear();
	m_scriptEntities.clear();
	m_scriptEntitiesPrepared = false;

	m_suppressEntityListNotifications = true;
	if (!m_levelData.is_object() || !m_levelData.contains("Entities"))
	{
		m_ecsPrepared = true;
		m_suppressEntityListNotifications = false;
		for (auto& callback : m_entityListChangedCallbacks)
		{
			if (callback)
			{
				callback();
			}
		}
		m_ecsPreparing = false;
		return true;
	}

	const auto& entitiesJson = m_levelData.at("Entities");
	if (!entitiesJson.is_array())
	{
		m_ecsPrepared = true;
		m_suppressEntityListNotifications = false;
		for (auto& callback : m_entityListChangedCallbacks)
		{
			if (callback)
			{
				callback();
			}
		}
		m_ecsPreparing = false;
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
						const auto& physJson = componentsJson.at("Physics");
						// Check for legacy format (has "isStatic" field)
						ECS::CollisionComponent cc;
						ECS::PhysicsComponent pc;
						if (deserializeLegacyPhysics(physJson, cc, pc))
						{
							// Legacy: single Physics block → split into both components
							m_ecs->addComponent<ECS::CollisionComponent>(entity, cc);
							m_ecs->addComponent<ECS::PhysicsComponent>(entity, pc);
						}
						else
						{
							// New format
							deserializePhysicsComponent(physJson, pc);
							m_ecs->addComponent<ECS::PhysicsComponent>(entity, pc);
						}
					}
					if (componentsJson.contains("Collision"))
					{
						ECS::CollisionComponent component;
						deserializeCollisionComponent(componentsJson.at("Collision"), component);
						m_ecs->addComponent<ECS::CollisionComponent>(entity, component);
					}
				if (componentsJson.contains("Logic"))
				{
					ECS::LogicComponent component;
					deserializeLogicComponent(componentsJson.at("Logic"), component);
					m_ecs->addComponent<ECS::LogicComponent>(entity, component);
				}
				else if (componentsJson.contains("Script"))
				{
					// Backward compat: migrate old Script component
					ECS::LogicComponent component;
					const auto& sj = componentsJson.at("Script");
					if (sj.is_object() && sj.contains("scriptPath"))
						component.scriptPath = sj.at("scriptPath").get<std::string>();
					// Also check for old NativeScript in same entity
					if (componentsJson.contains("NativeScript"))
					{
						const auto& nj = componentsJson.at("NativeScript");
						if (nj.is_object() && nj.contains("className"))
							component.nativeClassName = nj.at("className").get<std::string>();
					}
					m_ecs->addComponent<ECS::LogicComponent>(entity, component);
				}
				else if (componentsJson.contains("NativeScript"))
				{
					// Backward compat: migrate old NativeScript component
					ECS::LogicComponent component;
					const auto& nj = componentsJson.at("NativeScript");
					if (nj.is_object() && nj.contains("className"))
						component.nativeClassName = nj.at("className").get<std::string>();
					m_ecs->addComponent<ECS::LogicComponent>(entity, component);
				}
				if (componentsJson.contains("HeightField"))
				{
					ECS::HeightFieldComponent component;
					deserializeHeightFieldComponent(componentsJson.at("HeightField"), component);
					m_ecs->addComponent<ECS::HeightFieldComponent>(entity, component);
				}
				if (componentsJson.contains("Name"))
				{
					ECS::NameComponent component;
					deserializeNameComponent(componentsJson.at("Name"), component);
					m_ecs->addComponent<ECS::NameComponent>(entity, component);
				}
				if (componentsJson.contains("Lod"))
				{
					ECS::LodComponent component;
					deserializeLodComponent(componentsJson.at("Lod"), component);
					m_ecs->addComponent<ECS::LodComponent>(entity, component);
				}
				if (componentsJson.contains("Animation"))
				{
					ECS::AnimationComponent component;
					deserializeAnimationComponent(componentsJson.at("Animation"), component);
					m_ecs->addComponent<ECS::AnimationComponent>(entity, component);
				}
if (componentsJson.contains("ParticleEmitter"))
{
	ECS::ParticleEmitterComponent component;
	deserializeParticleEmitterComponent(componentsJson.at("ParticleEmitter"), component);
	m_ecs->addComponent<ECS::ParticleEmitterComponent>(entity, component);
}
			}
		}

		if (!m_ecs->hasComponent<ECS::NameComponent>(entity))
		{
			ECS::NameComponent component;
			component.displayName = "Entity " + std::to_string(entity);
			m_ecs->addComponent<ECS::NameComponent>(entity, component);
		}

		onEntityAdded(entity);
	}

	Logger::Instance().log(Logger::Category::Engine,
		"EngineLevel: prepareEcs built entities: " + std::to_string(m_entities.size()),
		Logger::LogLevel::INFO);

	buildScriptEntityCache();
	m_ecsPrepared = true;
	m_suppressEntityListNotifications = false;
	for (auto& callback : m_entityListChangedCallbacks)
	{
		if (callback)
		{
			callback();
		}
	}
	Logger::Instance().log(Logger::Category::Engine,
		"EngineLevel: prepareEcs done",
		Logger::LogLevel::INFO);
	m_ecsPreparing = false;
	return true;
}

void EngineLevel::onEntityAdded(ECS::Entity entity)
{
	if (entity == 0)
	{
		return;
	}
	if (std::find(m_entities.begin(), m_entities.end(), entity) == m_entities.end())
	{
		m_entities.push_back(entity);
	}
	if (m_ecs)
	{
		const auto* logic = m_ecs->getComponent<ECS::LogicComponent>(entity);
		if (logic && !logic->scriptPath.empty())
		{
			if (std::find(m_scriptEntities.begin(), m_scriptEntities.end(), entity) == m_scriptEntities.end())
			{
				m_scriptEntities.push_back(entity);
			}
		}
	}
	else
	{
		m_scriptEntitiesPrepared = false;
	}

	if (!m_suppressEntityListNotifications)
	{
		setIsSaved(false);
		if (m_onDirtyCallback)
		{
			m_onDirtyCallback();
		}
	}

	if (!m_suppressEntityListNotifications)
	{
		for (auto& callback : m_entityListChangedCallbacks)
		{
			if (callback)
			{
				callback();
			}
		}
	}
}

void EngineLevel::onEntityRemoved(ECS::Entity entity)
{
	if (entity == 0)
	{
		return;
	}
	m_entities.erase(std::remove(m_entities.begin(), m_entities.end(), entity), m_entities.end());
	m_scriptEntities.erase(std::remove(m_scriptEntities.begin(), m_scriptEntities.end(), entity), m_scriptEntities.end());

	if (!m_suppressEntityListNotifications)
	{
		setIsSaved(false);
		if (m_onDirtyCallback)
		{
			m_onDirtyCallback();
		}
	}

	if (!m_suppressEntityListNotifications)
	{
		for (auto& callback : m_entityListChangedCallbacks)
		{
			if (callback)
			{
				callback();
			}
		}
	}
}

void EngineLevel::registerEntityListChangedCallback(std::function<void()> callback)
{
	m_entityListChangedCallbacks.push_back(std::move(callback));
}

void EngineLevel::buildScriptEntityCache()
{
	if (m_scriptEntitiesPrepared)
	{
		return;
	}

	m_scriptEntities.clear();
	if (!m_ecs)
	{
		m_scriptEntitiesPrepared = true;
		return;
	}

	m_scriptEntitiesPrepared = false;

	for (const auto entity : m_entities)
	{
		const auto* logic = m_ecs->getComponent<ECS::LogicComponent>(entity);
		if (!logic || logic->scriptPath.empty())
		{
			continue;
		}
		m_scriptEntities.push_back(entity);
	}

	Logger::Instance().log(Logger::Category::Engine,
		"EngineLevel: script entities cached: " + std::to_string(m_scriptEntities.size()),
		Logger::LogLevel::INFO);

	m_scriptEntitiesPrepared = true;
}

json EngineLevel::serializeEcsEntities() const
{
	if (m_entities.empty() && m_levelData.is_object() && m_levelData.contains("Entities"))
	{
		const auto& entitiesJson = m_levelData.at("Entities");
		if (entitiesJson.is_array())
		{
			return entitiesJson;
		}
	}

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
		if (const auto* component = ecs.getComponent<ECS::CollisionComponent>(entity))
		{
			componentsJson["Collision"] = serializeCollisionComponent(*component);
		}
		if (const auto* component = ecs.getComponent<ECS::LogicComponent>(entity))
		{
			componentsJson["Logic"] = serializeLogicComponent(*component);
		}
		if (const auto* component = ecs.getComponent<ECS::HeightFieldComponent>(entity))
		{
			componentsJson["HeightField"] = serializeHeightFieldComponent(*component);
		}
		if (const auto* component = ecs.getComponent<ECS::NameComponent>(entity))
		{
			componentsJson["Name"] = serializeNameComponent(*component);
		}
		if (const auto* component = ecs.getComponent<ECS::LodComponent>(entity))
		{
			componentsJson["Lod"] = serializeLodComponent(*component);
		}
		if (const auto* component = ecs.getComponent<ECS::AnimationComponent>(entity))
		{
			componentsJson["Animation"] = serializeAnimationComponent(*component);
		}
if (const auto* component = ecs.getComponent<ECS::ParticleEmitterComponent>(entity))
{
	componentsJson["ParticleEmitter"] = serializeParticleEmitterComponent(*component);
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

void EngineLevel::snapshotEcsState()
{
	m_componentSnapshot.clear();
	auto& ecs = m_ecs ? *m_ecs : ECS::ECSManager::Instance();

	for (const auto entity : m_entities)
	{
		EntitySnapshot snap{};
		if (const auto* c = ecs.getComponent<ECS::TransformComponent>(entity))
		{
			snap.transform = *c;
			snap.mask.set(static_cast<size_t>(ECS::ComponentKind::Transform));
		}
		if (const auto* c = ecs.getComponent<ECS::MeshComponent>(entity))
		{
			snap.mesh = *c;
			snap.mask.set(static_cast<size_t>(ECS::ComponentKind::Mesh));
		}
		if (const auto* c = ecs.getComponent<ECS::MaterialComponent>(entity))
		{
			snap.material = *c;
			snap.mask.set(static_cast<size_t>(ECS::ComponentKind::Material));
		}
		if (const auto* c = ecs.getComponent<ECS::LightComponent>(entity))
		{
			snap.light = *c;
			snap.mask.set(static_cast<size_t>(ECS::ComponentKind::Light));
		}
		if (const auto* c = ecs.getComponent<ECS::CameraComponent>(entity))
		{
			snap.camera = *c;
			snap.mask.set(static_cast<size_t>(ECS::ComponentKind::Camera));
		}
		if (const auto* c = ecs.getComponent<ECS::PhysicsComponent>(entity))
		{
			snap.physics = *c;
			snap.mask.set(static_cast<size_t>(ECS::ComponentKind::Physics));
		}
		if (const auto* c = ecs.getComponent<ECS::CollisionComponent>(entity))
		{
			snap.collision = *c;
			snap.mask.set(static_cast<size_t>(ECS::ComponentKind::Collision));
		}
		if (const auto* c = ecs.getComponent<ECS::LogicComponent>(entity))
		{
			snap.logic = *c;
			snap.mask.set(static_cast<size_t>(ECS::ComponentKind::Logic));
		}
		if (const auto* c = ecs.getComponent<ECS::NameComponent>(entity))
		{
			snap.name = *c;
			snap.mask.set(static_cast<size_t>(ECS::ComponentKind::Name));
		}
		if (const auto* c = ecs.getComponent<ECS::HeightFieldComponent>(entity))
		{
			snap.heightField = *c;
			snap.mask.set(static_cast<size_t>(ECS::ComponentKind::HeightField));
		}
		m_componentSnapshot[entity] = std::move(snap);
	}

	Logger::Instance().log(Logger::Category::Engine,
		"EngineLevel: ECS state snapshot saved (" + std::to_string(m_componentSnapshot.size()) + " entities)",
		Logger::LogLevel::INFO);
}

bool EngineLevel::restoreEcsSnapshot()
{
	if (m_componentSnapshot.empty())
	{
		Logger::Instance().log(Logger::Category::Engine,
			"EngineLevel: no ECS snapshot to restore",
			Logger::LogLevel::WARNING);
		return false;
	}

	auto& ecs = m_ecs ? *m_ecs : ECS::ECSManager::Instance();

	for (const auto& [entity, snap] : m_componentSnapshot)
	{
		if (snap.mask.test(static_cast<size_t>(ECS::ComponentKind::Transform)))
			ecs.setComponent<ECS::TransformComponent>(entity, snap.transform);
		if (snap.mask.test(static_cast<size_t>(ECS::ComponentKind::Mesh)))
			ecs.setComponent<ECS::MeshComponent>(entity, snap.mesh);
		if (snap.mask.test(static_cast<size_t>(ECS::ComponentKind::Material)))
			ecs.setComponent<ECS::MaterialComponent>(entity, snap.material);
		if (snap.mask.test(static_cast<size_t>(ECS::ComponentKind::Light)))
			ecs.setComponent<ECS::LightComponent>(entity, snap.light);
		if (snap.mask.test(static_cast<size_t>(ECS::ComponentKind::Camera)))
			ecs.setComponent<ECS::CameraComponent>(entity, snap.camera);
		if (snap.mask.test(static_cast<size_t>(ECS::ComponentKind::Physics)))
			ecs.setComponent<ECS::PhysicsComponent>(entity, snap.physics);
		if (snap.mask.test(static_cast<size_t>(ECS::ComponentKind::Collision)))
			ecs.setComponent<ECS::CollisionComponent>(entity, snap.collision);
		if (snap.mask.test(static_cast<size_t>(ECS::ComponentKind::Logic)))
			ecs.setComponent<ECS::LogicComponent>(entity, snap.logic);
		if (snap.mask.test(static_cast<size_t>(ECS::ComponentKind::Name)))
			ecs.setComponent<ECS::NameComponent>(entity, snap.name);
		if (snap.mask.test(static_cast<size_t>(ECS::ComponentKind::HeightField)))
			ecs.setComponent<ECS::HeightFieldComponent>(entity, snap.heightField);
	}

	m_componentSnapshot.clear();
	buildScriptEntityCache();

	Logger::Instance().log(Logger::Category::Engine,
		"EngineLevel: ECS components restored in-place",
		Logger::LogLevel::INFO);

	return true;
}

