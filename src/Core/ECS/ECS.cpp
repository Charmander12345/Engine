#include <ECS/ECS.h>
#include "ComponentReflection.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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
		m_logicComponents.clear();
		m_nameComponents.clear();
		m_collisionComponents.clear();
		m_heightFieldComponents.clear();
		m_lodComponents.clear();
		m_animationComponents.clear();
		m_characterControllerComponents.clear();
		m_particleEmitterComponents.clear();
		m_audioSourceComponents.clear();

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
		// Unparent this entity and detach children before removing components
		auto* tc = getComponent<TransformComponent>(entity);
		if (tc)
		{
			// Unparent children (they keep their world transform)
			auto childrenCopy = tc->children; // copy because removeParent modifies the vector
			for (auto child : childrenCopy)
			{
				removeParent(child);
			}
			// Remove from parent's children list
			if (tc->parent != InvalidEntity)
			{
				auto* parentTc = getComponent<TransformComponent>(tc->parent);
				if (parentTc)
				{
					auto& pc = parentTc->children;
					pc.erase(std::remove(pc.begin(), pc.end(), entity), pc.end());
				}
			}
		}
		removeComponent<TransformComponent>(entity);
		removeComponent<MeshComponent>(entity);
		removeComponent<MaterialComponent>(entity);
		removeComponent<LightComponent>(entity);
		removeComponent<CameraComponent>(entity);
		removeComponent<PhysicsComponent>(entity);
		removeComponent<LogicComponent>(entity);
		removeComponent<NameComponent>(entity);
		removeComponent<CollisionComponent>(entity);
		removeComponent<HeightFieldComponent>(entity);
		removeComponent<LodComponent>(entity);
		removeComponent<AnimationComponent>(entity);
		removeComponent<CharacterControllerComponent>(entity);
		removeComponent<ParticleEmitterComponent>(entity);
		removeComponent<AudioSourceComponent>(entity);
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

	void ECSManager::getEntitiesMatchingSchema(const Schema& schema, std::vector<Entity>& out) const
	{
		out.clear();
		for (const auto entity : m_entities)
		{
			if (schema.matches(m_entityMasks[entity]))
			{
				out.push_back(entity);
			}
		}
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

	void ECSManager::getAssetsMatchingSchema(const Schema& schema, std::vector<SchemaAssetMatch>& out) const
	{
		out.clear();
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
			out.push_back(match);
		}
	}

	const Schema& ECSManager::getRenderSchema() const
	{
		return m_renderSchema;
	}

	// ── Transform Parenting ──────────────────────────────────────────

	// Helpers: Euler-angle rotation of a point (ZYX intrinsic order).
	static void rotatePoint(const float euler[3], const float in[3], float out[3])
	{
		constexpr float deg2rad = 3.14159265358979f / 180.0f;
		const float cx = cosf(euler[0] * deg2rad), sx = sinf(euler[0] * deg2rad);
		const float cy = cosf(euler[1] * deg2rad), sy = sinf(euler[1] * deg2rad);
		const float cz = cosf(euler[2] * deg2rad), sz = sinf(euler[2] * deg2rad);

		// Rotation matrix (Y * X * Z – engine convention: Yaw-Pitch-Roll)
		const float r00 = cy * cz + sy * sx * sz;
		const float r01 = -cy * sz + sy * sx * cz;
		const float r02 = sy * cx;
		const float r10 = cx * sz;
		const float r11 = cx * cz;
		const float r12 = -sx;
		const float r20 = -sy * cz + cy * sx * sz;
		const float r21 = sy * sz + cy * sx * cz;
		const float r22 = cy * cx;

		out[0] = r00 * in[0] + r01 * in[1] + r02 * in[2];
		out[1] = r10 * in[0] + r11 * in[1] + r12 * in[2];
		out[2] = r20 * in[0] + r21 * in[1] + r22 * in[2];
	}

	// Inverse-rotate a point (transpose of the rotation matrix).
	static void inverseRotatePoint(const float euler[3], const float in[3], float out[3])
	{
		constexpr float deg2rad = 3.14159265358979f / 180.0f;
		const float cx = cosf(euler[0] * deg2rad), sx = sinf(euler[0] * deg2rad);
		const float cy = cosf(euler[1] * deg2rad), sy = sinf(euler[1] * deg2rad);
		const float cz = cosf(euler[2] * deg2rad), sz = sinf(euler[2] * deg2rad);

		const float r00 = cy * cz + sy * sx * sz;
		const float r01 = -cy * sz + sy * sx * cz;
		const float r02 = sy * cx;
		const float r10 = cx * sz;
		const float r11 = cx * cz;
		const float r12 = -sx;
		const float r20 = -sy * cz + cy * sx * sz;
		const float r21 = sy * sz + cy * sx * cz;
		const float r22 = cy * cx;

		// Transpose
		out[0] = r00 * in[0] + r10 * in[1] + r20 * in[2];
		out[1] = r01 * in[0] + r11 * in[1] + r21 * in[2];
		out[2] = r02 * in[0] + r12 * in[1] + r22 * in[2];
	}

	// Wrap angle to [-180, 180].
	static float wrapAngle(float a)
	{
		a = fmodf(a + 180.0f, 360.0f);
		if (a < 0.0f) a += 360.0f;
		return a - 180.0f;
	}

	bool ECSManager::setParent(Entity entity, Entity parentEntity)
	{
		if (entity == 0 || entity >= m_maxEntities) return false;
		if (parentEntity == 0 || parentEntity >= m_maxEntities) return false;
		if (entity == parentEntity) return false;

		auto* tc = getComponent<TransformComponent>(entity);
		auto* parentTc = getComponent<TransformComponent>(parentEntity);
		if (!tc || !parentTc) return false;

		// Prevent cycles
		if (isAncestorOf(entity, parentEntity)) return false;

		// Remove from old parent first
		if (tc->parent != InvalidEntity && tc->parent != parentEntity)
		{
			removeParent(entity);
			// Re-fetch after removeParent may have changed components
			tc = getComponent<TransformComponent>(entity);
			if (!tc) return false;
		}

		// Compute local transform: local = inverse(parent) * world
		// Local position: inverse-rotate((worldPos - parentPos) / parentScale)
		float diff[3] = {
			tc->position[0] - parentTc->position[0],
			tc->position[1] - parentTc->position[1],
			tc->position[2] - parentTc->position[2]
		};
		float unscaled[3];
		inverseRotatePoint(parentTc->rotation, diff, unscaled);
		tc->localPosition[0] = (parentTc->scale[0] != 0.0f) ? unscaled[0] / parentTc->scale[0] : 0.0f;
		tc->localPosition[1] = (parentTc->scale[1] != 0.0f) ? unscaled[1] / parentTc->scale[1] : 0.0f;
		tc->localPosition[2] = (parentTc->scale[2] != 0.0f) ? unscaled[2] / parentTc->scale[2] : 0.0f;

		// Local rotation: simple subtraction for Euler angles (approximate but consistent)
		tc->localRotation[0] = wrapAngle(tc->rotation[0] - parentTc->rotation[0]);
		tc->localRotation[1] = wrapAngle(tc->rotation[1] - parentTc->rotation[1]);
		tc->localRotation[2] = wrapAngle(tc->rotation[2] - parentTc->rotation[2]);

		// Local scale: ratio
		tc->localScale[0] = (parentTc->scale[0] != 0.0f) ? tc->scale[0] / parentTc->scale[0] : 1.0f;
		tc->localScale[1] = (parentTc->scale[1] != 0.0f) ? tc->scale[1] / parentTc->scale[1] : 1.0f;
		tc->localScale[2] = (parentTc->scale[2] != 0.0f) ? tc->scale[2] / parentTc->scale[2] : 1.0f;

		// Set parent link
		tc->parent = parentEntity;
		parentTc->children.push_back(entity);
		tc->dirty = false; // We just computed world from local

		++m_componentVersion;
		return true;
	}

	bool ECSManager::removeParent(Entity entity)
	{
		if (entity == 0 || entity >= m_maxEntities) return false;

		auto* tc = getComponent<TransformComponent>(entity);
		if (!tc || tc->parent == InvalidEntity) return false;

		Entity oldParent = tc->parent;
		auto* parentTc = getComponent<TransformComponent>(oldParent);

		// World transform is already up-to-date, just copy to local
		std::memcpy(tc->localPosition, tc->position, sizeof(tc->localPosition));
		std::memcpy(tc->localRotation, tc->rotation, sizeof(tc->localRotation));
		std::memcpy(tc->localScale, tc->scale, sizeof(tc->localScale));

		// Remove from parent's children list
		if (parentTc)
		{
			auto& ch = parentTc->children;
			ch.erase(std::remove(ch.begin(), ch.end(), entity), ch.end());
		}

		tc->parent = InvalidEntity;
		tc->dirty = false;

		++m_componentVersion;
		return true;
	}

	Entity ECSManager::getParent(Entity entity) const
	{
		const auto* tc = getComponent<TransformComponent>(entity);
		if (!tc) return InvalidEntity;
		return tc->parent;
	}

	const std::vector<Entity>& ECSManager::getChildren(Entity entity) const
	{
		const auto* tc = getComponent<TransformComponent>(entity);
		if (!tc) return s_emptyChildren;
		return tc->children;
	}

	Entity ECSManager::getRoot(Entity entity) const
	{
		Entity current = entity;
		for (int guard = 0; guard < 1000; ++guard)
		{
			const auto* tc = getComponent<TransformComponent>(current);
			if (!tc || tc->parent == InvalidEntity) return current;
			current = tc->parent;
		}
		return current;
	}

	bool ECSManager::isAncestorOf(Entity ancestor, Entity descendant) const
	{
		Entity current = descendant;
		for (int guard = 0; guard < 1000; ++guard)
		{
			const auto* tc = getComponent<TransformComponent>(current);
			if (!tc || tc->parent == InvalidEntity) return false;
			if (tc->parent == ancestor) return true;
			current = tc->parent;
		}
		return false;
	}

	void ECSManager::markTransformDirty(Entity entity)
	{
		auto* tc = getComponent<TransformComponent>(entity);
		if (!tc || tc->dirty) return;
		tc->dirty = true;
		for (Entity child : tc->children)
		{
			markTransformDirty(child);
		}
	}

	void ECSManager::updateWorldTransforms()
	{
		// Process only root entities (no parent) with dirty descendants.
		// Then propagate top-down.
		for (Entity entity : m_entities)
		{
			auto* tc = getComponent<TransformComponent>(entity);
			if (!tc) continue;
			if (tc->parent != InvalidEntity) continue; // Skip children, will be processed via parent

			// Root: local == world (sync if dirty)
			if (tc->dirty)
			{
				std::memcpy(tc->position, tc->localPosition, sizeof(tc->position));
				std::memcpy(tc->rotation, tc->localRotation, sizeof(tc->rotation));
				std::memcpy(tc->scale, tc->localScale, sizeof(tc->scale));
				tc->dirty = false;
			}

			// Propagate to children
			for (Entity child : tc->children)
			{
				updateWorldTransformRecursive(child, *tc);
			}
		}
	}

	void ECSManager::updateWorldTransformRecursive(Entity entity, const TransformComponent& parentWorld)
	{
		auto* tc = getComponent<TransformComponent>(entity);
		if (!tc) return;

		if (tc->dirty)
		{
			// world scale = parent scale * local scale
			tc->scale[0] = parentWorld.scale[0] * tc->localScale[0];
			tc->scale[1] = parentWorld.scale[1] * tc->localScale[1];
			tc->scale[2] = parentWorld.scale[2] * tc->localScale[2];

			// world rotation = parent rotation + local rotation (Euler approximation)
			tc->rotation[0] = wrapAngle(parentWorld.rotation[0] + tc->localRotation[0]);
			tc->rotation[1] = wrapAngle(parentWorld.rotation[1] + tc->localRotation[1]);
			tc->rotation[2] = wrapAngle(parentWorld.rotation[2] + tc->localRotation[2]);

			// world position = parent position + rotate(local position * parent scale)
			float scaled[3] = {
				tc->localPosition[0] * parentWorld.scale[0],
				tc->localPosition[1] * parentWorld.scale[1],
				tc->localPosition[2] * parentWorld.scale[2]
			};
			float rotated[3];
			rotatePoint(parentWorld.rotation, scaled, rotated);
			tc->position[0] = parentWorld.position[0] + rotated[0];
			tc->position[1] = parentWorld.position[1] + rotated[1];
			tc->position[2] = parentWorld.position[2] + rotated[2];

			tc->dirty = false;
		}

		// Continue to children
		for (Entity child : tc->children)
		{
			updateWorldTransformRecursive(child, *tc);
		}
	}
}
