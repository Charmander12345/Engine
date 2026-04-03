#include "GameplayAPI.h"
#include "NativeScriptManager.h"

#include "../Core/ECS/ECS.h"
#include "../Logger/Logger.h"
#include "../Physics/PhysicsWorld.h"
#include "../Core/AudioManager.h"
#include "../AssetManager/AssetManager.h"
#include "../AssetManager/AssetTypes.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/UIManager.h"
#include "../Renderer/ViewportUIManager.h"
#include "../Renderer/UIWidget.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Scripting/ScriptingGlobalState.h"
#include "../Core/InputActionManager.h"

#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>

// ── Internal renderer pointer (set by engine, not exported) ──────────
static Renderer* s_renderer = nullptr;
// Thread-local buffer for getState string return
static thread_local std::string s_stateBuffer;
static thread_local std::string s_globalStringBuffer;

namespace GameplayAPI
{
	// ── Internal: called by engine to provide renderer access ─────────
	void setRendererInternal(Renderer* renderer)
	{
		s_renderer = renderer;
	}

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

	bool getTransform(ECS::Entity entity, float outPos[3], float outRot[3], float outScale[3])
	{
		const auto* tc = ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(entity);
		if (!tc) return false;
		outPos[0] = tc->position[0]; outPos[1] = tc->position[1]; outPos[2] = tc->position[2];
		outRot[0] = tc->rotation[0]; outRot[1] = tc->rotation[1]; outRot[2] = tc->rotation[2];
		outScale[0] = tc->scale[0];  outScale[1] = tc->scale[1];  outScale[2] = tc->scale[2];
		return true;
	}

	bool translate(ECS::Entity entity, const float delta[3])
	{
		auto* tc = ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(entity);
		if (!tc) return false;
		tc->position[0] += delta[0];
		tc->position[1] += delta[1];
		tc->position[2] += delta[2];
		return true;
	}

