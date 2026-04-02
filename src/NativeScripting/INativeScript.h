#pragma once

#include <string>

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

private:
	friend class NativeScriptManager;
	ECS::Entity m_entity{ 0 };
};
