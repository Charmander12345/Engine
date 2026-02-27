#pragma once

#include <vector>
#include <set>
#include <map>
#include <cstdint>
#include <functional>
#include <memory>

class IPhysicsBackend;

namespace ECS { class ECSManager; }

/// Rigid-body physics simulation – backend-agnostic.
/// Delegates to IPhysicsBackend (JoltBackend, PhysXBackend, …) while keeping
/// ECS synchronisation and event dispatch in this layer.
class PhysicsWorld
{
public:
    static PhysicsWorld& Instance();

    /// Available physics backend implementations.
    enum class Backend { Jolt, PhysX };

    /// Initialize with the default backend (Jolt).
    void initialize();

    /// Initialize with a specific backend.
    void initialize(Backend backend);

    void shutdown();

    /// Advance the simulation by `dt` seconds (variable frame delta).
    void step(float dt);

    /// World-space gravity (m/s^2).  Default: (0, -9.81, 0).
    void  setGravity(float x, float y, float z);
    void  getGravity(float& x, float& y, float& z) const;

    /// Fixed timestep used by the integrator (default 1/60).
    void  setFixedTimestep(float ts) { m_fixedTimestep = ts; }
    float getFixedTimestep() const { return m_fixedTimestep; }

    // ── Collision events ────────────────────────────────────────────
    struct CollisionEvent
    {
        uint32_t entityA{ 0 };
        uint32_t entityB{ 0 };
        float normal[3]{};
        float depth{ 0.0f };
        float contactPoint[3]{};
    };

    using CollisionCallback = std::function<void(const CollisionEvent&)>;
    void setCollisionCallback(CollisionCallback cb) { m_collisionCallback = std::move(cb); }
    const std::vector<CollisionEvent>& getCollisionEvents() const { return m_collisionEvents; }

    // ── Overlap events (begin / end) ─────────────────────────────────
    struct OverlapEvent
    {
        uint32_t entityA{ 0 };
        uint32_t entityB{ 0 };
    };

    const std::vector<OverlapEvent>& getBeginOverlapEvents() const { return m_beginOverlapEvents; }
    const std::vector<OverlapEvent>& getEndOverlapEvents()   const { return m_endOverlapEvents; }

    // ── Raycast ─────────────────────────────────────────────────────
    struct RaycastHit
    {
        uint32_t entity{ 0 };
        float point[3]{};
        float normal[3]{};
        float distance{ 0.0f };
        bool  hit{ false };
    };

    RaycastHit raycast(float ox, float oy, float oz,
                       float dx, float dy, float dz,
                       float maxDist = 1000.0f) const;

    // ── Sleep / deactivation ────────────────────────────────────────
    void  setSleepThreshold(float t) { m_sleepThreshold = t; }
    float getSleepThreshold() const  { return m_sleepThreshold; }
    bool  isBodySleeping(uint32_t entity) const;

private:
    PhysicsWorld() = default;
    ~PhysicsWorld() = default;
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // ── ECS ↔ Backend sync ──────────────────────────────────────────
    void syncBodiesToBackend();
    void syncBodiesFromBackend();
    void fireCollisionEvents();
    void updateOverlapTracking();

    // ── State ───────────────────────────────────────────────────────
    float m_gravity[3]{ 0.0f, -9.81f, 0.0f };
    float m_fixedTimestep{ 1.0f / 60.0f };
    float m_accumulator{ 0.0f };
    float m_sleepThreshold{ 0.01f };
    bool  m_initialized{ false };

    // ── Backend ─────────────────────────────────────────────────────
    std::unique_ptr<IPhysicsBackend> m_backend;

    // ── Entity ↔ backend handle tracking ────────────────────────────
    std::set<uint32_t> m_trackedEntities;   // entities that have a body in the backend

    // ── Collision / overlap data ────────────────────────────────────
    std::vector<CollisionEvent>  m_collisionEvents;
    CollisionCallback            m_collisionCallback;

    std::set<std::pair<uint32_t, uint32_t>> m_activeOverlaps;
    std::vector<OverlapEvent>               m_beginOverlapEvents;
    std::vector<OverlapEvent>               m_endOverlapEvents;
};
