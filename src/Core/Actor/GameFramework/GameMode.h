#pragma once

#include "../Actor.h"
#include <string>

class GameState;
class PlayerController;
class Pawn;

/// GameMode defines the rules of the game.
/// There is exactly one GameMode per World (set via World::setGameMode).
/// Inspired by Unreal Engine's AGameModeBase.
///
/// Override this class to define:
/// - Which Pawn class to spawn for players
/// - Which PlayerController class to use
/// - What happens when a player joins/leaves
/// - Match flow (start, end, restart)
class GameMode : public Actor
{
public:
	// ── Player management ─────────────────────────────────────────
	/// Called when a new player joins the game.
	virtual void onPlayerJoined(PlayerController* newPlayer) { (void)newPlayer; }

	/// Called when a player leaves the game.
	virtual void onPlayerLeft(PlayerController* exitingPlayer) { (void)exitingPlayer; }

	/// Called after the world's BeginPlay – override to set up match state.
	virtual void onStartPlay() {}

	// ── Class configuration ───────────────────────────────────────
	/// Returns the class name of the default Pawn to spawn for players.
	virtual std::string getDefaultPawnClass() const { return ""; }

	/// Returns the class name of the PlayerController to use.
	virtual std::string getPlayerControllerClass() const { return "PlayerController"; }

	// ── Game state access ─────────────────────────────────────────
	GameState* getGameState() const { return m_gameState; }

private:
	friend class World;
	GameState* m_gameState{ nullptr };
};
