#pragma once

#include "../Actor.h"

/// GameState holds game-wide state visible to all players.
/// Inspired by Unreal Engine's AGameStateBase.
///
/// Override this class to add:
/// - Score tracking
/// - Match timer / phase
/// - Team information
/// - Any data that all players need to see
///
/// There is exactly one GameState per World, created automatically
/// alongside the GameMode.
class GameState : public Actor
{
public:
	// ── Match state ───────────────────────────────────────────────
	/// Returns true if the game has begun play.
	bool hasBegunPlay() const { return m_hasBegunPlay; }

	/// Get elapsed match time in seconds.
	float getMatchTime() const { return m_matchTime; }

	/// Override to update match-specific state each frame.
	void tick(float deltaTime) override
	{
		if (m_hasBegunPlay)
			m_matchTime += deltaTime;
	}

private:
	friend class World;
	bool  m_hasBegunPlay{ false };
	float m_matchTime{ 0.0f };
};
