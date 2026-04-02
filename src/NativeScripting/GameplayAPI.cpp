#include "GameplayAPI.h"
#include "NativeScriptManager.h"

#include "../Core/ECS/ECS.h"
#include "../Logger/Logger.h"

#include <SDL3/SDL.h>

namespace GameplayAPI
{
	// ── ECS / Transform ───────────────────────────────────────────────

	bool getPosition(ECS::Entity entity, float outPos[3])
	{
		const auto* tc = ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(entity);
		if (!tc) return false;
		outPos[0] = tc->position[0];
		outPos[1] = tc->position[1];
		outPos[2] = tc->position[2];
		return true;
	}

	bool setPosition(ECS::Entity entity, const float pos[3])
	{
		auto* tc = ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(entity);
		if (!tc) return false;
		tc->position[0] = pos[0];
		tc->position[1] = pos[1];
		tc->position[2] = pos[2];
		return true;
	}

	bool getRotation(ECS::Entity entity, float outRot[3])
	{
		const auto* tc = ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(entity);
		if (!tc) return false;
		outRot[0] = tc->rotation[0];
		outRot[1] = tc->rotation[1];
		outRot[2] = tc->rotation[2];
		return true;
	}

	bool setRotation(ECS::Entity entity, const float rot[3])
	{
		auto* tc = ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(entity);
		if (!tc) return false;
		tc->rotation[0] = rot[0];
		tc->rotation[1] = rot[1];
		tc->rotation[2] = rot[2];
		return true;
	}

	bool getScale(ECS::Entity entity, float outScale[3])
	{
		const auto* tc = ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(entity);
		if (!tc) return false;
		outScale[0] = tc->scale[0];
		outScale[1] = tc->scale[1];
		outScale[2] = tc->scale[2];
		return true;
	}

	bool setScale(ECS::Entity entity, const float scale[3])
	{
		auto* tc = ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(entity);
		if (!tc) return false;
		tc->scale[0] = scale[0];
		tc->scale[1] = scale[1];
		tc->scale[2] = scale[2];
		return true;
	}

	// ── Entity management ─────────────────────────────────────────────

	ECS::Entity createEntity()
	{
		return ECS::ECSManager::Instance().createEntity();
	}

	bool removeEntity(ECS::Entity entity)
	{
		return ECS::ECSManager::Instance().removeEntity(entity);
	}

	ECS::Entity findEntityByName(const char* name)
	{
		if (!name) return 0;

		ECS::Schema schema;
		schema.require<ECS::NameComponent>();
		auto entities = ECS::ECSManager::Instance().getEntitiesMatchingSchema(schema);

		for (auto entity : entities)
		{
			const auto* nc = ECS::ECSManager::Instance().getComponent<ECS::NameComponent>(entity);
			if (nc && nc->displayName == name)
			{
				return entity;
			}
		}
		return 0;
	}

	// ── Physics ───────────────────────────────────────────────────────

	void setVelocity(ECS::Entity entity, const float vel[3])
	{
		auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(entity);
		if (!pc) return;
		pc->velocity[0] = vel[0];
		pc->velocity[1] = vel[1];
		pc->velocity[2] = vel[2];
	}

	void getVelocity(ECS::Entity entity, float outVel[3])
	{
		const auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(entity);
		if (!pc)
		{
			outVel[0] = outVel[1] = outVel[2] = 0.0f;
			return;
		}
		outVel[0] = pc->velocity[0];
		outVel[1] = pc->velocity[1];
		outVel[2] = pc->velocity[2];
	}

	void addForce(ECS::Entity entity, const float force[3])
	{
		auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(entity);
		if (!pc) return;
		if (pc->motionType != ECS::PhysicsComponent::MotionType::Dynamic || pc->mass <= 0.0f) return;
		const float invMass = 1.0f / pc->mass;
		pc->velocity[0] += force[0] * invMass;
		pc->velocity[1] += force[1] * invMass;
		pc->velocity[2] += force[2] * invMass;
	}

	void addImpulse(ECS::Entity entity, const float impulse[3])
	{
		auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(entity);
		if (!pc) return;
		if (pc->motionType != ECS::PhysicsComponent::MotionType::Dynamic) return;
		pc->velocity[0] += impulse[0];
		pc->velocity[1] += impulse[1];
		pc->velocity[2] += impulse[2];
	}

	// ── Logging ───────────────────────────────────────────────────────

	void logInfo(const char* msg)
	{
		Logger::Instance().log(Logger::Category::Scripting, msg ? msg : "", Logger::LogLevel::INFO);
	}

	void logWarning(const char* msg)
	{
		Logger::Instance().log(Logger::Category::Scripting, msg ? msg : "", Logger::LogLevel::WARNING);
	}

	void logError(const char* msg)
	{
		Logger::Instance().log(Logger::Category::Scripting, msg ? msg : "", Logger::LogLevel::ERROR);
	}

	// ── Time ──────────────────────────────────────────────────────────

	float getDeltaTime()
	{
		return NativeScriptManager::Instance().getLastDeltaTime();
	}

	float getTotalTime()
	{
		return static_cast<float>(SDL_GetTicks()) / 1000.0f;
	}
}
