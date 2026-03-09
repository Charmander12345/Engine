#pragma once
#include "DataStructs/SparseSet.h"
#include "Components.h"
#include "../Asset.h"
#include <array>
#include <bitset>
#include <type_traits>
#include <vector>

namespace ECS
{

struct ECSConfig
{
	unsigned int maxEntities{ 10000 };
};

using Entity = unsigned int;
static constexpr size_t MaxComponentTypes = 11;

enum class ComponentKind : size_t
{
	Transform = 0,
	Mesh,
	Material,
	Light,
	Camera,
	Physics,
	Script,
	Name,
	Collision,
	HeightField,
	Lod
};

template<typename T>
struct ComponentTraits;

template<>
struct ComponentTraits<TransformComponent>
{
	static constexpr ComponentKind kind = ComponentKind::Transform;
};

template<>
struct ComponentTraits<MeshComponent>
{
	static constexpr ComponentKind kind = ComponentKind::Mesh;
};

template<>
struct ComponentTraits<MaterialComponent>
{
	static constexpr ComponentKind kind = ComponentKind::Material;
};

template<>
struct ComponentTraits<LightComponent>
{
	static constexpr ComponentKind kind = ComponentKind::Light;
};

template<>
struct ComponentTraits<CameraComponent>
{
	static constexpr ComponentKind kind = ComponentKind::Camera;
};

template<>
struct ComponentTraits<PhysicsComponent>
{
	static constexpr ComponentKind kind = ComponentKind::Physics;
};

template<>
struct ComponentTraits<ScriptComponent>
{
	static constexpr ComponentKind kind = ComponentKind::Script;
};

template<>
struct ComponentTraits<NameComponent>
{
	static constexpr ComponentKind kind = ComponentKind::Name;
};

template<>
struct ComponentTraits<CollisionComponent>
{
	static constexpr ComponentKind kind = ComponentKind::Collision;
};

template<>
struct ComponentTraits<HeightFieldComponent>
{
	static constexpr ComponentKind kind = ComponentKind::HeightField;
};

template<>
struct ComponentTraits<LodComponent>
{
	static constexpr ComponentKind kind = ComponentKind::Lod;
};

class Schema
{
public:
	Schema() = default;

	template<typename T>
	Schema& require()
	{
		m_required.set(static_cast<size_t>(ComponentTraits<T>::kind));
		return *this;
	}

	bool matches(const std::bitset<MaxComponentTypes>& mask) const
	{
		return (mask & m_required) == m_required;
	}

	const std::bitset<MaxComponentTypes>& getMask() const
	{
		return m_required;
	}

private:
	std::bitset<MaxComponentTypes> m_required{};
};

struct SchemaAssetMatch
{
	Entity entity{ 0 };
	MeshComponent mesh{};
	MaterialComponent material{};
	TransformComponent transform{};
	bool hasTransform{ false };
};

class ECSManager
{
public:
	static ECSManager& Instance();
	void initialize(const ECSConfig& config);

	uint64_t getComponentVersion() const { return m_componentVersion; }

	Entity createEntity();
	bool createEntity(Entity entity);
	bool removeEntity(Entity entity);

	std::vector<Entity> getEntitiesMatchingSchema(const Schema& schema) const;
	std::vector<SchemaAssetMatch> getAssetsMatchingSchema(const Schema& schema) const;
	const Schema& getRenderSchema() const;

	template<typename T>
	bool addComponent(Entity entity, const T& component = {});

	template<typename T>
	bool removeComponent(Entity entity);

	template<typename T>
	bool hasComponent(Entity entity) const;

	template<typename T>
	bool setComponent(Entity entity, const T& component);

	template<typename T>
	bool setComponentAsset(Entity entity, const std::shared_ptr<AssetData>& asset);

	template<typename T>
	T* getComponent(Entity entity);

	template<typename T>
	const T* getComponent(Entity entity) const;


	private:
		ECSManager() = default;
		~ECSManager() = default;
		ECSManager(const ECSManager&) = delete;
		ECSManager& operator=(const ECSManager&) = delete;
	static constexpr size_t MaxEntities = 10000;
	Entity m_nextEntity{ 1 };
	unsigned int m_maxEntities{ MaxEntities };
	uint64_t m_componentVersion{ 0 };

