#pragma once

#include "ActorComponent.h"
#include "Actor.h"
#include "../ECS/ECS.h"
#include "../ECS/Components.h"

/// SceneComponent – represents a transform / spatial location.
/// Every Actor has one implicitly (via ECS TransformComponent),
/// but this component can be used to add extra logical transforms.
class SceneComponent : public ActorComponent
{
public:
	void getPosition(float outPos[3]) const
	{
		if (auto* owner = getOwner())
			owner->getPosition(outPos);
	}

	void setPosition(float x, float y, float z)
	{
		if (auto* owner = getOwner())
			owner->setPosition(x, y, z);
	}

	void getRotation(float outRot[3]) const
	{
		if (auto* owner = getOwner())
			owner->getRotation(outRot);
	}

	void setRotation(float x, float y, float z)
	{
		if (auto* owner = getOwner())
			owner->setRotation(x, y, z);
	}

	void getScale(float outScale[3]) const
	{
		if (auto* owner = getOwner())
			owner->getScale(outScale);
	}

	void setScale(float x, float y, float z)
	{
		if (auto* owner = getOwner())
			owner->setScale(x, y, z);
	}
};

/// StaticMeshComponent – renders a mesh with a material.
class StaticMeshComponent : public ActorComponent
{
public:
	void onRegister() override
	{
		if (auto* owner = getOwner())
		{
			auto& ecs = ECS::Manager();
			ECS::Entity e = owner->getEntity();
			if (!ecs.hasComponent<ECS::MeshComponent>(e))
				ecs.addComponent<ECS::MeshComponent>(e);
			if (!ecs.hasComponent<ECS::MaterialComponent>(e))
				ecs.addComponent<ECS::MaterialComponent>(e);
		}
	}

	void onUnregister() override
	{
		if (auto* owner = getOwner())
		{
			auto& ecs = ECS::Manager();
			ECS::Entity e = owner->getEntity();
			ecs.removeComponent<ECS::MeshComponent>(e);
			ecs.removeComponent<ECS::MaterialComponent>(e);
		}
	}

	void setMesh(const std::string& assetPath)
	{
		if (auto* owner = getOwner())
		{
			if (auto* mc = ECS::Manager().getComponent<ECS::MeshComponent>(owner->getEntity()))
				mc->meshAssetPath = assetPath;
		}
	}

	std::string getMesh() const
	{
		if (auto* owner = getOwner())
		{
			if (const auto* mc = ECS::Manager().getComponent<ECS::MeshComponent>(owner->getEntity()))
				return mc->meshAssetPath;
		}
		return {};
	}

	void setMaterial(const std::string& assetPath)
	{
		if (auto* owner = getOwner())
		{
			if (auto* mc = ECS::Manager().getComponent<ECS::MaterialComponent>(owner->getEntity()))
				mc->materialAssetPath = assetPath;
		}
	}

	std::string getMaterial() const
	{
		if (auto* owner = getOwner())
		{
			if (const auto* mc = ECS::Manager().getComponent<ECS::MaterialComponent>(owner->getEntity()))
				return mc->materialAssetPath;
		}
		return {};
	}

	void setColorTint(float r, float g, float b)
	{
		if (auto* owner = getOwner())
		{
			if (auto* mc = ECS::Manager().getComponent<ECS::MaterialComponent>(owner->getEntity()))
			{
				mc->overrides.colorTint[0] = r;
				mc->overrides.colorTint[1] = g;
				mc->overrides.colorTint[2] = b;
				mc->overrides.hasColorTint = true;
			}
		}
	}
};

/// LightComponent – wraps ECS::LightComponent.
class ActorLightComponent : public ActorComponent
{
public:
	void onRegister() override
	{
		if (auto* owner = getOwner())
		{
			auto& ecs = ECS::Manager();
			if (!ecs.hasComponent<ECS::LightComponent>(owner->getEntity()))
				ecs.addComponent<ECS::LightComponent>(owner->getEntity());
		}
	}

	void onUnregister() override
	{
		if (auto* owner = getOwner())
			ECS::Manager().removeComponent<ECS::LightComponent>(owner->getEntity());
	}

	void setColor(float r, float g, float b)
	{
		if (auto* lc = getECSLight())
		{
			lc->color[0] = r;
			lc->color[1] = g;
			lc->color[2] = b;
		}
	}

