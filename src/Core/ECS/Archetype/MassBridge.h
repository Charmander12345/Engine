#pragma once
#include "../Archetype/ArchetypeECS.h"
#include "../Archetype/ArchetypeTypes.h"
#include "../../Actor/World.h"
#include "../../Actor/Actor.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace Mass
{

// ── LOD Representation Levels ────────────────────────────────────────
/// Defines how an entity is represented based on camera distance.
/// Inspired by UE's Mass Entity Representation system.
enum class ERepresentationLOD
{
	High,     ///< Full Actor with all components (nearby)
	Medium,   ///< Simplified Actor (mid-range)
	Low,      ///< Instanced rendering, no Actor overhead (far)
	Off       ///< Pure ECS data, no visual (very far / out of range)
};

/// Configuration for LOD distance thresholds.
struct LODConfig
{
	float highToMedium{ 100.0f };    ///< Distance at which High → Medium
	float mediumToLow{ 500.0f };     ///< Distance at which Medium → Low
	float lowToOff{ 2000.0f };       ///< Distance at which Low → Off
	float hysteresis{ 0.1f };        ///< Buffer percentage to prevent flickering
};

/// Tracks the relationship between a MassEntity and an optionally-spawned Actor.
struct MassAgentEntry
{
	MassEntity  massEntity{ InvalidMassEntity };
	Actor*      actor{ nullptr };         ///< Non-null only when promoted to Actor
	std::string actorClassName;           ///< Registry class name for spawning
	ERepresentationLOD currentLOD{ ERepresentationLOD::Off };
};

/// Callback for custom promotion logic (e.g. copy MassEntity fragment data → Actor).
using PromotionCallback = std::function<void(MassEntity, Actor*)>;
/// Callback for custom demotion logic (e.g. copy Actor state → MassEntity fragments).
using DemotionCallback  = std::function<void(Actor*, MassEntity)>;

/// The MassBridge manages LOD-based promotion and demotion between the
/// Archetype ECS (mass entities) and the Actor system.
///
/// Entities that are far from the camera exist as pure MassEntity data
/// (cheap, cache-friendly, batch-processed).  When they come close to
/// the camera, the bridge "promotes" them by spawning a full Actor and
/// synchronising fragment data.  When they move away, the bridge "demotes"
/// them back to pure ECS data and destroys the Actor.
///
/// Usage:
///   1. Register entity class: bridge.registerClass("Citizen", signature, promoCb, demoCb)
///   2. Spawn mass entities: bridge.spawnMassAgents("Citizen", 5000)
///   3. Each frame: bridge.update(cameraPos, world, dt)
class MassBridge
{
public:
	static MassBridge& Instance();

	/// Register an actor class for bridge management.
	void registerClass(const std::string& className,
	                   const ArchetypeSignature& signature,
	                   PromotionCallback onPromote = {},
	                   DemotionCallback onDemote = {});

	/// Spawn a batch of mass-only agents (start at LOD::Off).
	void spawnMassAgents(const std::string& className, size_t count,
	                     std::vector<MassEntity>* outEntities = nullptr);

	/// Update all agents: compute LOD from distance and promote/demote as needed.
	/// @param cameraPos World-space camera position (float[3]).
	/// @param world     The Actor World for spawning/destroying Actors.
	/// @param deltaTime Frame delta time (unused currently, reserved for hysteresis).
	void update(const float cameraPos[3], World& world, float deltaTime);

	/// Set the LOD configuration.
	void setLODConfig(const LODConfig& config) { m_lodConfig = config; }
	const LODConfig& getLODConfig() const { return m_lodConfig; }

	/// Get all agents for a class.
	const std::vector<MassAgentEntry>& getAgents(const std::string& className) const;

	/// Total number of managed agents across all classes.
	size_t totalAgentCount() const;

	/// Clear all agents and class registrations.
	void clear();

private:
	MassBridge() = default;

	struct ClassEntry
	{
		std::string          className;
		ArchetypeSignature   signature;
		PromotionCallback    onPromote;
		DemotionCallback     onDemote;
		std::vector<MassAgentEntry> agents;
	};

	/// Compute distance from camera to an entity's position.
	float computeDistance(const float cameraPos[3], MassEntity entity) const;

	/// Determine the target LOD for a given distance.
	ERepresentationLOD computeLOD(float distance) const;

	/// Determine the target LOD with hysteresis (prevent flickering).
	ERepresentationLOD computeLODWithHysteresis(float distance,
	                                             ERepresentationLOD current) const;

	/// Promote: spawn Actor from MassEntity data.
	void promote(MassAgentEntry& agent, ClassEntry& classEntry, World& world);

	/// Demote: copy Actor state back to MassEntity, destroy Actor.
	void demote(MassAgentEntry& agent, ClassEntry& classEntry, World& world);

	std::unordered_map<std::string, ClassEntry> m_classes;
	LODConfig m_lodConfig;

	/// Empty vector for getAgents() when class not found.
	static inline const std::vector<MassAgentEntry> s_emptyAgents;
};

} // namespace Mass
