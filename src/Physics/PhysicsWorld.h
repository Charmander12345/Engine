#pragma once

#include <vector>
#include <set>
#include <cstdint>
#include <functional>

namespace ECS { class ECSManager; }

/// Lightweight rigid-body physics simulation that operates on ECS entities with
/// a PhysicsComponent and a TransformComponent.  Uses impulse-based collision
/// resolution with a fixed-timestep accumulator.
class PhysicsWorld
{
public:
    static PhysicsWorld& Instance();

    void initialize();
    void shutdown();

    /// Advance the simulation by `dt` seconds (variable frame delta).
    /// Internally uses a fixed timestep with an accumulator.
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

    // ── Internal types ──────────────────────────────────────────────
    struct RigidBody
    {
        uint32_t entity{ 0 };
        float position[3]{};
        float rotation[3]{};
        float scale[3]{ 1,1,1 };
        float velocity[3]{};
        float angularVelocity[3]{};
        float mass{ 1.0f };
        float invMass{ 1.0f };
        float restitution{ 0.3f };
        float friction{ 0.5f };
        float colliderSize[3]{ 1,1,1 };
        int   colliderType{ 0 }; // 0=Box, 1=Sphere
        bool  isStatic{ true };
        bool  useGravity{ true };
        bool  isKinematic{ false };
        float sleepTimer{ 0.0f };
        bool  isSleeping{ false };
    };

    struct ContactPoint
    {
        int  bodyA{ -1 };
        int  bodyB{ -1 };
        float normal[3]{};   // from A to B
        float depth{ 0.0f };
        float contactPoint[3]{};
    };

    // ── Simulation steps ────────────────────────────────────────────
    void gatherBodies();
    void integrate(float ts);
    void detectCollisions();
    void resolveCollisions();
    void writeback();

    // ── Collision helpers ───────────────────────────────────────────
    bool testSphereSphere(const RigidBody& a, const RigidBody& b, ContactPoint& out) const;
    bool testBoxBox(const RigidBody& a, const RigidBody& b, ContactPoint& out) const;
    bool testSphereBox(const RigidBody& sphere, const RigidBody& box, ContactPoint& out) const;

    // ── Raycast helpers ─────────────────────────────────────────────
    bool rayTestBox(const float origin[3], const float dir[3], float maxDist,
                    const RigidBody& body, float& outDist, float outNormal[3]) const;
    bool rayTestSphere(const float origin[3], const float dir[3], float maxDist,
                       const RigidBody& body, float& outDist, float outNormal[3]) const;

    // ── Collision event dispatch ─────────────────────────────────────
    void fireCollisionEvents();
    void updateOverlapTracking();

    // ── State ───────────────────────────────────────────────────────
    float m_gravity[3]{ 0.0f, -9.81f, 0.0f };
    float m_fixedTimestep{ 1.0f / 60.0f };
    float m_accumulator{ 0.0f };
    float m_sleepThreshold{ 0.01f };
    float m_sleepTime{ 0.5f };
    bool  m_initialized{ false };

    std::vector<RigidBody>       m_bodies;
    std::vector<ContactPoint>    m_contacts;
    std::vector<CollisionEvent>  m_collisionEvents;
    CollisionCallback            m_collisionCallback;

    // ── Overlap tracking ────────────────────────────────────────────
    std::set<std::pair<uint32_t, uint32_t>> m_activeOverlaps;
    std::vector<OverlapEvent>               m_beginOverlapEvents;
    std::vector<OverlapEvent>               m_endOverlapEvents;
};