	void getColor(float outColor[3]) const
	{
		if (const auto* lc = getECSLightConst())
		{
			outColor[0] = lc->color[0];
			outColor[1] = lc->color[1];
			outColor[2] = lc->color[2];
		}
	}

	void setIntensity(float intensity)
	{
		if (auto* lc = getECSLight())
			lc->intensity = intensity;
	}

	float getIntensity() const
	{
		if (const auto* lc = getECSLightConst())
			return lc->intensity;
		return 0.0f;
	}

	void setRange(float range)
	{
		if (auto* lc = getECSLight())
			lc->range = range;
	}

	float getRange() const
	{
		if (const auto* lc = getECSLightConst())
			return lc->range;
		return 0.0f;
	}

	void setLightType(ECS::LightComponent::LightType type)
	{
		if (auto* lc = getECSLight())
			lc->type = type;
	}

	ECS::LightComponent::LightType getLightType() const
	{
		if (const auto* lc = getECSLightConst())
			return lc->type;
		return ECS::LightComponent::LightType::Point;
	}

private:
	ECS::LightComponent* getECSLight() const
	{
		if (auto* owner = getOwner())
			return ECS::Manager().getComponent<ECS::LightComponent>(owner->getEntity());
		return nullptr;
	}

	const ECS::LightComponent* getECSLightConst() const
	{
		if (auto* owner = getOwner())
			return const_cast<const ECS::ECSManager&>(ECS::Manager()).getComponent<ECS::LightComponent>(owner->getEntity());
		return nullptr;
	}
};

/// PhysicsBodyComponent – wraps ECS::PhysicsComponent + ECS::CollisionComponent.
class PhysicsBodyComponent : public ActorComponent
{
public:
	void onRegister() override
	{
		if (auto* owner = getOwner())
		{
			auto& ecs = ECS::Manager();
			ECS::Entity e = owner->getEntity();
			if (!ecs.hasComponent<ECS::PhysicsComponent>(e))
				ecs.addComponent<ECS::PhysicsComponent>(e);
			if (!ecs.hasComponent<ECS::CollisionComponent>(e))
				ecs.addComponent<ECS::CollisionComponent>(e);
		}
	}

	void onUnregister() override
	{
		if (auto* owner = getOwner())
		{
			auto& ecs = ECS::Manager();
			ECS::Entity e = owner->getEntity();
			ecs.removeComponent<ECS::PhysicsComponent>(e);
			ecs.removeComponent<ECS::CollisionComponent>(e);
		}
	}

	void setMotionType(ECS::PhysicsComponent::MotionType type)
	{
		if (auto* pc = getECSPhysics())
			pc->motionType = type;
	}

	ECS::PhysicsComponent::MotionType getMotionType() const
	{
		if (const auto* pc = getECSPhysicsConst())
			return pc->motionType;
		return ECS::PhysicsComponent::MotionType::Dynamic;
	}

	void setMass(float mass)
	{
		if (auto* pc = getECSPhysics())
			pc->mass = mass;
	}

	float getMass() const
	{
		if (const auto* pc = getECSPhysicsConst())
			return pc->mass;
		return 1.0f;
	}

	void setGravityFactor(float factor)
	{
		if (auto* pc = getECSPhysics())
			pc->gravityFactor = factor;
	}

	void setVelocity(float x, float y, float z)
	{
		if (auto* pc = getECSPhysics())
		{
			pc->velocity[0] = x;
			pc->velocity[1] = y;
			pc->velocity[2] = z;
		}
	}

	void getVelocity(float outVel[3]) const
	{
		if (const auto* pc = getECSPhysicsConst())
		{
			outVel[0] = pc->velocity[0];
			outVel[1] = pc->velocity[1];
			outVel[2] = pc->velocity[2];
		}
	}

	void setColliderType(ECS::CollisionComponent::ColliderType type)
	{
		if (auto* cc = getECSCollision())
			cc->colliderType = type;
	}

	void setColliderSize(float x, float y, float z)
	{
		if (auto* cc = getECSCollision())
		{
			cc->colliderSize[0] = x;
			cc->colliderSize[1] = y;
			cc->colliderSize[2] = z;
		}
	}

	void setSensor(bool isSensor)
	{
		if (auto* cc = getECSCollision())
			cc->isSensor = isSensor;
	}

	bool isSensor() const
	{
		if (const auto* cc = getECSCollisionConst())
			return cc->isSensor;
		return false;
	}

private:
	ECS::PhysicsComponent* getECSPhysics() const
	{
		if (auto* owner = getOwner())
			return ECS::Manager().getComponent<ECS::PhysicsComponent>(owner->getEntity());
		return nullptr;
	}

