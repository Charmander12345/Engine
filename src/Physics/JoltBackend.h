#pragma once

#include "PhysicsTypes.h"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

// Forward-declare Jolt types to keep the header lightweight.
namespace JPH {
    class PhysicsSystem;
    class TempAllocator;
    class JobSystemThreadPool;
    class Factory;
    class BodyInterface;
    class ContactListener;
    class BroadPhaseLayerInterface;
    class ObjectVsBroadPhaseLayerFilter;
    class ObjectLayerPairFilter;
    struct BodyID;
    class CharacterVirtual;
}

/// Direct Jolt-backed physics implementation.
/// `IPhysicsBackend` types are kept here only as compatibility-facing data
/// shapes for externally exposed layers.
class JoltBackend final
{
public:
    using BodyHandle = PhysicsTypes::BodyHandle;
    static constexpr BodyHandle InvalidBody = PhysicsTypes::InvalidBody;
    using BodyDesc = PhysicsTypes::BodyDesc;
    using BodyState = PhysicsTypes::BodyState;
    using CollisionEventData = PhysicsTypes::CollisionEventData;
    using RaycastResult = PhysicsTypes::RaycastResult;
    using CharacterHandle = PhysicsTypes::CharacterHandle;
    static constexpr CharacterHandle InvalidCharacter = PhysicsTypes::InvalidCharacter;
    using CharacterDesc = PhysicsTypes::CharacterDesc;
    using CharacterState = PhysicsTypes::CharacterState;

    JoltBackend();
    ~JoltBackend();

    // ── Lifecycle ───────────────────────────────────────────────────
    void initialize();
    void shutdown();
    void update(float fixedDt);

    // ── Gravity ─────────────────────────────────────────────────────
    void setGravity(float x, float y, float z);
    void getGravity(float& x, float& y, float& z) const;

    // ── Body management ─────────────────────────────────────────────
    BodyHandle createBody(const BodyDesc& desc);
    void       removeBody(BodyHandle handle);
    void       removeAllBodies();

    void setBodyPositionRotation(BodyHandle handle,
                                  float px, float py, float pz,
                                   float eulerX, float eulerY, float eulerZ);

    BodyState getBodyState(BodyHandle handle) const;

    void setLinearVelocity(BodyHandle handle, float vx, float vy, float vz);
    void setAngularVelocity(BodyHandle handle, float vx, float vy, float vz);

    void addForce(BodyHandle handle, float fx, float fy, float fz);
    void addImpulse(BodyHandle handle, float ix, float iy, float iz);

    // ── Collision events ────────────────────────────────────────────
    std::vector<CollisionEventData> drainCollisionEvents();

    // ── Queries ─────────────────────────────────────────────────────
    RaycastResult raycast(float ox, float oy, float oz,
                          float dx, float dy, float dz,
                          float maxDist = 1000.0f) const;

    std::vector<uint32_t> overlapSphere(float cx, float cy, float cz,
                                        float radius) const;
    std::vector<uint32_t> overlapBox(float cx, float cy, float cz,
                                     float hx, float hy, float hz,
                                     float eulerX = 0, float eulerY = 0, float eulerZ = 0) const;
    RaycastResult sweepSphere(float ox, float oy, float oz,
                              float radius,
                              float dx, float dy, float dz,
                              float maxDist = 1000.0f) const;
    RaycastResult sweepBox(float ox, float oy, float oz,
                           float hx, float hy, float hz,
                           float dx, float dy, float dz,
                           float maxDist = 1000.0f) const;

    void addForceAtPosition(BodyHandle handle,
                            float fx, float fy, float fz,
                            float px, float py, float pz);
    void addImpulseAtPosition(BodyHandle handle,
                              float ix, float iy, float iz,
                              float px, float py, float pz);

    bool isBodySleeping(BodyHandle handle) const;

    BodyHandle getBodyForEntity(uint32_t entity) const;

    // ── Character Controller ────────────────────────────────────────
    CharacterHandle createCharacter(const CharacterDesc& desc);
    void            removeCharacter(CharacterHandle handle);
    void            removeAllCharacters();
    void updateCharacter(CharacterHandle handle,
                         float dt,
                         float desiredVelX, float desiredVelY, float desiredVelZ,
                         float gravityX, float gravityY, float gravityZ);
    CharacterState getCharacterState(CharacterHandle handle) const;
    void setCharacterPosition(CharacterHandle handle,
                              float x, float y, float z);
    CharacterHandle getCharacterForEntity(uint32_t entity) const;

private:
    float m_gravity[3]{ 0.0f, -9.81f, 0.0f };

    // ── Jolt systems (pImpl-style via unique_ptr) ───────────────────
    std::unique_ptr<JPH::TempAllocator>                  m_tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool>            m_jobSystem;
    std::unique_ptr<JPH::PhysicsSystem>                  m_physicsSystem;
    std::unique_ptr<JPH::BroadPhaseLayerInterface>       m_broadPhaseLayerInterface;
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter>  m_objectVsBroadPhaseFilter;
    std::unique_ptr<JPH::ObjectLayerPairFilter>          m_objectLayerPairFilter;
    std::unique_ptr<JPH::ContactListener>                m_contactListener;

    // ── Entity ↔ Jolt BodyID mapping ────────────────────────────────
    std::map<uint32_t, uint32_t> m_entityToBodyID;   // ECS entity → Jolt BodyID raw value
    std::map<uint32_t, uint32_t> m_bodyIDToEntity;   // Jolt BodyID raw value → ECS entity

    // ── Character Controller ────────────────────────────────────────
    std::map<CharacterHandle, JPH::CharacterVirtual*> m_handleToCharacter;
    std::map<uint32_t, CharacterHandle>               m_entityToCharacterHandle;
    CharacterHandle                                    m_nextCharacterHandle{ 1 };

    bool m_initialized{ false };
};
