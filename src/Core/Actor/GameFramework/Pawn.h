#pragma once

#include "../Actor.h"

class PlayerController;

/// Pawn is the base class for actors that can be possessed by a PlayerController.
/// It represents the physical presence of a player in the game world.
/// Inspired by Unreal Engine's APawn.
///
/// Subclass Pawn for characters, vehicles, or any controllable entity.
/// Use CharacterActor (BuiltinActors.h) for characters with a
/// CharacterController already attached.
class Pawn : public Actor
{
public:
	// ── Controller access ─────────────────────────────────────────
	/// Get the controller currently possessing this Pawn (or nullptr).
	PlayerController* getController() const { return m_controller; }

	/// Returns true if a PlayerController possesses this Pawn.
	bool isPossessed() const { return m_controller != nullptr; }

	// ── Lifecycle hooks for possession ────────────────────────────
	/// Called when a controller takes possession of this Pawn.
	virtual void onPossessedBy(PlayerController* controller) { (void)controller; }

	/// Called when a controller releases this Pawn.
	virtual void onUnpossessed() {}

private:
	friend class PlayerController;
	PlayerController* m_controller{ nullptr };
};

// ── PlayerController inline implementations (need Pawn definition) ────

#include "PlayerController.h"

inline void PlayerController::possess(Pawn* pawn)
{
	if (!pawn || pawn == m_possessedPawn)
		return;

	// Release current pawn
	unpossess();

	// If the pawn is already possessed by someone else, release it
	if (pawn->m_controller && pawn->m_controller != this)
		pawn->m_controller->unpossess();

	m_possessedPawn = pawn;
	pawn->m_controller = this;

	onPossess(pawn);
	pawn->onPossessedBy(this);
}

inline void PlayerController::unpossess()
{
	if (!m_possessedPawn)
		return;

	Pawn* oldPawn = m_possessedPawn;
	m_possessedPawn = nullptr;
	oldPawn->m_controller = nullptr;

	onUnpossess(oldPawn);
	oldPawn->onUnpossessed();
}
