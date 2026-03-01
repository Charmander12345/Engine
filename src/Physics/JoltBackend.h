#pragma once

#include "IPhysicsBackend.h"

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
}

/// Jolt Physics implementation of IPhysicsBackend.
class JoltBackend final : public IPhysicsBackend
{
public:
    JoltBackend();
    ~JoltBackend() override;

    // ── Lifecycle ───────────────────────────────────────────────────
    void initialize() override;
    void shutdown()   override;
    void update(float fixedDt) override;

    // ── Gravity ─────────────────────────────────────────────────────
    void setGravity(float x, float y, float z) override;
    void getGravity(float& x, float& y, float& z) const override;

    // ── Body management ─────────────────────────────────────────────
    BodyHandle createBody(const BodyDesc& desc) override;
    void       removeBody(BodyHandle handle)    override;
    void       removeAllBodies()                override;

    void setBodyPositionRotation(BodyHandle handle,
                                  float px, float py, float pz,
                                  float eulerX, float eulerY, float eulerZ) override;

    BodyState getBodyState(BodyHandle handle) const override;

    void setLinearVelocity(BodyHandle handle, float vx, float vy, float vz)  override;
    void setAngularVelocity(BodyHandle handle, float vx, float vy, float vz) override;

    // ── Collision events ────────────────────────────────────────────
    std::vector<CollisionEventData> drainCollisionEvents() override;

    // ── Queries ─────────────────────────────────────────────────────
    RaycastResult raycast(float ox, float oy, float oz,
                          float dx, float dy, float dz,
                          float maxDist = 1000.0f) const override;

    bool isBodySleeping(BodyHandle handle) const override;

    BodyHandle getBodyForEntity(uint32_t entity) const override;

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

    bool m_initialized{ false };
};
