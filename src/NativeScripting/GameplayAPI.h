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
	GAMEPLAY_API bool getTransform(ECS::Entity entity, float outPos[3], float outRot[3], float outScale[3]);
	GAMEPLAY_API bool translate(ECS::Entity entity, const float delta[3]);
	GAMEPLAY_API bool rotate(ECS::Entity entity, const float delta[3]);

	// ── Entity management ─────────────────────────────────────────────
	GAMEPLAY_API ECS::Entity createEntity();
	GAMEPLAY_API bool removeEntity(ECS::Entity entity);
	GAMEPLAY_API ECS::Entity findEntityByName(const char* name);
	GAMEPLAY_API bool attachComponent(ECS::Entity entity, int componentKind);
	GAMEPLAY_API bool detachComponent(ECS::Entity entity, int componentKind);
	GAMEPLAY_API int  getEntities(const int* componentKinds, int kindCount, ECS::Entity* outEntities, int maxCount);

	// ── Entity queries ────────────────────────────────────────────────
	GAMEPLAY_API bool        isEntityValid(ECS::Entity entity);
	GAMEPLAY_API bool        hasComponent(ECS::Entity entity, int componentKind);
	GAMEPLAY_API const char* getEntityName(ECS::Entity entity);
	GAMEPLAY_API bool        setEntityName(ECS::Entity entity, const char* name);
	GAMEPLAY_API int         getEntityCount();
	GAMEPLAY_API int         getAllEntities(ECS::Entity* outEntities, int maxCount);
	GAMEPLAY_API float       distanceBetween(ECS::Entity a, ECS::Entity b);

	// ── Cross-script communication ────────────────────────────────────
	GAMEPLAY_API ScriptValue callScriptFunction(ECS::Entity entity, const char* funcName, const std::vector<ScriptValue>& args = {});
	GAMEPLAY_API ScriptValue callPythonFunctionOn(ECS::Entity entity, const char* funcName, const std::vector<ScriptValue>& args = {});

	/// Unified call: automatically routes to C++ (onScriptCall) or Python.
	/// Callers never need to know which language implements the function.
	GAMEPLAY_API ScriptValue callFunction(ECS::Entity entity, const char* funcName, const std::vector<ScriptValue>& args = {});

	// ── Mesh ──────────────────────────────────────────────────────────
	GAMEPLAY_API bool setMesh(ECS::Entity entity, const char* assetPath);
	GAMEPLAY_API bool getMesh(ECS::Entity entity, char* outPath, int maxLen);

	// ── Light ─────────────────────────────────────────────────────────
	GAMEPLAY_API bool getLightColor(ECS::Entity entity, float outColor[3]);
	GAMEPLAY_API bool setLightColor(ECS::Entity entity, const float color[3]);

	// ── Material overrides ────────────────────────────────────────────
	GAMEPLAY_API bool setMaterialOverrideColorTint(ECS::Entity entity, float r, float g, float b);
	GAMEPLAY_API bool getMaterialOverrideColorTint(ECS::Entity entity, float outColor[3]);
	GAMEPLAY_API bool setMaterialOverrideMetallic(ECS::Entity entity, float metallic);
	GAMEPLAY_API bool setMaterialOverrideRoughness(ECS::Entity entity, float roughness);
	GAMEPLAY_API bool setMaterialOverrideShininess(ECS::Entity entity, float shininess);
	GAMEPLAY_API bool clearMaterialOverrides(ECS::Entity entity);

	// ── Physics ───────────────────────────────────────────────────────
	GAMEPLAY_API void setVelocity(ECS::Entity entity, const float vel[3]);
	GAMEPLAY_API void getVelocity(ECS::Entity entity, float outVel[3]);
	GAMEPLAY_API void addForce(ECS::Entity entity, const float force[3]);
	GAMEPLAY_API void addImpulse(ECS::Entity entity, const float impulse[3]);
	GAMEPLAY_API void setAngularVelocity(ECS::Entity entity, const float vel[3]);
	GAMEPLAY_API void getAngularVelocity(ECS::Entity entity, float outVel[3]);
	GAMEPLAY_API void setGravity(const float gravity[3]);
	GAMEPLAY_API void getGravity(float outGravity[3]);
	GAMEPLAY_API bool isBodySleeping(ECS::Entity entity);

	struct GAMEPLAY_API RaycastResult
	{
		ECS::Entity entity{ 0 };
		float point[3]{};
		float normal[3]{};
		float distance{ 0.0f };
		bool  hit{ false };
	};
	GAMEPLAY_API RaycastResult raycast(const float origin[3], const float direction[3], float maxDist = 1000.0f);

	// ── Camera ────────────────────────────────────────────────────────
	GAMEPLAY_API void getCameraPosition(float outPos[3]);
	GAMEPLAY_API void setCameraPosition(const float pos[3]);
	GAMEPLAY_API void getCameraRotation(float& outYaw, float& outPitch);
	GAMEPLAY_API void setCameraRotation(float yaw, float pitch);
	GAMEPLAY_API void cameraTransitionTo(const float pos[3], float yaw, float pitch, float durationSec);
	GAMEPLAY_API bool isCameraTransitioning();
	GAMEPLAY_API void cancelCameraTransition();
	GAMEPLAY_API void startCameraPath(const float* positions, const float* yaws, const float* pitches, int count, float duration, bool loop);
	GAMEPLAY_API bool isCameraPathPlaying();
	GAMEPLAY_API void pauseCameraPath();
	GAMEPLAY_API void resumeCameraPath();
	GAMEPLAY_API void stopCameraPath();
	GAMEPLAY_API float getCameraPathProgress();

	// ── Audio ─────────────────────────────────────────────────────────
	GAMEPLAY_API unsigned int createAudio(const char* contentPath, bool loop = false, float gain = 1.0f);
	GAMEPLAY_API unsigned int createAudioFromAsset(unsigned int assetId, bool loop = false, float gain = 1.0f);
	GAMEPLAY_API unsigned int playAudio(const char* contentPath, bool loop = false, float gain = 1.0f);
	GAMEPLAY_API bool playAudioHandle(unsigned int handle);
	GAMEPLAY_API bool setAudioVolume(unsigned int handle, float gain);
	GAMEPLAY_API float getAudioVolume(unsigned int handle);
	GAMEPLAY_API bool pauseAudio(unsigned int handle);
	GAMEPLAY_API bool isAudioPlaying(unsigned int handle);
	GAMEPLAY_API bool isAudioPlayingPath(const char* contentPath);
	GAMEPLAY_API bool stopAudio(unsigned int handle);
	GAMEPLAY_API bool invalidateAudioHandle(unsigned int handle);

	// ── Input ─────────────────────────────────────────────────────────
	GAMEPLAY_API bool isKeyPressed(int keycode);
	GAMEPLAY_API bool isShiftPressed();
	GAMEPLAY_API bool isCtrlPressed();
	GAMEPLAY_API bool isAltPressed();
	GAMEPLAY_API int  getKey(const char* name);

	// ── Input Actions ────────────────────────────────────────────────
	/// Register a C++ callback for when the named input action fires.
	/// Returns a callback ID that can be used to unregister.
	GAMEPLAY_API int  registerInputActionCallback(const char* actionName, void(*callback)());
	/// Unregister a previously registered input action callback.
	GAMEPLAY_API void unregisterInputActionCallback(int callbackId);

	// ── UI ────────────────────────────────────────────────────────────
	GAMEPLAY_API void showToastMessage(const char* message, float duration = 2.5f);
	GAMEPLAY_API void showModalMessage(const char* message);
	GAMEPLAY_API void closeModalMessage();
	GAMEPLAY_API bool showCursor(bool visible);
	GAMEPLAY_API void clearAllWidgets();
	GAMEPLAY_API bool spawnWidget(const char* contentPath, char* outWidgetId, int maxLen);
	GAMEPLAY_API bool removeWidget(const char* widgetId);
	GAMEPLAY_API bool playAnimation(const char* widgetId, const char* animationName, bool fromStart = true);
	GAMEPLAY_API bool stopAnimation(const char* widgetId, const char* animationName);
	GAMEPLAY_API bool setAnimationSpeed(const char* widgetId, const char* animationName, float speed);
	GAMEPLAY_API bool setFocus(const char* elementId);
	GAMEPLAY_API bool clearFocus();
	GAMEPLAY_API bool setFocusable(const char* elementId, bool focusable);
	GAMEPLAY_API bool setDraggable(const char* elementId, bool enabled, const char* payload = "");
	GAMEPLAY_API bool setDropTarget(const char* elementId, bool enabled);

	// ── Particle ──────────────────────────────────────────────────────
	GAMEPLAY_API bool setEmitterProperty(ECS::Entity entity, const char* key, float value);
	GAMEPLAY_API bool setEmitterEnabled(ECS::Entity entity, bool enabled);
	GAMEPLAY_API bool setEmitterColor(ECS::Entity entity, float r, float g, float b, float a);
	GAMEPLAY_API bool setEmitterEndColor(ECS::Entity entity, float r, float g, float b, float a);

	// ── Diagnostics ───────────────────────────────────────────────────
	GAMEPLAY_API bool        setState(const char* key, const char* value);
	GAMEPLAY_API const char* getState(const char* key);

	// ── Global state ──────────────────────────────────────────────────
	GAMEPLAY_API bool        setGlobalNumber(const char* name, double value);
	GAMEPLAY_API bool        setGlobalString(const char* name, const char* value);
	GAMEPLAY_API bool        setGlobalBool(const char* name, bool value);
	GAMEPLAY_API double      getGlobalNumber(const char* name);
	GAMEPLAY_API const char* getGlobalString(const char* name);
	GAMEPLAY_API bool        getGlobalBool(const char* name);
	GAMEPLAY_API bool        removeGlobal(const char* name);
	GAMEPLAY_API void        clearGlobals();

	// ── Logging ───────────────────────────────────────────────────────
	GAMEPLAY_API void logInfo(const char* msg);
	GAMEPLAY_API void logWarning(const char* msg);
	GAMEPLAY_API void logError(const char* msg);
	GAMEPLAY_API void log(const char* msg, int level = 0);

	// ── Time ──────────────────────────────────────────────────────────
	GAMEPLAY_API float getDeltaTime();
	GAMEPLAY_API float getTotalTime();
}
