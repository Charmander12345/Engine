#pragma once

/// Central include for game developer C++ scripts.
/// Include this in gameplay code to access all engine functionality.

#include "INativeScript.h"
#include "NativeScriptRegistry.h"
#include "GameplayAPIExport.h"

namespace GameplayAPI
{
	// ── ECS / Transform ───────────────────────────────────────────────
	GAMEPLAY_API bool getPosition(ECS::Entity entity, float outPos[3]);
	GAMEPLAY_API bool setPosition(ECS::Entity entity, const float pos[3]);
	GAMEPLAY_API bool getRotation(ECS::Entity entity, float outRot[3]);
	GAMEPLAY_API bool setRotation(ECS::Entity entity, const float rot[3]);
	GAMEPLAY_API bool getScale(ECS::Entity entity, float outScale[3]);
	GAMEPLAY_API bool setScale(ECS::Entity entity, const float scale[3]);

	// ── Entity management ─────────────────────────────────────────────
	GAMEPLAY_API ECS::Entity createEntity();
	GAMEPLAY_API bool removeEntity(ECS::Entity entity);
	GAMEPLAY_API ECS::Entity findEntityByName(const char* name);

	// ── Physics ───────────────────────────────────────────────────────
	GAMEPLAY_API void setVelocity(ECS::Entity entity, const float vel[3]);
	GAMEPLAY_API void getVelocity(ECS::Entity entity, float outVel[3]);
	GAMEPLAY_API void addForce(ECS::Entity entity, const float force[3]);
	GAMEPLAY_API void addImpulse(ECS::Entity entity, const float impulse[3]);

	// ── Logging ───────────────────────────────────────────────────────
	GAMEPLAY_API void logInfo(const char* msg);
	GAMEPLAY_API void logWarning(const char* msg);
	GAMEPLAY_API void logError(const char* msg);

	// ── Time ──────────────────────────────────────────────────────────
	GAMEPLAY_API float getDeltaTime();
	GAMEPLAY_API float getTotalTime();
}
