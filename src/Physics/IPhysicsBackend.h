#pragma once

#include <cstdint>
#include <vector>
#include <functional>

/// Abstract physics backend interface.
/// Concrete implementations (JoltBackend, PhysXBackend, …) provide the
/// simulation, body management and queries.  PhysicsWorld delegates to the
/// active backend while keeping ECS synchronisation in its own layer.
class IPhysicsBackend
{
public:
    virtual ~IPhysicsBackend() = default;

    // ── Lifecycle ───────────────────────────────────────────────────
    virtual void initialize() = 0;
    virtual void shutdown()   = 0;

    /// Advance the internal simulation by one fixed step.
    virtual void update(float fixedDt) = 0;

    // ── Gravity ─────────────────────────────────────────────────────
    virtual void setGravity(float x, float y, float z) = 0;
    virtual void getGravity(float& x, float& y, float& z) const = 0;

    // ── Body descriptor (backend-agnostic creation params) ──────────
    /// Opaque body handle returned by the backend.
    using BodyHandle = uint64_t;
    static constexpr BodyHandle InvalidBody = 0;

    struct BodyDesc
    {
        // Shape ---
        enum class Shape { Box, Sphere, Capsule, Cylinder, HeightField };
        Shape shape{ Shape::Box };

        float halfExtents[3]{ 0.5f, 0.5f, 0.5f };   // Box
        float radius{ 0.5f };                         // Sphere / Capsule / Cylinder
        float halfHeight{ 0.5f };                     // Capsule / Cylinder

        // HeightField data (only for Shape::HeightField)
        const float* heightData{ nullptr };
        int          heightSampleCount{ 0 };
        float        heightOffset[3]{ 0.0f, 0.0f, 0.0f };
        float        heightScale[3]{ 1.0f, 1.0f, 1.0f };

        // Collider offset (local-space)
        float colliderOffset[3]{ 0.0f, 0.0f, 0.0f };

        // Transform ---
        float position[3]{ 0.0f, 0.0f, 0.0f };
        float rotationEulerDeg[3]{ 0.0f, 0.0f, 0.0f };

        // Motion ---
        enum class MotionType { Static, Kinematic, Dynamic };
        MotionType motionType{ MotionType::Static };

        enum class MotionQuality { Discrete, LinearCast };
        MotionQuality motionQuality{ MotionQuality::Discrete };

        // Material ---
        float restitution{ 0.3f };
        float friction{ 0.5f };
        bool  isSensor{ false };

        // Dynamics ---
        float mass{ 1.0f };
        float gravityFactor{ 1.0f };
        float linearDamping{ 0.05f };
        float angularDamping{ 0.05f };
        float maxLinearVelocity{ 500.0f };
        float maxAngularVelocity{ 47.1239f };
        bool  allowSleeping{ true };
        float velocity[3]{ 0.0f, 0.0f, 0.0f };
        float angularVelocity[3]{ 0.0f, 0.0f, 0.0f };

        // Entity ID (so the backend can map body ↔ entity)
        uint32_t entityId{ 0 };
    };

    // ── Body management ─────────────────────────────────────────────
    virtual BodyHandle createBody(const BodyDesc& desc) = 0;
    virtual void       removeBody(BodyHandle handle)    = 0;
    virtual void       removeAllBodies()                = 0;

    /// Update the position/rotation of an existing kinematic or static body.
    virtual void setBodyPositionRotation(BodyHandle handle,
                                         float px, float py, float pz,
                                         float eulerX, float eulerY, float eulerZ) = 0;

    // ── Body readback (after simulation step) ───────────────────────
    struct BodyState
    {
        float position[3]{};
        float rotationEulerDeg[3]{};
        float velocity[3]{};
        float angularVelocityDeg[3]{};
    };

    virtual BodyState getBodyState(BodyHandle handle) const = 0;

    // ── Velocity setters (for scripting / initial velocity) ─────────
    virtual void setLinearVelocity(BodyHandle handle, float vx, float vy, float vz)  = 0;
    virtual void setAngularVelocity(BodyHandle handle, float vx, float vy, float vz) = 0;

    // ── Collision events ────────────────────────────────────────────
    struct CollisionEventData
    {
        uint32_t entityA{ 0 };
        uint32_t entityB{ 0 };
        float    normal[3]{};
        float    depth{ 0.0f };
        float    contactPoint[3]{};
    };

    virtual std::vector<CollisionEventData> drainCollisionEvents() = 0;

    // ── Queries ─────────────────────────────────────────────────────
    struct RaycastResult
    {
        uint32_t entity{ 0 };
        float    point[3]{};
        float    normal[3]{};
        float    distance{ 0.0f };
        bool     hit{ false };
    };

    virtual RaycastResult raycast(float ox, float oy, float oz,
                                  float dx, float dy, float dz,
                                  float maxDist = 1000.0f) const = 0;

    virtual bool isBodySleeping(BodyHandle handle) const = 0;

    // ── Handle ↔ Entity mapping (convenience for PhysicsWorld) ──────
    virtual BodyHandle getBodyForEntity(uint32_t entity) const = 0;
};