	std::vector<Entity> m_entities;
	std::array<std::bitset<MaxComponentTypes>, MaxEntities> m_entityMasks{};

	Schema m_renderSchema{};
	bool m_renderSchemaInitialized{ false };

	SparseSet<TransformComponent, MaxEntities> m_transformComponents;
	SparseSet<MeshComponent, MaxEntities> m_meshComponents;
	SparseSet<MaterialComponent, MaxEntities> m_materialComponents;
	SparseSet<LightComponent, MaxEntities> m_lightComponents;
	SparseSet<CameraComponent, MaxEntities> m_cameraComponents;
	SparseSet<PhysicsComponent, MaxEntities> m_physicsComponents;
	SparseSet<ScriptComponent, MaxEntities> m_scriptComponents;
	SparseSet<NameComponent, MaxEntities> m_nameComponents;
	SparseSet<CollisionComponent, MaxEntities> m_collisionComponents;
	SparseSet<HeightFieldComponent, MaxEntities> m_heightFieldComponents;
	SparseSet<LodComponent, MaxEntities> m_lodComponents;

	template<typename T>
	SparseSet<T, MaxEntities>& getStorage();

	template<typename T>
	const SparseSet<T, MaxEntities>& getStorage() const;
	};

inline ECSManager& Manager()
{
	return ECSManager::Instance();
}

inline Entity createEntity()
{
	return ECSManager::Instance().createEntity();
}

inline bool removeEntity(Entity entity)
{
	return ECSManager::Instance().removeEntity(entity);
}

inline std::vector<Entity> getEntitiesMatchingSchema(const Schema& schema)
{
	return ECSManager::Instance().getEntitiesMatchingSchema(schema);
}

template<typename T>
inline bool addComponent(Entity entity, const T& component = {})
{
	return ECSManager::Instance().addComponent<T>(entity, component);
}

template<typename T>
inline bool removeComponent(Entity entity)
{
	return ECSManager::Instance().removeComponent<T>(entity);
}

template<typename T>
inline bool hasComponent(Entity entity)
{
	return ECSManager::Instance().hasComponent<T>(entity);
}

template<typename T>
inline bool setComponent(Entity entity, const T& component)
{
	return ECSManager::Instance().setComponent<T>(entity, component);
}

template<typename T>
inline bool setComponentAsset(Entity entity, const std::shared_ptr<AssetData>& asset)
{
	return ECSManager::Instance().setComponentAsset<T>(entity, asset);
}

template<typename T>
inline T* getComponent(Entity entity)
{
	return ECSManager::Instance().getComponent<T>(entity);
}

template<typename T>
inline const T* getComponent(Entity entity)
{
	return ECSManager::Instance().getComponent<T>(entity);
}

template<typename T>
inline SparseSet<T, ECSManager::MaxEntities>& ECSManager::getStorage()
{
	if constexpr (std::is_same_v<T, TransformComponent>)
	{
		return m_transformComponents;
	}
	else if constexpr (std::is_same_v<T, MeshComponent>)
	{
		return m_meshComponents;
	}
	else if constexpr (std::is_same_v<T, MaterialComponent>)
	{
		return m_materialComponents;
	}
	else if constexpr (std::is_same_v<T, LightComponent>)
	{
		return m_lightComponents;
	}
	else if constexpr (std::is_same_v<T, CameraComponent>)
	{
		return m_cameraComponents;
	}
	else if constexpr (std::is_same_v<T, PhysicsComponent>)
	{
		return m_physicsComponents;
	}
	else if constexpr (std::is_same_v<T, NameComponent>)
	{
		return m_nameComponents;
	}
	else if constexpr (std::is_same_v<T, CollisionComponent>)
	{
		return m_collisionComponents;
	}
	else if constexpr (std::is_same_v<T, ScriptComponent>)
	{
		return m_scriptComponents;
	}
	else if constexpr (std::is_same_v<T, HeightFieldComponent>)
	{
		return m_heightFieldComponents;
	}
	else
	{
		static_assert(std::is_same_v<T, LodComponent>, "Unsupported component type");
		return m_lodComponents;
	}
}

template<typename T>
inline const SparseSet<T, ECSManager::MaxEntities>& ECSManager::getStorage() const
{
	if constexpr (std::is_same_v<T, TransformComponent>)
	{
		return m_transformComponents;
	}
	else if constexpr (std::is_same_v<T, MeshComponent>)
	{
		return m_meshComponents;
	}
	else if constexpr (std::is_same_v<T, MaterialComponent>)
	{
		return m_materialComponents;
	}
	else if constexpr (std::is_same_v<T, LightComponent>)
	{
		return m_lightComponents;
	}
	else if constexpr (std::is_same_v<T, CameraComponent>)
	{
		return m_cameraComponents;
	}
	else if constexpr (std::is_same_v<T, PhysicsComponent>)
	{
		return m_physicsComponents;
	}
	else if constexpr (std::is_same_v<T, NameComponent>)
	{
		return m_nameComponents;
	}
	else if constexpr (std::is_same_v<T, CollisionComponent>)
	{
		return m_collisionComponents;
	}
	else if constexpr (std::is_same_v<T, ScriptComponent>)
	{
		return m_scriptComponents;
	}
	else if constexpr (std::is_same_v<T, HeightFieldComponent>)
	{
		return m_heightFieldComponents;
	}
	else
	{
		static_assert(std::is_same_v<T, LodComponent>, "Unsupported component type");
		return m_lodComponents;
	}
}

template<typename T>
inline bool ECSManager::addComponent(Entity entity, const T& component)
{
	if (entity == 0 || entity >= m_maxEntities)
	{
		return false;
	}
	auto& storage = getStorage<T>();
	if (storage.contains(entity))
	{
		return false;
	}
	storage.insert(entity, component);
	m_entityMasks[entity].set(static_cast<size_t>(ComponentTraits<T>::kind));
	++m_componentVersion;
	return true;
}

template<typename T>
inline bool ECSManager::removeComponent(Entity entity)
{
	auto& storage = getStorage<T>();
	if (!storage.contains(entity))
	{
		return false;
	}
	storage.erase(entity);
	m_entityMasks[entity].reset(static_cast<size_t>(ComponentTraits<T>::kind));
	++m_componentVersion;
	return true;
}

template<typename T>
inline bool ECSManager::hasComponent(Entity entity) const
{
	return getStorage<T>().contains(entity);
}

template<typename T>
inline bool ECSManager::setComponent(Entity entity, const T& component)
{
	auto& storage = getStorage<T>();
	if (!storage.contains(entity))
	{
		return false;
	}
	storage.get(entity) = component;
	++m_componentVersion;
	return true;
}

template<typename T>
inline bool ECSManager::setComponentAsset(Entity entity, const std::shared_ptr<AssetData>& asset)
{
	if (!asset)
	{
		return false;
	}
	if constexpr (std::is_same_v<T, MeshComponent>)
	{
		MeshComponent component;
		component.meshAssetPath = asset->getPath();
		component.meshAssetId = asset->getId();
		return setComponent(entity, component);
	}
	else if constexpr (std::is_same_v<T, MaterialComponent>)
	{
		MaterialComponent component;
		component.materialAssetPath = asset->getPath();
		component.materialAssetId = asset->getId();
		return setComponent(entity, component);
	}
	else if constexpr (std::is_same_v<T, ScriptComponent>)
	{
		ScriptComponent component;
		component.scriptPath = asset->getPath();
		component.scriptAssetId = asset->getId();
		return setComponent(entity, component);
	}
	else
	{
		return false;
	}
}

template<typename T>
inline T* ECSManager::getComponent(Entity entity)
{
	auto& storage = getStorage<T>();
	return storage.contains(entity) ? &storage.get(entity) : nullptr;
}

template<typename T>
inline const T* ECSManager::getComponent(Entity entity) const
{
	const auto& storage = getStorage<T>();
	return storage.contains(entity) ? &storage.get(entity) : nullptr;
}
}