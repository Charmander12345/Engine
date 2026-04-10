#pragma once

#include <string>

class Actor;

/// Base class for all actor components.
/// An ActorComponent adds modular functionality to an Actor.
/// Game developers can create custom components by inheriting from this class.
class ActorComponent
{
public:
	ActorComponent() = default;
	virtual ~ActorComponent() = default;

	// ── Lifecycle (override in subclasses) ─────────────────────────
	/// Called when the component is added to an actor.
	virtual void onRegister() {}

	/// Called when the component is removed from an actor.
	virtual void onUnregister() {}

	/// Called once when the owning actor's BeginPlay fires.
	virtual void beginPlay() {}

	/// Called every frame if the component is enabled.
	virtual void tickComponent(float deltaTime) { (void)deltaTime; }

	/// Called when the owning actor's EndPlay fires.
	virtual void endPlay() {}

	// ── Accessors ─────────────────────────────────────────────────
	Actor* getOwner() const { return m_owner; }

	bool isEnabled() const { return m_enabled; }
	void setEnabled(bool enabled) { m_enabled = enabled; }

private:
	friend class Actor;

	Actor* m_owner{ nullptr };
	bool m_enabled{ true };
};
