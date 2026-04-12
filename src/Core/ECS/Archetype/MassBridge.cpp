#include "MassBridge.h"
#include "ArchetypeECS.h"
#include <cmath>

namespace Mass
{

/// Built-in position fragment for mass entities.
/// Bridge uses this to compute camera distance.
struct MassPositionFragment
{
	float x{ 0.0f };
	float y{ 0.0f };
	float z{ 0.0f };
};

// Register the built-in fragment
MASS_FRAGMENT(MassPositionFragment)

MassBridge& MassBridge::Instance()
{
	static MassBridge s_instance;
	return s_instance;
}

void MassBridge::registerClass(const std::string& className,
                                const ArchetypeSignature& signature,
                                PromotionCallback onPromote,
                                DemotionCallback onDemote)
{
	ClassEntry entry;
	entry.className = className;
	entry.signature = signature;
	entry.onPromote = std::move(onPromote);
	entry.onDemote  = std::move(onDemote);
	m_classes[className] = std::move(entry);
}

void MassBridge::spawnMassAgents(const std::string& className, size_t count,
                                  std::vector<MassEntity>* outEntities)
{
	auto it = m_classes.find(className);
	if (it == m_classes.end()) return;

	auto& classEntry = it->second;
	auto& subsystem = MassEntitySubsystem::Instance();

	std::vector<MassEntity> handles;
	subsystem.createEntities(classEntry.signature, count, &handles);

	for (auto& h : handles)
	{
		MassAgentEntry agent;
		agent.massEntity     = h;
		agent.actorClassName = className;
		agent.currentLOD     = ERepresentationLOD::Off;
		classEntry.agents.push_back(agent);
	}

	if (outEntities)
		outEntities->insert(outEntities->end(), handles.begin(), handles.end());
}

void MassBridge::update(const float cameraPos[3], World& world, float deltaTime)
{
	(void)deltaTime; // reserved for future hysteresis timing

	for (auto& [name, classEntry] : m_classes)
	{
		for (auto& agent : classEntry.agents)
		{
			if (!MassEntitySubsystem::Instance().isAlive(agent.massEntity))
				continue;

			float dist = computeDistance(cameraPos, agent.massEntity);
			ERepresentationLOD targetLOD = computeLODWithHysteresis(dist, agent.currentLOD);

			if (targetLOD == agent.currentLOD)
				continue;

			bool needsActor = (targetLOD == ERepresentationLOD::High ||
			                   targetLOD == ERepresentationLOD::Medium);
			bool hasActor   = (agent.actor != nullptr);

			if (needsActor && !hasActor)
			{
				promote(agent, classEntry, world);
			}
			else if (!needsActor && hasActor)
			{
				demote(agent, classEntry, world);
			}

			agent.currentLOD = targetLOD;
		}
	}
}

const std::vector<MassAgentEntry>& MassBridge::getAgents(const std::string& className) const
{
	auto it = m_classes.find(className);
	return (it != m_classes.end()) ? it->second.agents : s_emptyAgents;
}

size_t MassBridge::totalAgentCount() const
{
	size_t total = 0;
	for (const auto& [name, entry] : m_classes)
		total += entry.agents.size();
	return total;
}

void MassBridge::clear()
{
	m_classes.clear();
}

// ── Private helpers ──────────────────────────────────────────────────

float MassBridge::computeDistance(const float cameraPos[3], MassEntity entity) const
{
	auto* pos = MassEntitySubsystem::Instance().getFragment<MassPositionFragment>(entity);
	if (!pos)
		return 99999.0f; // No position → treat as very far

	float dx = pos->x - cameraPos[0];
	float dy = pos->y - cameraPos[1];
	float dz = pos->z - cameraPos[2];
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

ERepresentationLOD MassBridge::computeLOD(float distance) const
{
	if (distance <= m_lodConfig.highToMedium)
		return ERepresentationLOD::High;
	if (distance <= m_lodConfig.mediumToLow)
		return ERepresentationLOD::Medium;
	if (distance <= m_lodConfig.lowToOff)
		return ERepresentationLOD::Low;
	return ERepresentationLOD::Off;
}

ERepresentationLOD MassBridge::computeLODWithHysteresis(float distance,
                                                         ERepresentationLOD current) const
{
	ERepresentationLOD target = computeLOD(distance);

	// Apply hysteresis: only allow demotion (higher LOD number) if distance exceeds
	// threshold * (1 + hysteresis), and promotion if below threshold * (1 - hysteresis).
	if (target > current)
	{
		// Demoting — require larger distance (add buffer)
		float buffer = m_lodConfig.hysteresis;
		float adjustedDist = distance / (1.0f + buffer);
		target = computeLOD(adjustedDist);
		if (target <= current)
			return current; // Not far enough — stay at current LOD
	}
	else if (target < current)
	{
		// Promoting — require smaller distance (subtract buffer)
		float buffer = m_lodConfig.hysteresis;
		float adjustedDist = distance * (1.0f + buffer);
		target = computeLOD(adjustedDist);
		if (target >= current)
			return current; // Not close enough — stay at current LOD
	}

	return target;
}

void MassBridge::promote(MassAgentEntry& agent, ClassEntry& classEntry, World& world)
{
	// Spawn the actor via ActorRegistry class name
	Actor* actor = world.spawnActorByClass(classEntry.className);
	if (!actor) return;

	// Sync position from MassEntity to Actor
	auto* pos = MassEntitySubsystem::Instance().getFragment<MassPositionFragment>(agent.massEntity);
	if (pos)
		actor->setPosition(pos->x, pos->y, pos->z);

	// Call user promotion callback for custom fragment→Actor sync
	if (classEntry.onPromote)
		classEntry.onPromote(agent.massEntity, actor);

	agent.actor = actor;
}

void MassBridge::demote(MassAgentEntry& agent, ClassEntry& classEntry, World& world)
{
	if (!agent.actor) return;

	// Sync position from Actor back to MassEntity
	float actorPos[3];
	agent.actor->getPosition(actorPos);

	auto* pos = MassEntitySubsystem::Instance().getFragment<MassPositionFragment>(agent.massEntity);
	if (pos)
	{
		pos->x = actorPos[0];
		pos->y = actorPos[1];
		pos->z = actorPos[2];
	}

	// Call user demotion callback for custom Actor→fragment sync
	if (classEntry.onDemote)
		classEntry.onDemote(agent.actor, agent.massEntity);

	// Destroy the actor
	world.destroyActor(agent.actor);
	agent.actor = nullptr;
}

} // namespace Mass