	const ECS::PhysicsComponent* getECSPhysicsConst() const
	{
		if (auto* owner = getOwner())
			return const_cast<const ECS::ECSManager&>(ECS::Manager()).getComponent<ECS::PhysicsComponent>(owner->getEntity());
		return nullptr;
	}

	ECS::CollisionComponent* getECSCollision() const
	{
		if (auto* owner = getOwner())
			return ECS::Manager().getComponent<ECS::CollisionComponent>(owner->getEntity());
		return nullptr;
	}

	const ECS::CollisionComponent* getECSCollisionConst() const
	{
		if (auto* owner = getOwner())
			return const_cast<const ECS::ECSManager&>(ECS::Manager()).getComponent<ECS::CollisionComponent>(owner->getEntity());
		return nullptr;
	}
};

/// CameraActorComponent – wraps ECS::CameraComponent.
class CameraActorComponent : public ActorComponent
{
public:
	void onRegister() override
	{
		if (auto* owner = getOwner())
		{
			auto& ecs = ECS::Manager();
			if (!ecs.hasComponent<ECS::CameraComponent>(owner->getEntity()))
				ecs.addComponent<ECS::CameraComponent>(owner->getEntity());
		}
	}

	void onUnregister() override
	{
		if (auto* owner = getOwner())
			ECS::Manager().removeComponent<ECS::CameraComponent>(owner->getEntity());
	}

	void setFOV(float fov)
	{
		if (auto* cc = getECSCamera())
			cc->fov = fov;
	}

	float getFOV() const
	{
		if (const auto* cc = getECSCameraConst())
			return cc->fov;
		return 60.0f;
	}

	void setNearClip(float near) { if (auto* cc = getECSCamera()) cc->nearClip = near; }
	void setFarClip(float far)   { if (auto* cc = getECSCamera()) cc->farClip = far; }

	void setActive(bool active)
	{
		if (auto* cc = getECSCamera())
			cc->isActive = active;
	}

	bool isActive() const
	{
		if (const auto* cc = getECSCameraConst())
			return cc->isActive;
		return false;
	}

private:
	ECS::CameraComponent* getECSCamera() const
	{
		if (auto* owner = getOwner())
			return ECS::Manager().getComponent<ECS::CameraComponent>(owner->getEntity());
		return nullptr;
	}

	const ECS::CameraComponent* getECSCameraConst() const
	{
		if (auto* owner = getOwner())
			return const_cast<const ECS::ECSManager&>(ECS::Manager()).getComponent<ECS::CameraComponent>(owner->getEntity());
		return nullptr;
	}
};

/// AudioActorComponent – wraps ECS::AudioSourceComponent.
class AudioActorComponent : public ActorComponent
{
public:
	void onRegister() override
	{
		if (auto* owner = getOwner())
		{
			auto& ecs = ECS::Manager();
			if (!ecs.hasComponent<ECS::AudioSourceComponent>(owner->getEntity()))
				ecs.addComponent<ECS::AudioSourceComponent>(owner->getEntity());
		}
	}

	void onUnregister() override
	{
		if (auto* owner = getOwner())
			ECS::Manager().removeComponent<ECS::AudioSourceComponent>(owner->getEntity());
	}

	void setAssetPath(const std::string& path)
	{
		if (auto* ac = getECSAudio())
			ac->assetPath = path;
	}

	std::string getAssetPath() const
	{
		if (const auto* ac = getECSAudioConst())
			return ac->assetPath;
		return {};
	}

	void setGain(float gain) { if (auto* ac = getECSAudio()) ac->gain = gain; }
	float getGain() const { if (const auto* ac = getECSAudioConst()) return ac->gain; return 1.0f; }

	void setLoop(bool loop) { if (auto* ac = getECSAudio()) ac->loop = loop; }
	bool isLoop() const { if (const auto* ac = getECSAudioConst()) return ac->loop; return false; }

	void set3D(bool is3D) { if (auto* ac = getECSAudio()) ac->is3D = is3D; }
	bool is3D() const { if (const auto* ac = getECSAudioConst()) return ac->is3D; return false; }

