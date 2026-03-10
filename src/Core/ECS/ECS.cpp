#include <ECS/ECS.h>
#include <algorithm>

namespace ECS
{
	ECSManager& ECSManager::Instance()
	{
		static ECSManager instance;
		return instance;
	}

	void ECSManager::initialize(const ECSConfig& config)
	{
		m_nextEntity = 1;
		m_maxEntities = std::min(config.maxEntities, static_cast<unsigned int>(MaxEntities));
		m_entities.clear();
		for (auto& mask : m_entityMasks)
		{
			mask.reset();
		}
		m_transformComponents.clear();
		m_meshComponents.clear();
		m_materialComponents.clear();
		m_lightComponents.clear();
		m_cameraComponents.clear();
		m_physicsComponents.clear();
		m_scriptComponents.clear();
		m_nameComponents.clear();
		m_collisionComponents.clear();
		m_heightFieldComponents.clear();
		m_lodComponents.clear();
		m_animationComponents.clear();

		m_renderSchema = Schema();
		m_renderSchema.require<MeshComponent>().require<MaterialComponent>();
		m_renderSchemaInitialized = true;
	}

	Entity ECSManager::createEntity()
	{
		if (m_nextEntity >= m_maxEntities)
		{
			return 0;
		}
		const Entity entity = m_nextEntity++;
		m_entities.push_back(entity);
		return entity;
	}

	bool ECSManager::createEntity(Entity entity)
	{
		if (entity == 0 || entity >= m_maxEntities)
		{
			return false;
		}
		if (std::find(m_entities.begin(), m_entities.end(), entity) != m_entities.end())
		{
			return false;
		}
		if (entity >= m_nextEntity)
		{
			m_nextEntity = entity + 1;
		}
		m_entityMasks[entity].reset();
		m_entities.push_back(entity);
		return true;
	}

	bool ECSManager::removeEntity(Entity entity)
	{
		if (entity == 0 || entity >= m_maxEntities)
		{
			return false;
		}
		removeComponent<TransformComponent>(entity);
		removeComponent<MeshComponent>(entity);
		removeComponent<MaterialComponent>(entity);
		removeComponent<LightComponent>(entity);
		removeComponent<CameraComponent>(entity);
		removeComponent<PhysicsComponent>(entity);
		removeComponent<ScriptComponent>(entity);
		removeComponent<NameComponent>(entity);
		removeComponent<CollisionComponent>(entity);
		removeComponent<HeightFieldComponent>(entity);
		removeComponent<LodComponent>(entity);
		removeComponent<AnimationComponent>(entity);
		m_entityMasks[entity].reset();
		m_entities.erase(std::remove(m_entities.begin(), m_entities.end(), entity), m_entities.end());
		return true;
	}

	std::vector<Entity> ECSManager::getEntitiesMatchingSchema(const Schema& schema) const
	{
		std::vector<Entity> matches;
		for (const auto entity : m_entities)
		{
			if (schema.matches(m_entityMasks[entity]))
			{
				matches.push_back(entity);
			}
		}
		return matches;
	}

	std::vector<SchemaAssetMatch> ECSManager::getAssetsMatchingSchema(const Schema& schema) const
	{
		std::vector<SchemaAssetMatch> matches;
		for (const auto entity : m_entities)
		{
			if (!schema.matches(m_entityMasks[entity]))
			{
				continue;
			}
			SchemaAssetMatch match;
			match.entity = entity;
			if (const auto* mesh = getComponent<MeshComponent>(entity))
			{
				match.mesh = *mesh;
			}
			if (const auto* material = getComponent<MaterialComponent>(entity))
			{
				match.material = *material;
			}
			if (const auto* transform = getComponent<TransformComponent>(entity))
			{
				match.transform = *transform;
				match.hasTransform = true;
			}
			matches.push_back(match);
		}
		return matches;
	}

	const Schema& ECSManager::getRenderSchema() const
	{
		return m_renderSchema;
	}
}
