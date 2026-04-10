#pragma once

#include "Actor.h"
#include "BuiltinComponents.h"

/// A StaticMeshActor comes pre-configured with a StaticMeshComponent.
/// This is the most common actor for placing meshes in the world.
class StaticMeshActor : public Actor
{
public:
	void beginPlay() override
	{
		m_meshComp = addComponent<StaticMeshComponent>();
	}

	StaticMeshComponent* getMeshComponent() const { return m_meshComp; }

	void setMesh(const std::string& assetPath)
	{
		if (m_meshComp)
			m_meshComp->setMesh(assetPath);
	}

	void setMaterial(const std::string& assetPath)
	{
		if (m_meshComp)
			m_meshComp->setMaterial(assetPath);
	}

private:
	StaticMeshComponent* m_meshComp{ nullptr };
};

/// A PointLightActor comes pre-configured with a LightComponent set to Point.
class PointLightActor : public Actor
{
public:
	void beginPlay() override
	{
		m_lightComp = addComponent<ActorLightComponent>();
		m_lightComp->setLightType(ECS::LightComponent::LightType::Point);
	}

	ActorLightComponent* getLightComponent() const { return m_lightComp; }

	void setColor(float r, float g, float b) { if (m_lightComp) m_lightComp->setColor(r, g, b); }
	void setIntensity(float intensity)        { if (m_lightComp) m_lightComp->setIntensity(intensity); }
	void setRange(float range)                { if (m_lightComp) m_lightComp->setRange(range); }

private:
	ActorLightComponent* m_lightComp{ nullptr };
};

/// A DirectionalLightActor comes pre-configured with a directional light.
class DirectionalLightActor : public Actor
{
public:
	void beginPlay() override
	{
		m_lightComp = addComponent<ActorLightComponent>();
		m_lightComp->setLightType(ECS::LightComponent::LightType::Directional);
	}

	ActorLightComponent* getLightComponent() const { return m_lightComp; }

private:
	ActorLightComponent* m_lightComp{ nullptr };
};

/// A SpotLightActor comes pre-configured with a spot light.
class SpotLightActor : public Actor
{
public:
	void beginPlay() override
	{
		m_lightComp = addComponent<ActorLightComponent>();
		m_lightComp->setLightType(ECS::LightComponent::LightType::Spot);
	}

	ActorLightComponent* getLightComponent() const { return m_lightComp; }

private:
	ActorLightComponent* m_lightComp{ nullptr };
};

/// A CameraActor comes pre-configured with a CameraActorComponent.
class CameraActor : public Actor
{
public:
	void beginPlay() override
	{
		m_cameraComp = addComponent<CameraActorComponent>();
	}

	CameraActorComponent* getCameraComponent() const { return m_cameraComp; }

	void setFOV(float fov)        { if (m_cameraComp) m_cameraComp->setFOV(fov); }
	void setActive(bool active)   { if (m_cameraComp) m_cameraComp->setActive(active); }

private:
	CameraActorComponent* m_cameraComp{ nullptr };
};

/// A PhysicsActor is a mesh with a physics body attached.
class PhysicsActor : public Actor
{
public:
	void beginPlay() override
	{
		m_meshComp = addComponent<StaticMeshComponent>();
		m_physicsComp = addComponent<PhysicsBodyComponent>();
	}

	StaticMeshComponent* getMeshComponent() const    { return m_meshComp; }
	PhysicsBodyComponent* getPhysicsComponent() const { return m_physicsComp; }

private:
	StaticMeshComponent* m_meshComp{ nullptr };
	PhysicsBodyComponent* m_physicsComp{ nullptr };
};

/// A CharacterActor is an actor with a CharacterController for player / NPC movement.
class CharacterActor : public Actor
{
public:
	void beginPlay() override
	{
		m_meshComp = addComponent<StaticMeshComponent>();
		m_ccComp = addComponent<CharacterControllerActorComponent>();
	}

	StaticMeshComponent* getMeshComponent() const                    { return m_meshComp; }
	CharacterControllerActorComponent* getCharacterController() const { return m_ccComp; }

	bool isGrounded() const { return m_ccComp ? m_ccComp->isGrounded() : false; }

private:
	StaticMeshComponent* m_meshComp{ nullptr };
	CharacterControllerActorComponent* m_ccComp{ nullptr };
};

/// An AudioActor is an actor that plays spatial audio.
class AudioActor : public Actor
{
public:
	void beginPlay() override
	{
		m_audioComp = addComponent<AudioActorComponent>();
	}

	AudioActorComponent* getAudioComponent() const { return m_audioComp; }

private:
	AudioActorComponent* m_audioComp{ nullptr };
};

/// A ParticleActor is an actor that emits particles.
class ParticleActor : public Actor
{
public:
	void beginPlay() override
	{
		m_particleComp = addComponent<ParticleActorComponent>();
	}

	ParticleActorComponent* getParticleComponent() const { return m_particleComp; }

private:
	ParticleActorComponent* m_particleComp{ nullptr };
};
