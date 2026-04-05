#pragma once

#include "IPhysicsBackend.h"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

// Forward-declare PhysX types to keep the header lightweight.
namespace physx {
    class PxFoundation;
    class PxPhysics;
    class PxScene;
    class PxMaterial;
    class PxDefaultAllocator;
    class PxDefaultErrorCallback;
    class PxDefaultCpuDispatcher;
    class PxRigidActor;
    class PxPvd;
    class PxControllerManager;
    class PxController;
}

/// NVIDIA PhysX 5.x implementation of IPhysicsBackend.
class PhysXBackend final : public IPhysicsBackend
{
public:
    PhysXBackend();
    ~PhysXBackend() override;

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

    // ── Character Controller ────────────────────────────────────────
    CharacterHandle createCharacter(const CharacterDesc& desc) override;
    void            removeCharacter(CharacterHandle handle)    override;
    void            removeAllCharacters()                      override;
    void updateCharacter(CharacterHandle handle,
                         float dt,
                         float desiredVelX, float desiredVelY, float desiredVelZ,
                         float gravityX, float gravityY, float gravityZ) override;
    CharacterState getCharacterState(CharacterHandle handle) const override;
    void setCharacterPosition(CharacterHandle handle,
                              float x, float y, float z) override;
    CharacterHandle getCharacterForEntity(uint32_t entity) const override;

private:
    float m_gravity[3]{ 0.0f, -9.81f, 0.0f };

    // ── PhysX core objects ──────────────────────────────────────────
    physx::PxFoundation*            m_foundation{ nullptr };
    physx::PxPhysics*               m_physics{ nullptr };
    physx::PxScene*                 m_scene{ nullptr };
    physx::PxMaterial*              m_defaultMaterial{ nullptr };
    physx::PxDefaultAllocator*      m_allocator{ nullptr };
    physx::PxDefaultErrorCallback*  m_errorCallback{ nullptr };
    physx::PxDefaultCpuDispatcher*  m_dispatcher{ nullptr };
    physx::PxPvd*                   m_pvd{ nullptr };

    // ── Entity ↔ PhysX actor mapping ────────────────────────────────
    std::map<uint32_t, physx::PxRigidActor*> m_entityToActor;
    std::map<physx::PxRigidActor*, uint32_t> m_actorToEntity;

    // ── Collision events (collected during simulation) ──────────────
    struct SimCallbackImpl;
    std::unique_ptr<SimCallbackImpl> m_simCallback;
    std::mutex                       m_collisionMutex;
    std::vector<CollisionEventData>  m_pendingCollisions;

    uint64_t m_nextHandle{ 1 };
    std::map<uint64_t, physx::PxRigidActor*> m_handleToActor;
    std::map<physx::PxRigidActor*, uint64_t> m_actorToHandle;

    // ── Character Controller ────────────────────────────────────────
    physx::PxControllerManager*               m_controllerManager{ nullptr };
    std::map<CharacterHandle, physx::PxController*> m_handleToController;
    std::map<uint32_t, CharacterHandle>        m_entityToControllerHandle;
    CharacterHandle                            m_nextControllerHandle{ 1 };
    // Per-controller vertical velocity (PhysX CCT doesn't track this internally)
    std::map<CharacterHandle, float>           m_controllerVerticalVel;

    bool m_initialized{ false };

    // ── Helpers ─────────────────────────────────────────────────────
    static void eulerToQuat(float ex, float ey, float ez,
                            float& qx, float& qy, float& qz, float& qw);
    static void quatToEuler(float qx, float qy, float qz, float qw,
                            float& ex, float& ey, float& ez);
};
