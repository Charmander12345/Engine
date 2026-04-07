#pragma once

#include "JoltBackend.h"

#include <vector>
#include <set>
#include <map>
#include <cstdint>
#include <functional>
#include <memory>

namespace ECS { class ECSManager; }

/// Rigid-body physics simulation built directly on top of `JoltBackend`.
/// The former backend abstraction is retained elsewhere only for incremental
/// migration compatibility.
class PhysicsWorld
{
public:
    static PhysicsWorld& Instance();

    /// Legacy backend selector retained for API compatibility.
    enum class Backend { Jolt };

    /// Initialize with the default backend (Jolt).
    void initialize();

    /// Initialize physics. The backend selector is retained for API
    /// compatibility, but the implementation is Jolt-only.
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

    // ── Overlap queries ─────────────────────────────────────────────
    std::vector<uint32_t> overlapSphere(float cx, float cy, float cz,
                                        float radius) const;
    std::vector<uint32_t> overlapBox(float cx, float cy, float cz,
                                     float hx, float hy, float hz,
                                     float eulerX = 0, float eulerY = 0, float eulerZ = 0) const;

    // ── Sweep queries ───────────────────────────────────────────────
    RaycastHit sweepSphere(float ox, float oy, float oz,
                           float radius,
                           float dx, float dy, float dz,
                           float maxDist = 1000.0f) const;
    RaycastHit sweepBox(float ox, float oy, float oz,
                        float hx, float hy, float hz,
                        float dx, float dy, float dz,
                        float maxDist = 1000.0f) const;

    // ── Force / impulse ────────────────────────────────────────────
    void addForce(uint32_t entity, float fx, float fy, float fz);
    void addImpulse(uint32_t entity, float ix, float iy, float iz);
    void addForceAtPosition(uint32_t entity,
                            float fx, float fy, float fz,
                            float px, float py, float pz);
    void addImpulseAtPosition(uint32_t entity,
                              float ix, float iy, float iz,
                              float px, float py, float pz);

    // ── Velocity (backend-routed) ───────────────────────────────────
    void setVelocity(uint32_t entity, float vx, float vy, float vz);
    void setAngularVelocity(uint32_t entity, float vx, float vy, float vz);

    // ── Sleep / deactivation ────────────────────────────────────────
    void  setSleepThreshold(float t) { m_sleepThreshold = t; }
    float getSleepThreshold() const  { return m_sleepThreshold; }
    bool  isBodySleeping(uint32_t entity) const;

    // ── Rebuild invalidation ────────────────────────────────────────
    void requestBodyRebuild(uint32_t entity);
    void requestAllBodyRebuilds();

private:
    PhysicsWorld() = default;
    ~PhysicsWorld() = default;
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    // ── ECS ↔ Backend sync ──────────────────────────────────────────
    void syncBodiesToBackend();
    void syncBodiesFromBackend();
    void syncCharactersToBackend();
    void syncCharactersFromBackend();
    void updateCharacters(float dt);
    void fireCollisionEvents();
    void updateOverlapTracking();

    // ── State ───────────────────────────────────────────────────────
    float m_gravity[3]{ 0.0f, -9.81f, 0.0f };
    float m_fixedTimestep{ 1.0f / 60.0f };
    float m_accumulator{ 0.0f };
    float m_sleepThreshold{ 0.01f };
    bool  m_initialized{ false };

    // ── Backend ─────────────────────────────────────────────────────
    std::unique_ptr<JoltBackend> m_backend;

    // ── Entity ↔ backend handle tracking ────────────────────────────
    std::set<uint32_t> m_trackedEntities;     // entities that have a body in the backend
    std::set<uint32_t> m_trackedCharacters;   // entities that have a character in the backend
    std::map<uint32_t, uint64_t> m_bodyConfigHashes;
    std::map<uint32_t, uint64_t> m_bodyTransformHashes;

    // ── Collision / overlap data ────────────────────────────────────
    std::vector<CollisionEvent>  m_collisionEvents;
    CollisionCallback            m_collisionCallback;

    std::set<std::pair<uint32_t, uint32_t>> m_activeOverlaps;
    std::vector<OverlapEvent>               m_beginOverlapEvents;
    std::vector<OverlapEvent>               m_endOverlapEvents;
};
