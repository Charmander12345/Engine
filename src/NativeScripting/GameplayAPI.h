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

	// ── Transform Parenting ──────────────────────────────────────────
	GAMEPLAY_API bool         setParent(ECS::Entity entity, ECS::Entity parent);
	GAMEPLAY_API bool         removeParent(ECS::Entity entity);
	GAMEPLAY_API ECS::Entity  getParent(ECS::Entity entity);
	GAMEPLAY_API int          getChildren(ECS::Entity entity, ECS::Entity* outChildren, int maxCount);
	GAMEPLAY_API int          getChildCount(ECS::Entity entity);
	GAMEPLAY_API ECS::Entity  getRoot(ECS::Entity entity);
	GAMEPLAY_API bool         isAncestorOf(ECS::Entity ancestor, ECS::Entity descendant);
	GAMEPLAY_API bool         getLocalPosition(ECS::Entity entity, float outPos[3]);
	GAMEPLAY_API bool         setLocalPosition(ECS::Entity entity, const float pos[3]);
	GAMEPLAY_API bool         getLocalRotation(ECS::Entity entity, float outRot[3]);
	GAMEPLAY_API bool         setLocalRotation(ECS::Entity entity, const float rot[3]);
	GAMEPLAY_API bool         getLocalScale(ECS::Entity entity, float outScale[3]);
	GAMEPLAY_API bool         setLocalScale(ECS::Entity entity, const float scale[3]);

	// ── Cross-script communication ────────────────────────────────────
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

	// ── Physics Queries (Overlap / Sweep) ─────────────────────────────
	GAMEPLAY_API std::vector<ECS::Entity> overlapSphere(const float center[3], float radius);
	GAMEPLAY_API std::vector<ECS::Entity> overlapBox(const float center[3], const float halfExtents[3],
													 const float eulerDeg[3] = nullptr);
	GAMEPLAY_API RaycastResult sweepSphere(const float origin[3], float radius,
										   const float direction[3], float maxDist = 1000.0f);
	GAMEPLAY_API RaycastResult sweepBox(const float origin[3], const float halfExtents[3],
										const float direction[3], float maxDist = 1000.0f);

	// ── Force / impulse at position ──────────────────────────────────
	GAMEPLAY_API void addForceAtPosition(ECS::Entity entity, const float force[3], const float position[3]);
	GAMEPLAY_API void addImpulseAtPosition(ECS::Entity entity, const float impulse[3], const float position[3]);

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
	GAMEPLAY_API bool setAudioPosition(unsigned int handle, float x, float y, float z);
	GAMEPLAY_API bool setAudioSpatial(unsigned int handle, bool is3D, float minDist = 1.0f, float maxDist = 50.0f, float rolloff = 1.0f);

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
	GAMEPLAY_API void log(const char* msg, int level = 0);

	// ── Time ──────────────────────────────────────────────────────────
	GAMEPLAY_API float getDeltaTime();
	GAMEPLAY_API float getTotalTime();

	// ── Actor System ─────────────────────────────────────────────────
	/// Get the active World pointer (set by the engine main loop).
	GAMEPLAY_API void* getWorld();
	/// Set the active World pointer (called by engine internals).
	GAMEPLAY_API void  setWorld(void* world);

	// ── Actor System – Extended ──────────────────────────────────────
	/// Spawn an actor by class name at the given position.
	GAMEPLAY_API void* spawnActorByClass(const char* className, float x = 0, float y = 0, float z = 0);

	/// Spawn a deferred actor by class name (beginPlay not called until finishSpawning).
	GAMEPLAY_API void* spawnActorDeferredByClass(const char* className, float x = 0, float y = 0, float z = 0);

	/// Finalize a deferred actor spawn (calls beginPlay).
	GAMEPLAY_API bool finishSpawning(void* actor);

	/// Destroy an actor (deferred to end of frame).
	GAMEPLAY_API bool destroyActor(void* actor);

	/// Get the GameMode actor for the active world (or nullptr).
	GAMEPLAY_API void* getGameMode();

	/// Get the GameState actor for the active world (or nullptr).
	GAMEPLAY_API void* getGameState();

	/// Add a tick prerequisite: 'actor' will tick after 'prerequisite'.
	GAMEPLAY_API bool addTickPrerequisite(void* actor, void* prerequisite);

	/// Remove a tick prerequisite.
	GAMEPLAY_API bool removeTickPrerequisite(void* actor, void* prerequisite);

	/// Set the tick group for an actor.
	GAMEPLAY_API bool setTickGroup(void* actor, int tickGroup);

	/// Set the tick interval for an actor (0 = every frame).
	GAMEPLAY_API bool setTickInterval(void* actor, float interval);

	/// Enable or disable ticking for an actor.
	GAMEPLAY_API bool setCanEverTick(void* actor, bool canTick);

	/// Get an actor's name.
	GAMEPLAY_API const char* getActorName(void* actor);

	/// Set an actor's name.
	GAMEPLAY_API bool setActorName(void* actor, const char* name);

	/// Get an actor's tag.
	GAMEPLAY_API const char* getActorTag(void* actor);

	/// Set an actor's tag.
	GAMEPLAY_API bool setActorTag(void* actor, const char* tag);

	/// Find an actor by name in the active world.
	GAMEPLAY_API void* findActorByName(const char* name);

	/// Find actors by tag in the active world.
	GAMEPLAY_API int findActorsByTag(const char* tag, void** outActors, int maxCount);

	/// Possess a Pawn with a PlayerController.
	GAMEPLAY_API bool possess(void* controller, void* pawn);

	/// Unpossess the current Pawn from a PlayerController.
	GAMEPLAY_API bool unpossessController(void* controller);

	// ── Skeletal Animation ───────────────────────────────────────────
	GAMEPLAY_API bool  isEntitySkinned(ECS::Entity entity);
	GAMEPLAY_API int   getAnimationClipCount(ECS::Entity entity);
	GAMEPLAY_API int   findAnimationClipByName(ECS::Entity entity, const char* name);
	GAMEPLAY_API bool  playSkeletalAnimation(ECS::Entity entity, int clipIndex, bool loop = true);
	GAMEPLAY_API bool  stopSkeletalAnimation(ECS::Entity entity);
	GAMEPLAY_API bool  setSkeletalAnimationSpeed(ECS::Entity entity, float speed);
	GAMEPLAY_API bool  crossfadeAnimation(ECS::Entity entity, int toClip, float duration, bool loop = true);
	GAMEPLAY_API bool  isCrossfading(ECS::Entity entity);
	GAMEPLAY_API bool  playAnimationOnLayer(ECS::Entity entity, int layer, int clipIndex, bool loop = true, float crossfadeDuration = 0.0f);
	GAMEPLAY_API bool  stopAnimationLayer(ECS::Entity entity, int layer);
	GAMEPLAY_API bool  setAnimationLayerWeight(ECS::Entity entity, int layer, float weight);
	GAMEPLAY_API int   getAnimationLayerCount(ECS::Entity entity);
	GAMEPLAY_API bool  setAnimationLayerCount(ECS::Entity entity, int count);
}