	bool rotate(ECS::Entity entity, const float delta[3])
	{
		auto* tc = ECS::ECSManager::Instance().getComponent<ECS::TransformComponent>(entity);
		if (!tc) return false;
		tc->rotation[0] += delta[0];
		tc->rotation[1] += delta[1];
		tc->rotation[2] += delta[2];
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

	static bool AddComponentByKind(ECS::Entity entity, int kind)
	{
		switch (static_cast<ECS::ComponentKind>(kind))
		{
		case ECS::ComponentKind::Transform:      return ECS::addComponent<ECS::TransformComponent>(entity);
		case ECS::ComponentKind::Mesh:            return ECS::addComponent<ECS::MeshComponent>(entity);
		case ECS::ComponentKind::Material:        return ECS::addComponent<ECS::MaterialComponent>(entity);
		case ECS::ComponentKind::Light:           return ECS::addComponent<ECS::LightComponent>(entity);
		case ECS::ComponentKind::Camera:          return ECS::addComponent<ECS::CameraComponent>(entity);
		case ECS::ComponentKind::Physics:         return ECS::addComponent<ECS::PhysicsComponent>(entity);
		case ECS::ComponentKind::Collision:       return ECS::addComponent<ECS::CollisionComponent>(entity);
		case ECS::ComponentKind::Logic:           return ECS::addComponent<ECS::LogicComponent>(entity);
		case ECS::ComponentKind::Name:            return ECS::addComponent<ECS::NameComponent>(entity);
		case ECS::ComponentKind::ParticleEmitter: return ECS::addComponent<ECS::ParticleEmitterComponent>(entity);
		default: return false;
		}
	}

	static bool RemoveComponentByKind(ECS::Entity entity, int kind)
	{
		switch (static_cast<ECS::ComponentKind>(kind))
		{
		case ECS::ComponentKind::Transform:      return ECS::removeComponent<ECS::TransformComponent>(entity);
		case ECS::ComponentKind::Mesh:            return ECS::removeComponent<ECS::MeshComponent>(entity);
		case ECS::ComponentKind::Material:        return ECS::removeComponent<ECS::MaterialComponent>(entity);
		case ECS::ComponentKind::Light:           return ECS::removeComponent<ECS::LightComponent>(entity);
		case ECS::ComponentKind::Camera:          return ECS::removeComponent<ECS::CameraComponent>(entity);
		case ECS::ComponentKind::Physics:         return ECS::removeComponent<ECS::PhysicsComponent>(entity);
		case ECS::ComponentKind::Collision:       return ECS::removeComponent<ECS::CollisionComponent>(entity);
		case ECS::ComponentKind::Logic:           return ECS::removeComponent<ECS::LogicComponent>(entity);
		case ECS::ComponentKind::Name:            return ECS::removeComponent<ECS::NameComponent>(entity);
		case ECS::ComponentKind::ParticleEmitter: return ECS::removeComponent<ECS::ParticleEmitterComponent>(entity);
		default: return false;
		}
	}

	static bool RequireComponentByKind(ECS::Schema& schema, int kind)
	{
		switch (static_cast<ECS::ComponentKind>(kind))
		{
		case ECS::ComponentKind::Transform:      schema.require<ECS::TransformComponent>(); return true;
		case ECS::ComponentKind::Mesh:            schema.require<ECS::MeshComponent>(); return true;
		case ECS::ComponentKind::Material:        schema.require<ECS::MaterialComponent>(); return true;
		case ECS::ComponentKind::Light:           schema.require<ECS::LightComponent>(); return true;
		case ECS::ComponentKind::Camera:          schema.require<ECS::CameraComponent>(); return true;
		case ECS::ComponentKind::Physics:         schema.require<ECS::PhysicsComponent>(); return true;
		case ECS::ComponentKind::Collision:       schema.require<ECS::CollisionComponent>(); return true;
		case ECS::ComponentKind::Logic:           schema.require<ECS::LogicComponent>(); return true;
		case ECS::ComponentKind::Name:            schema.require<ECS::NameComponent>(); return true;
		case ECS::ComponentKind::ParticleEmitter: schema.require<ECS::ParticleEmitterComponent>(); return true;
		default: return false;
		}
	}

	bool attachComponent(ECS::Entity entity, int componentKind)
	{
		return AddComponentByKind(entity, componentKind);
	}

	bool detachComponent(ECS::Entity entity, int componentKind)
	{
		return RemoveComponentByKind(entity, componentKind);
	}

	int getEntities(const int* componentKinds, int kindCount, ECS::Entity* outEntities, int maxCount)
	{
		ECS::Schema schema;
		for (int i = 0; i < kindCount; ++i)
		{
			if (!RequireComponentByKind(schema, componentKinds[i]))
				return 0;
		}
		auto entities = ECS::ECSManager::Instance().getEntitiesMatchingSchema(schema);
		int count = static_cast<int>(entities.size());
		if (outEntities && maxCount > 0)
		{
			int toCopy = (std::min)(count, maxCount);
			for (int i = 0; i < toCopy; ++i)
				outEntities[i] = entities[i];
		}
		return count;
	}

	// ── Mesh ──────────────────────────────────────────────────────────

	bool setMesh(ECS::Entity entity, const char* assetPath)
	{
		if (!assetPath) return false;
		if (!ECS::hasComponent<ECS::MeshComponent>(entity))
		{
			if (!ECS::addComponent<ECS::MeshComponent>(entity))
				return false;
		}
		int assetId = AssetManager::Instance().loadAsset(assetPath, AssetType::Model3D, AssetManager::Sync);
		if (assetId == 0)
			assetId = AssetManager::Instance().loadAsset(assetPath, AssetType::Model2D, AssetManager::Sync);
		if (assetId == 0) return false;

		ECS::MeshComponent comp{};
		comp.meshAssetPath = assetPath;
		comp.meshAssetId = static_cast<unsigned int>(assetId);
		return ECS::setComponent<ECS::MeshComponent>(entity, comp);
	}

	bool getMesh(ECS::Entity entity, char* outPath, int maxLen)
	{
		const auto* mc = ECS::ECSManager::Instance().getComponent<ECS::MeshComponent>(entity);
		if (!mc || mc->meshAssetPath.empty()) return false;
		if (outPath && maxLen > 0)
		{
			std::strncpy(outPath, mc->meshAssetPath.c_str(), static_cast<size_t>(maxLen) - 1);
			outPath[maxLen - 1] = '\0';
		}
		return true;
	}

	// ── Light ─────────────────────────────────────────────────────────

	bool getLightColor(ECS::Entity entity, float outColor[3])
	{
		const auto* lc = ECS::ECSManager::Instance().getComponent<ECS::LightComponent>(entity);
		if (!lc) return false;
		outColor[0] = lc->color[0];
		outColor[1] = lc->color[1];
		outColor[2] = lc->color[2];
		return true;
	}

	bool setLightColor(ECS::Entity entity, const float color[3])
	{
		auto* lc = ECS::ECSManager::Instance().getComponent<ECS::LightComponent>(entity);
		if (!lc) return false;
		lc->color[0] = color[0];
		lc->color[1] = color[1];
		lc->color[2] = color[2];
		return true;
	}

	// ── Material overrides ────────────────────────────────────────────

	bool setMaterialOverrideColorTint(ECS::Entity entity, float r, float g, float b)
	{
		auto* mc = ECS::ECSManager::Instance().getComponent<ECS::MaterialComponent>(entity);
		if (!mc) return false;
		mc->overrides.colorTint[0] = r;
		mc->overrides.colorTint[1] = g;
		mc->overrides.colorTint[2] = b;
		mc->overrides.hasColorTint = true;
		return true;
	}

	bool getMaterialOverrideColorTint(ECS::Entity entity, float outColor[3])
	{
		const auto* mc = ECS::ECSManager::Instance().getComponent<ECS::MaterialComponent>(entity);
		if (!mc || !mc->overrides.hasColorTint) return false;
		outColor[0] = mc->overrides.colorTint[0];
		outColor[1] = mc->overrides.colorTint[1];
		outColor[2] = mc->overrides.colorTint[2];
		return true;
	}

	bool setMaterialOverrideMetallic(ECS::Entity entity, float metallic)
	{
		auto* mc = ECS::ECSManager::Instance().getComponent<ECS::MaterialComponent>(entity);
		if (!mc) return false;
		mc->overrides.metallic = metallic;
		mc->overrides.hasMetallic = true;
		return true;
	}

	bool setMaterialOverrideRoughness(ECS::Entity entity, float roughness)
	{
		auto* mc = ECS::ECSManager::Instance().getComponent<ECS::MaterialComponent>(entity);
		if (!mc) return false;
		mc->overrides.roughness = roughness;
		mc->overrides.hasRoughness = true;
		return true;
	}

	bool setMaterialOverrideShininess(ECS::Entity entity, float shininess)
	{
		auto* mc = ECS::ECSManager::Instance().getComponent<ECS::MaterialComponent>(entity);
		if (!mc) return false;
		mc->overrides.shininess = shininess;
		mc->overrides.hasShininess = true;
		return true;
	}

	bool clearMaterialOverrides(ECS::Entity entity)
	{
		auto* mc = ECS::ECSManager::Instance().getComponent<ECS::MaterialComponent>(entity);
		if (!mc) return false;
		mc->overrides = ECS::MaterialOverrides{};
		return true;
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

	void setAngularVelocity(ECS::Entity entity, const float vel[3])
	{
		auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(entity);
		if (!pc) return;
		pc->angularVelocity[0] = vel[0];
		pc->angularVelocity[1] = vel[1];
		pc->angularVelocity[2] = vel[2];
	}

	void getAngularVelocity(ECS::Entity entity, float outVel[3])
	{
		const auto* pc = ECS::ECSManager::Instance().getComponent<ECS::PhysicsComponent>(entity);
		if (!pc)
		{
			outVel[0] = outVel[1] = outVel[2] = 0.0f;
			return;
		}
		outVel[0] = pc->angularVelocity[0];
		outVel[1] = pc->angularVelocity[1];
		outVel[2] = pc->angularVelocity[2];
	}

	void setGravity(const float gravity[3])
	{
		PhysicsWorld::Instance().setGravity(gravity[0], gravity[1], gravity[2]);
	}

	void getGravity(float outGravity[3])
	{
		PhysicsWorld::Instance().getGravity(outGravity[0], outGravity[1], outGravity[2]);
	}

	bool isBodySleeping(ECS::Entity entity)
	{
		return PhysicsWorld::Instance().isBodySleeping(entity);
	}

	RaycastResult raycast(const float origin[3], const float direction[3], float maxDist)
	{
		auto hit = PhysicsWorld::Instance().raycast(
			origin[0], origin[1], origin[2],
			direction[0], direction[1], direction[2],
			maxDist);

		RaycastResult result{};
		result.hit = hit.hit;
		if (hit.hit)
		{
			result.entity = hit.entity;
			result.point[0] = hit.point[0]; result.point[1] = hit.point[1]; result.point[2] = hit.point[2];
			result.normal[0] = hit.normal[0]; result.normal[1] = hit.normal[1]; result.normal[2] = hit.normal[2];
			result.distance = hit.distance;
		}
		return result;
	}

	// ── Camera ────────────────────────────────────────────────────────

	void getCameraPosition(float outPos[3])
	{
		if (!s_renderer) { outPos[0] = outPos[1] = outPos[2] = 0.0f; return; }
		Vec3 p = s_renderer->getCameraPosition();
		outPos[0] = p.x; outPos[1] = p.y; outPos[2] = p.z;
	}

	void setCameraPosition(const float pos[3])
	{
		if (!s_renderer) return;
		s_renderer->setCameraPosition({ pos[0], pos[1], pos[2] });
	}

	void getCameraRotation(float& outYaw, float& outPitch)
	{
		if (!s_renderer) { outYaw = outPitch = 0.0f; return; }
		Vec2 r = s_renderer->getCameraRotationDegrees();
		outYaw = r.x; outPitch = r.y;
	}

	void setCameraRotation(float yaw, float pitch)
	{
		if (!s_renderer) return;
		s_renderer->setCameraRotationDegrees(yaw, pitch);
	}

	void cameraTransitionTo(const float pos[3], float yaw, float pitch, float durationSec)
	{
		if (!s_renderer) return;
		s_renderer->startCameraTransition({ pos[0], pos[1], pos[2] }, yaw, pitch, durationSec);
	}

	bool isCameraTransitioning()
	{
		return s_renderer ? s_renderer->isCameraTransitioning() : false;
	}

	void cancelCameraTransition()
	{
		if (s_renderer) s_renderer->cancelCameraTransition();
	}

	void startCameraPath(const float* positions, const float* yaws, const float* pitches, int count, float duration, bool loop)
	{
		if (!s_renderer || count <= 0) return;
		std::vector<CameraPathPoint> points(static_cast<size_t>(count));
		for (int i = 0; i < count; ++i)
		{
			points[i].position = { positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2] };
			points[i].yaw = yaws[i];
			points[i].pitch = pitches[i];
		}
		s_renderer->startCameraPath(points, duration, loop);
	}

	bool isCameraPathPlaying()
	{
		return s_renderer ? s_renderer->isCameraPathPlaying() : false;
	}

	void pauseCameraPath()
	{
		if (s_renderer) s_renderer->pauseCameraPath();
	}

	void resumeCameraPath()
	{
		if (s_renderer) s_renderer->resumeCameraPath();
	}

	void stopCameraPath()
	{
		if (s_renderer) s_renderer->stopCameraPath();
	}

	float getCameraPathProgress()
	{
		return s_renderer ? s_renderer->getCameraPathProgress() : 0.0f;
	}

	// ── Audio ─────────────────────────────────────────────────────────

	unsigned int createAudio(const char* contentPath, bool loop, float gain)
	{
		if (!contentPath) return 0;
		const std::string absPath = AssetManager::Instance().getAbsoluteContentPath(contentPath);
		if (absPath.empty()) return 0;
		return AudioManager::Instance().createAudioPathAsync(absPath, loop, gain);
	}

	unsigned int createAudioFromAsset(unsigned int assetId, bool loop, float gain)
	{
		if (assetId == 0) return 0;
		return AudioManager::Instance().createAudioHandle(assetId, loop, gain);
	}

	unsigned int playAudio(const char* contentPath, bool loop, float gain)
	{
		if (!contentPath) return 0;
		const std::string absPath = AssetManager::Instance().getAbsoluteContentPath(contentPath);
		if (absPath.empty()) return 0;
		return AudioManager::Instance().playAudioPathAsync(absPath, loop, gain);
	}

	bool playAudioHandle(unsigned int handle)
	{
		return AudioManager::Instance().playHandle(handle);
	}

	bool setAudioVolume(unsigned int handle, float gain)
	{
		return AudioManager::Instance().setHandleGain(handle, gain);
	}

	float getAudioVolume(unsigned int handle)
	{
		auto val = AudioManager::Instance().getHandleGain(handle);
		return val.has_value() ? val.value() : 0.0f;
	}

	bool pauseAudio(unsigned int handle)
	{
		return AudioManager::Instance().pauseSource(handle);
	}

	bool isAudioPlaying(unsigned int handle)
	{
		return AudioManager::Instance().isSourcePlaying(handle);
	}

	bool isAudioPlayingPath(const char* contentPath)
	{
		if (!contentPath) return false;
		return AssetManager::Instance().isAudioPlayingContentPath(contentPath);
	}

	bool stopAudio(unsigned int handle)
	{
		return AudioManager::Instance().stopSource(handle);
	}

	bool invalidateAudioHandle(unsigned int handle)
	{
		return AudioManager::Instance().invalidateHandle(handle);
	}

	// ── Input ─────────────────────────────────────────────────────────

	bool isKeyPressed(int keycode)
	{
		const SDL_Scancode scancode = SDL_GetScancodeFromKey(static_cast<SDL_Keycode>(keycode), nullptr);
		int numkeys = 0;
		const bool* state = SDL_GetKeyboardState(&numkeys);
		if (!state || static_cast<int>(scancode) < 0 || static_cast<int>(scancode) >= numkeys)
			return false;
		return state[scancode];
	}

	bool isShiftPressed()
	{
		return (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
	}

	bool isCtrlPressed()
	{
		return (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
	}

	bool isAltPressed()
	{
		return (SDL_GetModState() & SDL_KMOD_ALT) != 0;
	}

	int getKey(const char* name)
	{
		if (!name) return 0;
		return static_cast<int>(SDL_GetKeyFromName(name));
	}

	// ── Input Actions ────────────────────────────────────────────────

	int registerInputActionCallback(const char* actionName, void(*callback)())
	{
		if (!actionName || !callback) return 0;
		return InputActionManager::Instance().registerCallback(
			actionName, [callback]() { callback(); });
	}

	void unregisterInputActionCallback(int callbackId)
	{
		InputActionManager::Instance().unregisterCallback(callbackId);
	}

	// ── UI ────────────────────────────────────────────────────────────

	void showToastMessage(const char* message, float duration)
	{
		if (auto* uiManager = UIManager::GetActiveInstance())
			uiManager->showToastMessage(message ? message : "", duration);
	}

	void showModalMessage(const char* message)
	{
		if (auto* uiManager = UIManager::GetActiveInstance())
			uiManager->showModalMessage(message ? message : "", nullptr);
	}

	void closeModalMessage()
	{
		if (auto* uiManager = UIManager::GetActiveInstance())
			uiManager->closeModalMessage();
	}

	bool showCursor(bool visible)
	{
		if (!s_renderer) return false;
		auto* vp = s_renderer->getViewportUIManagerPtr();
		if (!vp) return false;
		vp->setGameplayCursorVisible(visible);
		if (visible)
		{
			SDL_ShowCursor();
			if (auto* w = s_renderer->window())
			{
				SDL_SetWindowRelativeMouseMode(w, false);
				SDL_SetWindowMouseGrab(w, false);
			}
		}
		else
		{
			SDL_HideCursor();
			if (auto* w = s_renderer->window())
			{
				SDL_SetWindowRelativeMouseMode(w, true);
				SDL_SetWindowMouseGrab(w, true);
			}
		}
		return true;
	}

	void clearAllWidgets()
	{
		if (!s_renderer) return;
		auto* vp = s_renderer->getViewportUIManagerPtr();
		if (vp) vp->clearAllWidgets();
	}

	bool spawnWidget(const char* contentPath, char* outWidgetId, int maxLen)
	{
		if (!s_renderer || !contentPath || !contentPath[0]) return false;
		auto* vpUI = s_renderer->getViewportUIManagerPtr();
		if (!vpUI) return false;

		auto& assetManager = AssetManager::Instance();
		std::string resolvedPath(contentPath);
		if (resolvedPath.size() < 6 || resolvedPath.substr(resolvedPath.size() - 6) != ".asset")
			resolvedPath += ".asset";
		const std::string absolutePath = assetManager.getAbsoluteContentPath(resolvedPath);
		if (absolutePath.empty()) return false;

		const int assetId = assetManager.loadAsset(absolutePath, AssetType::Widget, AssetManager::Sync);
		if (assetId == 0) return false;

		auto asset = assetManager.getLoadedAssetByID(static_cast<unsigned int>(assetId));
		if (!asset) return false;

		auto widget = s_renderer->createWidgetFromAsset(asset);
		if (!widget) return false;

		static int s_spawnCounter = 0;
		const std::string widgetName = "_spawned_" + std::to_string(++s_spawnCounter);
		widget->setName(widgetName);
		if (!vpUI->createWidget(widgetName, 0)) return false;

		if (auto* destWidget = vpUI->getWidget(widgetName))
		{
			destWidget->setElements(widget->getElements());
			vpUI->markLayoutDirty();
		}

		if (outWidgetId && maxLen > 0)
		{
			std::strncpy(outWidgetId, widgetName.c_str(), static_cast<size_t>(maxLen) - 1);
			outWidgetId[maxLen - 1] = '\0';
		}
		return true;
	}

	bool removeWidget(const char* widgetId)
	{
		if (!s_renderer || !widgetId || !widgetId[0]) return false;
		auto* vpUI = s_renderer->getViewportUIManagerPtr();
		if (!vpUI) return false;
		return vpUI->removeWidget(widgetId);
	}

	bool playAnimation(const char* widgetId, const char* animationName, bool fromStart)
	{
		if (!s_renderer || !widgetId || !animationName) return false;
		auto* vp = s_renderer->getViewportUIManagerPtr();
		if (!vp) return false;
		Widget* widget = vp->getWidget(widgetId);
		if (!widget) return false;
		widget->animationPlayer().play(animationName, fromStart);
		return true;
	}

	bool stopAnimation(const char* widgetId, const char* animationName)
	{
		if (!s_renderer || !widgetId || !animationName) return false;
		auto* vp = s_renderer->getViewportUIManagerPtr();
		if (!vp) return false;
		Widget* widget = vp->getWidget(widgetId);
		if (!widget) return false;
		auto& player = widget->animationPlayer();
		if (!player.getCurrentAnimation().empty() && player.getCurrentAnimation() != animationName)
			return false;
		player.stop();
		return true;
	}

	bool setAnimationSpeed(const char* widgetId, const char* animationName, float speed)
	{
		if (!s_renderer || !widgetId || !animationName) return false;
		auto* vp = s_renderer->getViewportUIManagerPtr();
		if (!vp) return false;
		Widget* widget = vp->getWidget(widgetId);
		if (!widget) return false;
		for (auto& animation : widget->getAnimationsMutable())
		{
			if (animation.name == animationName)
			{
				animation.playbackSpeed = (std::max)(0.0f, speed);
				return true;
			}
		}
		return false;
	}

	bool setFocus(const char* elementId)
	{
		if (!s_renderer || !elementId) return false;
		auto* vp = s_renderer->getViewportUIManagerPtr();
		if (!vp) return false;
		vp->setFocus(elementId);
		return true;
	}

	bool clearFocus()
	{
		if (!s_renderer) return false;
		auto* vp = s_renderer->getViewportUIManagerPtr();
		if (!vp) return false;
		vp->clearFocus();
		return true;
	}

	bool setFocusable(const char* elementId, bool focusable)
	{
		if (!s_renderer || !elementId) return false;
		auto* vp = s_renderer->getViewportUIManagerPtr();
		if (!vp) return false;
		vp->setFocusable(elementId, focusable);
		return true;
	}

	bool setDraggable(const char* elementId, bool enabled, const char* payload)
	{
		if (!s_renderer || !elementId) return false;
		auto* vp = s_renderer->getViewportUIManagerPtr();
		if (!vp) return false;
		auto* elem = vp->findElementById(elementId);
		if (!elem) return false;
		elem->isDraggable = enabled;
		elem->dragPayload = payload ? payload : "";
		return true;
	}

	bool setDropTarget(const char* elementId, bool enabled)
	{
		if (!s_renderer || !elementId) return false;
		auto* vp = s_renderer->getViewportUIManagerPtr();
		if (!vp) return false;
		auto* elem = vp->findElementById(elementId);
		if (!elem) return false;
		elem->acceptsDrop = enabled;
		return true;
	}

	// ── Particle ──────────────────────────────────────────────────────

	bool setEmitterProperty(ECS::Entity entity, const char* key, float value)
	{
		if (!key) return false;
		auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
		if (!c) return false;
		std::string k(key);
		if      (k == "emissionRate")  c->emissionRate  = value;
		else if (k == "lifetime")      c->lifetime      = value;
		else if (k == "speed")         c->speed         = value;
		else if (k == "speedVariance") c->speedVariance = value;
		else if (k == "size")          c->size          = value;
		else if (k == "sizeEnd")       c->sizeEnd       = value;
		else if (k == "gravity")       c->gravity       = value;
		else if (k == "coneAngle")     c->coneAngle     = value;
		else if (k == "maxParticles")  c->maxParticles  = static_cast<int>(value);
		else if (k == "colorR")        c->colorR        = value;
		else if (k == "colorG")        c->colorG        = value;
		else if (k == "colorB")        c->colorB        = value;
		else if (k == "colorA")        c->colorA        = value;
		else if (k == "colorEndR")     c->colorEndR     = value;
		else if (k == "colorEndG")     c->colorEndG     = value;
		else if (k == "colorEndB")     c->colorEndB     = value;
		else if (k == "colorEndA")     c->colorEndA     = value;
		else return false;
		return true;
	}

	bool setEmitterEnabled(ECS::Entity entity, bool enabled)
	{
		auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
		if (!c) return false;
		c->enabled = enabled;
		return true;
	}

	bool setEmitterColor(ECS::Entity entity, float r, float g, float b, float a)
	{
		auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
		if (!c) return false;
		c->colorR = r; c->colorG = g; c->colorB = b; c->colorA = a;
		return true;
	}

	bool setEmitterEndColor(ECS::Entity entity, float r, float g, float b, float a)
	{
		auto* c = ECS::ECSManager::Instance().getComponent<ECS::ParticleEmitterComponent>(entity);
		if (!c) return false;
		c->colorEndR = r; c->colorEndG = g; c->colorEndB = b; c->colorEndA = a;
		return true;
	}

	// ── Diagnostics ───────────────────────────────────────────────────

	bool setState(const char* key, const char* value)
	{
		if (!key || !value) return false;
		DiagnosticsManager::Instance().setState(key, value);
		return true;
	}

	const char* getState(const char* key)
	{
		if (!key) return "";
		auto val = DiagnosticsManager::Instance().getState(key);
		if (!val.has_value()) return "";
		s_stateBuffer = val.value();
		return s_stateBuffer.c_str();
	}

	// ── Global state ──────────────────────────────────────────────────

	bool setGlobalNumber(const char* name, double value)
	{
		if (!name) return false;
		GlobalVariable var{};
		var.type = EngineVariableType::Number;
		var.number = value;
		ScriptingGlobalState::Instance().setVariable(name, var);
		return true;
	}

	bool setGlobalString(const char* name, const char* value)
	{
		if (!name || !value) return false;
		GlobalVariable* prev = ScriptingGlobalState::Instance().getVariable(name);
		if (prev && prev->type == EngineVariableType::String && prev->string)
			delete[] prev->string;

		size_t len = std::strlen(value);
		char* copy = new char[len + 1];
		std::memcpy(copy, value, len + 1);

		GlobalVariable var{};
		var.type = EngineVariableType::String;
		var.string = copy;
		ScriptingGlobalState::Instance().setVariable(name, var);
		return true;
	}

	bool setGlobalBool(const char* name, bool value)
	{
		if (!name) return false;
		GlobalVariable var{};
		var.type = EngineVariableType::Boolean;
		var.boolean = value;
		ScriptingGlobalState::Instance().setVariable(name, var);
		return true;
	}

	double getGlobalNumber(const char* name)
	{
		if (!name) return 0.0;
		GlobalVariable* var = ScriptingGlobalState::Instance().getVariable(name);
		if (!var || var->type != EngineVariableType::Number) return 0.0;
		return var->number;
	}

	const char* getGlobalString(const char* name)
	{
		if (!name) return "";
		GlobalVariable* var = ScriptingGlobalState::Instance().getVariable(name);
		if (!var || var->type != EngineVariableType::String || !var->string) return "";
		s_globalStringBuffer = var->string;
		return s_globalStringBuffer.c_str();
	}

	bool getGlobalBool(const char* name)
	{
		if (!name) return false;
		GlobalVariable* var = ScriptingGlobalState::Instance().getVariable(name);
		if (!var || var->type != EngineVariableType::Boolean) return false;
		return var->boolean;
	}

	bool removeGlobal(const char* name)
	{
		if (!name) return false;
		GlobalVariable* var = ScriptingGlobalState::Instance().getVariable(name);
		if (var && var->type == EngineVariableType::String && var->string)
			delete[] var->string;
		return ScriptingGlobalState::Instance().removeVariable(name);
	}

	void clearGlobals()
	{
		for (const auto& [key, var] : ScriptingGlobalState::Instance().getAllVariables())
		{
			if (var.type == EngineVariableType::String && var.string)
				delete[] var.string;
		}
		ScriptingGlobalState::Instance().clear();
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

	void log(const char* msg, int level)
	{
		Logger::LogLevel logLevel = Logger::LogLevel::INFO;
		if (level == 1) logLevel = Logger::LogLevel::WARNING;
		else if (level == 2) logLevel = Logger::LogLevel::ERROR;
		Logger::Instance().log(Logger::Category::Scripting, msg ? msg : "", logLevel);
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

// ── INativeScript convenience method implementations ─────────────────
// These delegate to the GameplayAPI free functions using the script's own entity.

bool INativeScript::getPosition(float outPos[3]) const { return GameplayAPI::getPosition(m_entity, outPos); }
bool INativeScript::setPosition(const float pos[3])    { return GameplayAPI::setPosition(m_entity, pos); }
bool INativeScript::getRotation(float outRot[3]) const { return GameplayAPI::getRotation(m_entity, outRot); }
bool INativeScript::setRotation(const float rot[3])    { return GameplayAPI::setRotation(m_entity, rot); }
bool INativeScript::getScale(float outScale[3]) const  { return GameplayAPI::getScale(m_entity, outScale); }
bool INativeScript::setScale(const float scale[3])     { return GameplayAPI::setScale(m_entity, scale); }
bool INativeScript::getTransform(float outPos[3], float outRot[3], float outScale[3]) const { return GameplayAPI::getTransform(m_entity, outPos, outRot, outScale); }
bool INativeScript::translate(const float delta[3])    { return GameplayAPI::translate(m_entity, delta); }
bool INativeScript::rotate(const float delta[3])       { return GameplayAPI::rotate(m_entity, delta); }

void INativeScript::setVelocity(const float vel[3])     { GameplayAPI::setVelocity(m_entity, vel); }
void INativeScript::getVelocity(float outVel[3]) const  { GameplayAPI::getVelocity(m_entity, outVel); }
void INativeScript::addForce(const float force[3])      { GameplayAPI::addForce(m_entity, force); }
void INativeScript::addImpulse(const float impulse[3])  { GameplayAPI::addImpulse(m_entity, impulse); }
void INativeScript::setAngularVelocity(const float vel[3])    { GameplayAPI::setAngularVelocity(m_entity, vel); }
void INativeScript::getAngularVelocity(float outVel[3]) const { GameplayAPI::getAngularVelocity(m_entity, outVel); }

bool INativeScript::getLightColor(float outColor[3]) const        { return GameplayAPI::getLightColor(m_entity, outColor); }
bool INativeScript::setLightColor(const float color[3])           { return GameplayAPI::setLightColor(m_entity, color); }
bool INativeScript::setEmitterProperty(const char* key, float v)  { return GameplayAPI::setEmitterProperty(m_entity, key, v); }
bool INativeScript::setEmitterEnabled(bool enabled)               { return GameplayAPI::setEmitterEnabled(m_entity, enabled); }
bool INativeScript::setEmitterColor(float r, float g, float b, float a)    { return GameplayAPI::setEmitterColor(m_entity, r, g, b, a); }
bool INativeScript::setEmitterEndColor(float r, float g, float b, float a) { return GameplayAPI::setEmitterEndColor(m_entity, r, g, b, a); }

ECS::Entity INativeScript::findEntityByName(const char* name) { return GameplayAPI::findEntityByName(name); }
ECS::Entity INativeScript::createEntity()                     { return GameplayAPI::createEntity(); }
bool INativeScript::removeEntity(ECS::Entity entity)          { return GameplayAPI::removeEntity(entity); }

void INativeScript::logInfo(const char* msg)    { GameplayAPI::logInfo(msg); }
void INativeScript::logWarning(const char* msg) { GameplayAPI::logWarning(msg); }
void INativeScript::logError(const char* msg)   { GameplayAPI::logError(msg); }

float INativeScript::getDeltaTime()  { return GameplayAPI::getDeltaTime(); }
float INativeScript::getTotalTime()  { return GameplayAPI::getTotalTime(); }
