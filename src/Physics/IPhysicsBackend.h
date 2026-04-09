#pragma once

#include "PhysicsTypes.h"

#include <vector>
#include <functional>

/// Legacy physics backend compatibility interface.
/// The active engine runtime is Jolt-based, but this interface is retained so
/// externally exposed layers can keep stable function shapes while the engine
/// internals migrate to direct `JoltBackend` usage.
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
    using BodyHandle = PhysicsTypes::BodyHandle;
    static constexpr BodyHandle InvalidBody = PhysicsTypes::InvalidBody;
    using BodyDesc = PhysicsTypes::BodyDesc;

    // ── Body management ─────────────────────────────────────────────
    virtual BodyHandle createBody(const BodyDesc& desc) = 0;
    virtual void       removeBody(BodyHandle handle)    = 0;
    virtual void       removeAllBodies()                = 0;

    /// Update the position/rotation of an existing kinematic or static body.
    virtual void setBodyPositionRotation(BodyHandle handle,
                                         float px, float py, float pz,
                                         float eulerX, float eulerY, float eulerZ) = 0;

    // ── Body readback (after simulation step) ───────────────────────
    using BodyState = PhysicsTypes::BodyState;

    virtual BodyState getBodyState(BodyHandle handle) const = 0;

    // ── Velocity setters (for scripting / initial velocity) ─────────
    virtual void setLinearVelocity(BodyHandle handle, float vx, float vy, float vz)  = 0;
    virtual void setAngularVelocity(BodyHandle handle, float vx, float vy, float vz) = 0;

    // ── Force / impulse (at center of mass) ─────────────────────────
    virtual void addForce(BodyHandle handle, float fx, float fy, float fz)     = 0;
    virtual void addImpulse(BodyHandle handle, float ix, float iy, float iz)   = 0;

    // ── Collision events ────────────────────────────────────────────
    using CollisionEventData = PhysicsTypes::CollisionEventData;

    virtual std::vector<CollisionEventData> drainCollisionEvents() = 0;

    // ── Queries ─────────────────────────────────────────────────────
    using RaycastResult = PhysicsTypes::RaycastResult;

    virtual RaycastResult raycast(float ox, float oy, float oz,
                                  float dx, float dy, float dz,
                                  float maxDist = 1000.0f) const = 0;

    /// Overlap a sphere against the world – returns entity IDs of all overlapping bodies.
    virtual std::vector<uint32_t> overlapSphere(float cx, float cy, float cz,
                                                float radius) const = 0;

    /// Overlap a box against the world – returns entity IDs of all overlapping bodies.
    virtual std::vector<uint32_t> overlapBox(float cx, float cy, float cz,
                                             float hx, float hy, float hz,
                                             float eulerX = 0, float eulerY = 0, float eulerZ = 0) const = 0;

    /// Sweep a sphere from origin along direction – returns closest hit.
    virtual RaycastResult sweepSphere(float ox, float oy, float oz,
                                      float radius,
                                      float dx, float dy, float dz,
                                      float maxDist = 1000.0f) const = 0;

    /// Sweep a box from origin along direction – returns closest hit.
    virtual RaycastResult sweepBox(float ox, float oy, float oz,
                                   float hx, float hy, float hz,
                                   float dx, float dy, float dz,
                                   float maxDist = 1000.0f) const = 0;

    // ── Force / impulse at world-space position ─────────────────────
    /// Apply a force at a specific world-space position (generates torque).
    virtual void addForceAtPosition(BodyHandle handle,
                                    float fx, float fy, float fz,
                                    float px, float py, float pz) = 0;

    /// Apply an impulse at a specific world-space position (generates angular impulse).
    virtual void addImpulseAtPosition(BodyHandle handle,
                                      float ix, float iy, float iz,
                                      float px, float py, float pz) = 0;

    virtual bool isBodySleeping(BodyHandle handle) const = 0;

    // ── Handle ↔ Entity mapping (convenience for PhysicsWorld) ──────
    virtual BodyHandle getBodyForEntity(uint32_t entity) const = 0;

    // ── Character Controller ────────────────────────────────────────
    using CharacterHandle = PhysicsTypes::CharacterHandle;
    static constexpr CharacterHandle InvalidCharacter = PhysicsTypes::InvalidCharacter;
    using CharacterDesc = PhysicsTypes::CharacterDesc;
    using CharacterState = PhysicsTypes::CharacterState;

    virtual CharacterHandle createCharacter(const CharacterDesc& desc) = 0;
    virtual void            removeCharacter(CharacterHandle handle)    = 0;
    virtual void            removeAllCharacters()                      = 0;

    /// Move the character by desired velocity for one fixed step.
    /// The backend handles collision, step-up, slope limiting internally.
    virtual void updateCharacter(CharacterHandle handle,
                                 float dt,
                                 float desiredVelX, float desiredVelY, float desiredVelZ,
                                 float gravityX, float gravityY, float gravityZ) = 0;

    /// Read back the character's state after the update.
    virtual CharacterState getCharacterState(CharacterHandle handle) const = 0;

    /// Teleport the character to a specific position (no collision).
    virtual void setCharacterPosition(CharacterHandle handle,
                                      float x, float y, float z) = 0;

    /// Get character handle for a given entity (InvalidCharacter if none).
    virtual CharacterHandle getCharacterForEntity(uint32_t entity) const = 0;
};