	void setAutoPlay(bool autoPlay) { if (auto* ac = getECSAudio()) ac->autoPlay = autoPlay; }

private:
	ECS::AudioSourceComponent* getECSAudio() const
	{
		if (auto* owner = getOwner())
			return ECS::Manager().getComponent<ECS::AudioSourceComponent>(owner->getEntity());
		return nullptr;
	}

	const ECS::AudioSourceComponent* getECSAudioConst() const
	{
		if (auto* owner = getOwner())
			return const_cast<const ECS::ECSManager&>(ECS::Manager()).getComponent<ECS::AudioSourceComponent>(owner->getEntity());
		return nullptr;
	}
};

/// ParticleActorComponent – wraps ECS::ParticleEmitterComponent.
class ParticleActorComponent : public ActorComponent
{
public:
	void onRegister() override
	{
		if (auto* owner = getOwner())
		{
			auto& ecs = ECS::Manager();
			if (!ecs.hasComponent<ECS::ParticleEmitterComponent>(owner->getEntity()))
				ecs.addComponent<ECS::ParticleEmitterComponent>(owner->getEntity());
		}
	}

	void onUnregister() override
	{
		if (auto* owner = getOwner())
			ECS::Manager().removeComponent<ECS::ParticleEmitterComponent>(owner->getEntity());
	}

	void setEmissionRate(float rate)
	{
		if (auto* pe = getECSParticle())
			pe->emissionRate = rate;
	}

	void setLifetime(float lifetime)
	{
		if (auto* pe = getECSParticle())
			pe->lifetime = lifetime;
	}

	void setSpeed(float speed)
	{
		if (auto* pe = getECSParticle())
			pe->speed = speed;
	}

	void setSize(float size)
	{
		if (auto* pe = getECSParticle())
			pe->size = size;
	}

	void setEnabled(bool enabled)
	{
		if (auto* pe = getECSParticle())
			pe->enabled = enabled;
	}

	void setColor(float r, float g, float b, float a)
	{
		if (auto* pe = getECSParticle())
		{
			pe->colorR = r;
			pe->colorG = g;
			pe->colorB = b;
			pe->colorA = a;
		}
	}

private:
	ECS::ParticleEmitterComponent* getECSParticle() const
	{
		if (auto* owner = getOwner())
			return ECS::Manager().getComponent<ECS::ParticleEmitterComponent>(owner->getEntity());
		return nullptr;
	}
};

/// CharacterControllerActorComponent – wraps ECS::CharacterControllerComponent.
class CharacterControllerActorComponent : public ActorComponent
{
public:
	void onRegister() override
	{
		if (auto* owner = getOwner())
		{
			auto& ecs = ECS::Manager();
			ECS::Entity e = owner->getEntity();
			if (!ecs.hasComponent<ECS::CharacterControllerComponent>(e))
				ecs.addComponent<ECS::CharacterControllerComponent>(e);
			if (!ecs.hasComponent<ECS::CollisionComponent>(e))
				ecs.addComponent<ECS::CollisionComponent>(e);
		}
	}

	void onUnregister() override
	{
		if (auto* owner = getOwner())
		{
			auto& ecs = ECS::Manager();
			ECS::Entity e = owner->getEntity();
			ecs.removeComponent<ECS::CharacterControllerComponent>(e);
		}
	}

	void setRadius(float radius)
	{
		if (auto* cc = getECSCC())
			cc->radius = radius;
	}

	void setHeight(float height)
	{
		if (auto* cc = getECSCC())
			cc->height = height;
	}

	void setMaxSlopeAngle(float degrees)
	{
		if (auto* cc = getECSCC())
			cc->maxSlopeAngle = degrees;
	}

	bool isGrounded() const
	{
		if (const auto* cc = getECSCCConst())
			return cc->isGrounded;
		return false;
	}

	void getVelocity(float outVel[3]) const
	{
		if (const auto* cc = getECSCCConst())
		{
			outVel[0] = cc->velocity[0];
			outVel[1] = cc->velocity[1];
			outVel[2] = cc->velocity[2];
		}
	}

private:
	ECS::CharacterControllerComponent* getECSCC() const
	{
		if (auto* owner = getOwner())
			return ECS::Manager().getComponent<ECS::CharacterControllerComponent>(owner->getEntity());
		return nullptr;
	}

	const ECS::CharacterControllerComponent* getECSCCConst() const
	{
		if (auto* owner = getOwner())
			return const_cast<const ECS::ECSManager&>(ECS::Manager()).getComponent<ECS::CharacterControllerComponent>(owner->getEntity());
		return nullptr;
	}
};
