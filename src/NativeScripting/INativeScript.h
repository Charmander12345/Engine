#pragma once

#include <string>
#include "GameplayAPIExport.h"

namespace ECS { using Entity = unsigned int; }

/// Base class for all C++ gameplay scripts.
/// The game developer inherits from this and overrides the lifecycle methods.
class INativeScript
{
public:
	virtual ~INativeScript() = default;

	// ── Lifecycle events (overridden by the game developer) ──────────
	virtual void onLoaded()  {}
	virtual void tick(float deltaTime) { (void)deltaTime; }
	virtual void onBeginOverlap(ECS::Entity other) { (void)other; }
	virtual void onEndOverlap(ECS::Entity other)   { (void)other; }
	virtual void onDestroy() {}

	// ── Engine-set helpers ────────────────────────────────────────────
	ECS::Entity getEntity() const { return m_entity; }

	// ── Convenience API (delegates to GameplayAPI for own entity) ────

	// Transform
	GAMEPLAY_API bool getPosition(float outPos[3]) const;
	GAMEPLAY_API bool setPosition(const float pos[3]);
	GAMEPLAY_API bool getRotation(float outRot[3]) const;
	GAMEPLAY_API bool setRotation(const float rot[3]);
	GAMEPLAY_API bool getScale(float outScale[3]) const;
	GAMEPLAY_API bool setScale(const float scale[3]);
	GAMEPLAY_API bool getTransform(float outPos[3], float outRot[3], float outScale[3]) const;
	GAMEPLAY_API bool translate(const float delta[3]);
	GAMEPLAY_API bool rotate(const float delta[3]);

	// Physics
	GAMEPLAY_API void setVelocity(const float vel[3]);
	GAMEPLAY_API void getVelocity(float outVel[3]) const;
	GAMEPLAY_API void addForce(const float force[3]);
	GAMEPLAY_API void addImpulse(const float impulse[3]);
	GAMEPLAY_API void setAngularVelocity(const float vel[3]);
	GAMEPLAY_API void getAngularVelocity(float outVel[3]) const;

	// Light
	GAMEPLAY_API bool getLightColor(float outColor[3]) const;
	GAMEPLAY_API bool setLightColor(const float color[3]);

	// Particle
	GAMEPLAY_API bool setEmitterProperty(const char* key, float value);
	GAMEPLAY_API bool setEmitterEnabled(bool enabled);
	GAMEPLAY_API bool setEmitterColor(float r, float g, float b, float a);
	GAMEPLAY_API bool setEmitterEndColor(float r, float g, float b, float a);

	// Entity management
	GAMEPLAY_API static ECS::Entity findEntityByName(const char* name);
	GAMEPLAY_API static ECS::Entity createEntity();
	GAMEPLAY_API static bool removeEntity(ECS::Entity entity);

	// Logging
	GAMEPLAY_API static void logInfo(const char* msg);
	GAMEPLAY_API static void logWarning(const char* msg);
	GAMEPLAY_API static void logError(const char* msg);

	// Time
	GAMEPLAY_API static float getDeltaTime();
	GAMEPLAY_API static float getTotalTime();

private:
	friend class NativeScriptManager;
	ECS::Entity m_entity{ 0 };
};
