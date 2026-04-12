#include "CDO.h"
#include "ActorRegistry.h"
#include "../ObjectHandle.h"
#include "../ECS/ECS.h"

// ── CDORegistry singleton ───────────────────────────────────────────

CDORegistry& CDORegistry::Instance()
{
	static CDORegistry instance;
	return instance;
}

const ClassDefaultObject* CDORegistry::getCDO(const std::string& className) const
{
	auto it = m_cdos.find(className);
	return it != m_cdos.end() ? &it->second : nullptr;
}

ClassDefaultObject* CDORegistry::getMutableCDO(const std::string& className)
{
	auto it = m_cdos.find(className);
	return it != m_cdos.end() ? &it->second : nullptr;
}

bool CDORegistry::hasCDO(const std::string& className) const
{
	return m_cdos.find(className) != m_cdos.end();
}

void CDORegistry::clear()
{
	m_cdos.clear();
	m_built = false;
}

// ── Helper: capture ECS component if present on entity ──────────────

namespace
{
	template<typename T>
	void captureIfPresent(ECS::ECSManager& ecs, ECS::Entity entity, ClassDefaultObject& cdo)
	{
		if (ecs.hasComponent<T>(entity))
		{
			const T* comp = ecs.getComponent<T>(entity);
			if (comp)
				cdo.captureDefault<T>(*comp);
		}
	}
}

// ── CDO construction for a single class ─────────────────────────────

void CDORegistry::buildCDO(const std::string& className)
{
	auto& registry = ActorRegistry::Instance();
	auto& ecs = ECS::Manager();

	// Create a temporary ECS entity for CDO construction
	ECS::Entity tempEntity = ecs.createEntity();
	if (tempEntity == 0)
		return;

	// Create a temporary actor via the registry factory
	Actor* actor = registry.createActor(className);
	if (!actor)
	{
		ecs.removeEntity(tempEntity);
		return;
	}

	// Initialize the actor (adds TransformComponent, allocates handle)
	actor->internalInitialize(tempEntity, nullptr);

	// Run beginPlay to set up all default components
	actor->internalBeginPlay();

	// Capture the CDO
	ClassDefaultObject cdo(className);
	cdo.setDefaultTickSettings(actor->getTickSettings());

	// Capture all possible ECS component types
	captureIfPresent<ECS::TransformComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::MeshComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::MaterialComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::LightComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::CameraComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::PhysicsComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::CollisionComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::LogicComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::NameComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::HeightFieldComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::LodComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::AnimationComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::CharacterControllerComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::ParticleEmitterComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::AudioSourceComponent>(ecs, tempEntity, cdo);
	captureIfPresent<ECS::ConstraintComponent>(ecs, tempEntity, cdo);

	m_cdos[className] = std::move(cdo);

	// Clean up: release handle, run endPlay, remove entity, delete actor
	actor->internalEndPlay(EEndPlayReason::Destroyed);
	ecs.removeEntity(tempEntity);
	delete actor;
}

// ── Build all CDOs from ActorRegistry ───────────────────────────────

void CDORegistry::buildAll()
{
	m_cdos.clear();

	auto classNames = ActorRegistry::Instance().getRegisteredClassNames();
	for (const auto& name : classNames)
		buildCDO(name);

	m_built = true;
}
