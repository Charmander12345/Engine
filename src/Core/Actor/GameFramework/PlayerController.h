#pragma once

#include "../Actor.h"

class Pawn;

/// PlayerController represents the human player's will.
/// It receives input, owns a HUD, and persists across Pawn changes.
/// Inspired by Unreal Engine's APlayerController.
///
/// Key design: the PlayerController survives Pawn death/respawn.
/// Possession transfers control between the controller and a Pawn.
class PlayerController : public Actor
{
public:
	// ── Possession ────────────────────────────────────────────────
	/// Take control of a Pawn.  Unpossesses any currently possessed Pawn.
	void possess(Pawn* pawn);

	/// Release control of the currently possessed Pawn.
	void unpossess();

	/// Get the currently possessed Pawn (or nullptr).
	Pawn* getPawn() const { return m_possessedPawn; }

	// ── Lifecycle hooks for possession ────────────────────────────
	/// Called after this controller possesses a new Pawn.
	virtual void onPossess(Pawn* pawn) { (void)pawn; }

	/// Called when this controller releases a Pawn.
	virtual void onUnpossess(Pawn* oldPawn) { (void)oldPawn; }

	// ── Player index ─────────────────────────────────────────────
	int getPlayerIndex() const { return m_playerIndex; }

private:
	friend class World;
	Pawn* m_possessedPawn{ nullptr };
	int   m_playerIndex{ 0 };
};
