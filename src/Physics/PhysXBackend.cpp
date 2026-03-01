#include "PhysXBackend.h"

// Include Logger BEFORE PhysX so that PxPhysicsAPI.h (which pulls in Windows.h)
// does not clobber Logger::LogLevel::ERROR with the Windows ERROR macro.
#include "../Logger/Logger.h"

// Save and restore the ERROR macro that Windows.h defines.
#ifdef ERROR
#define _ENGINE_SAVED_ERROR ERROR
#undef ERROR
#endif

#include <PxPhysicsAPI.h>

// Restore ERROR macro for other translation units.
#ifdef _ENGINE_SAVED_ERROR
#define ERROR _ENGINE_SAVED_ERROR
#undef _ENGINE_SAVED_ERROR
#endif

#include <cmath>
#include <algorithm>
#include <cstring>
#include <thread>

using namespace physx;

// ── Euler ↔ Quaternion helpers (Y-Yaw, X-Pitch, Z-Roll, same as JoltBackend) ──

void PhysXBackend::eulerToQuat(float ex, float ey, float ez,
                               float& qx, float& qy, float& qz, float& qw)
{
    constexpr float deg2rad = 3.14159265358979f / 180.0f;
    const float hx = ex * deg2rad * 0.5f;
    const float hy = ey * deg2rad * 0.5f;
    const float hz = ez * deg2rad * 0.5f;
    const float cx = cosf(hx), sx = sinf(hx);
    const float cy = cosf(hy), sy = sinf(hy);
    const float cz = cosf(hz), sz = sinf(hz);
    qw = cy * cx * cz + sy * sx * sz;
    qx = cy * sx * cz + sy * cx * sz;
    qy = sy * cx * cz - cy * sx * sz;
    qz = cy * cx * sz - sy * sx * cz;
}

void PhysXBackend::quatToEuler(float qx, float qy, float qz, float qw,
                               float& ex, float& ey, float& ez)
{
    constexpr float rad2deg = 180.0f / 3.14159265358979f;
    const float sinp = 2.0f * (qw * qx + qy * qz);
    if (fabsf(sinp) >= 1.0f)
        ex = copysignf(90.0f, sinp);
    else
        ex = asinf(sinp) * rad2deg;
    const float siny_cosp = 2.0f * (qw * qy - qz * qx);
    const float cosy_cosp = 1.0f - 2.0f * (qx * qx + qy * qy);
    ey = atan2f(siny_cosp, cosy_cosp) * rad2deg;
    const float sinr_cosp = 2.0f * (qw * qz - qx * qy);
    const float cosr_cosp = 1.0f - 2.0f * (qy * qy + qz * qz);
    ez = atan2f(sinr_cosp, cosr_cosp) * rad2deg;
}

// ── Simulation event callback (contact listener) ─────────────────────────

struct PhysXBackend::SimCallbackImpl : public PxSimulationEventCallback
{
    PhysXBackend* backend{ nullptr };

    void onContact(const PxContactPairHeader& pairHeader, const PxContactPair* pairs, PxU32 nbPairs) override
    {
        if (!backend) return;
        for (PxU32 i = 0; i < nbPairs; ++i)
        {
            const PxContactPair& cp = pairs[i];
            if (cp.events & PxPairFlag::eNOTIFY_TOUCH_FOUND)
            {
                PxContactPairPoint contacts[1];
                PxU32 nbContacts = cp.extractContacts(contacts, 1);

                IPhysicsBackend::CollisionEventData evt{};

                auto* actorA = static_cast<PxRigidActor*>(pairHeader.actors[0]);
                auto* actorB = static_cast<PxRigidActor*>(pairHeader.actors[1]);
                auto itA = backend->m_actorToEntity.find(actorA);
                auto itB = backend->m_actorToEntity.find(actorB);
                evt.entityA = (itA != backend->m_actorToEntity.end()) ? itA->second : 0;
                evt.entityB = (itB != backend->m_actorToEntity.end()) ? itB->second : 0;

                if (nbContacts > 0)
                {
                    evt.normal[0] = contacts[0].normal.x;
                    evt.normal[1] = contacts[0].normal.y;
                    evt.normal[2] = contacts[0].normal.z;
                    evt.depth     = contacts[0].separation;
                    evt.contactPoint[0] = contacts[0].position.x;
                    evt.contactPoint[1] = contacts[0].position.y;
                    evt.contactPoint[2] = contacts[0].position.z;
                }

                std::lock_guard<std::mutex> lock(backend->m_collisionMutex);
                backend->m_pendingCollisions.push_back(evt);
            }
        }
    }

    void onConstraintBreak(PxConstraintInfo*, PxU32) override {}
    void onWake(PxActor**, PxU32) override {}
    void onSleep(PxActor**, PxU32) override {}
    void onTrigger(PxTriggerPair*, PxU32) override {}
    void onAdvance(const PxRigidBody* const*, const PxTransform*, const PxU32) override {}
};

// ── Constructor / Destructor ─────────────────────────────────────────────

PhysXBackend::PhysXBackend()  = default;
PhysXBackend::~PhysXBackend() { shutdown(); }

// ── Lifecycle ────────────────────────────────────────────────────────────

void PhysXBackend::initialize()
{
    if (m_initialized) return;

    m_allocator     = new PxDefaultAllocator();
    m_errorCallback = new PxDefaultErrorCallback();

    m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, *m_allocator, *m_errorCallback);
    if (!m_foundation)
    {
        Logger::Instance().log(Logger::Category::Engine, "PhysX: PxCreateFoundation failed", Logger::LogLevel::ERROR);
        return;
    }

    m_pvd = PxCreatePvd(*m_foundation);

    m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, PxTolerancesScale(), true, m_pvd);
    if (!m_physics)
    {
        Logger::Instance().log(Logger::Category::Engine, "PhysX: PxCreatePhysics failed", Logger::LogLevel::ERROR);
        return;
    }

    PxInitExtensions(*m_physics, m_pvd);

    PxSceneDesc sceneDesc(m_physics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(m_gravity[0], m_gravity[1], m_gravity[2]);

    const int numThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    m_dispatcher = PxDefaultCpuDispatcherCreate(numThreads);
    sceneDesc.cpuDispatcher = m_dispatcher;

    sceneDesc.filterShader = PxDefaultSimulationFilterShader;

    m_simCallback = std::make_unique<SimCallbackImpl>();
    m_simCallback->backend = this;
    sceneDesc.simulationEventCallback = m_simCallback.get();

    // Enable contact reporting for all collision pairs.
    sceneDesc.filterShader = [](PxFilterObjectAttributes attributes0, PxFilterData /*filterData0*/,
                                PxFilterObjectAttributes attributes1, PxFilterData /*filterData1*/,
                                PxPairFlags& pairFlags, const void* /*constantBlock*/,
                                PxU32 /*constantBlockSize*/) -> PxFilterFlags
    {
        if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1))
        {
            pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
            return PxFilterFlag::eDEFAULT;
        }
        pairFlags = PxPairFlag::eCONTACT_DEFAULT
                  | PxPairFlag::eNOTIFY_TOUCH_FOUND
                  | PxPairFlag::eNOTIFY_CONTACT_POINTS;
        return PxFilterFlag::eDEFAULT;
    };

    m_scene = m_physics->createScene(sceneDesc);
    if (!m_scene)
    {
        Logger::Instance().log(Logger::Category::Engine, "PhysX: createScene failed", Logger::LogLevel::ERROR);
        return;
    }

    m_defaultMaterial = m_physics->createMaterial(0.5f, 0.5f, 0.3f);

    m_initialized = true;
    Logger::Instance().log(Logger::Category::Engine, "PhysX backend initialized (threads: " + std::to_string(numThreads) + ")", Logger::LogLevel::INFO);
}

void PhysXBackend::shutdown()
{
    if (!m_initialized) return;

    removeAllBodies();

    if (m_scene)       { m_scene->release();       m_scene = nullptr; }
    if (m_dispatcher)  { m_dispatcher->release();   m_dispatcher = nullptr; }

    PxCloseExtensions();

    if (m_physics)     { m_physics->release();      m_physics = nullptr; }
    if (m_pvd)         { m_pvd->release();          m_pvd = nullptr; }
    if (m_foundation)  { m_foundation->release();   m_foundation = nullptr; }

    m_simCallback.reset();

    delete m_errorCallback; m_errorCallback = nullptr;
    delete m_allocator;     m_allocator = nullptr;

    m_initialized = false;
    Logger::Instance().log(Logger::Category::Engine, "PhysX backend shut down", Logger::LogLevel::INFO);
}

void PhysXBackend::update(float fixedDt)
{
    if (!m_scene) return;
    m_scene->simulate(fixedDt);
    m_scene->fetchResults(true);
}

// ── Gravity ──────────────────────────────────────────────────────────────

void PhysXBackend::setGravity(float x, float y, float z)
{
    m_gravity[0] = x; m_gravity[1] = y; m_gravity[2] = z;
    if (m_scene) m_scene->setGravity(PxVec3(x, y, z));
}

void PhysXBackend::getGravity(float& x, float& y, float& z) const
{
    x = m_gravity[0]; y = m_gravity[1]; z = m_gravity[2];
}

// ── Body management ──────────────────────────────────────────────────────

IPhysicsBackend::BodyHandle PhysXBackend::createBody(const BodyDesc& desc)
{
    if (!m_physics || !m_scene) return InvalidBody;

    // ── Shape ──────────────────────────────────────────────────────
    PxMaterial* mat = m_physics->createMaterial(desc.friction, desc.friction, desc.restitution);
    if (!mat) mat = m_defaultMaterial;

    PxShape* shape = nullptr;

    switch (desc.shape)
    {
    case BodyDesc::Shape::Sphere:
        shape = m_physics->createShape(PxSphereGeometry(desc.radius), *mat, true);
        break;
    case BodyDesc::Shape::Capsule:
        shape = m_physics->createShape(PxCapsuleGeometry(desc.radius, desc.halfHeight), *mat, true);
        break;
    case BodyDesc::Shape::Cylinder:
        // PhysX has no native cylinder – approximate with capsule.
        shape = m_physics->createShape(PxCapsuleGeometry(desc.radius, desc.halfHeight), *mat, true);
        break;
    case BodyDesc::Shape::HeightField:
    {
        if (desc.heightData && desc.heightSampleCount > 1)
        {
            // heightSampleCount is already the per-side count (NOT total samples).
            const int side = desc.heightSampleCount;
            const int total = side * side;
            std::vector<PxHeightFieldSample> samples(total);
            for (int i = 0; i < total; ++i)
            {
                samples[i].height  = static_cast<PxI16>(std::clamp(desc.heightData[i] * 32767.0f, -32768.0f, 32767.0f));
                samples[i].materialIndex0 = 0;
                samples[i].materialIndex1 = 0;
            }
            PxHeightFieldDesc hfDesc;
            hfDesc.nbRows    = side;   // rows → Z direction
            hfDesc.nbColumns = side;   // columns → X direction
            hfDesc.samples.data   = samples.data();
            hfDesc.samples.stride = sizeof(PxHeightFieldSample);
            PxHeightField* hf = PxCreateHeightField(hfDesc, m_physics->getPhysicsInsertionCallback());
            if (hf)
            {
                // PxHeightFieldGeometry: heightScale(Y), rowScale(Z), columnScale(X)
                shape = m_physics->createShape(
                    PxHeightFieldGeometry(hf, PxMeshGeometryFlags(),
                                          desc.heightScale[1], desc.heightScale[2], desc.heightScale[0]),
                    *mat, true);
                hf->release();

                // Apply heightfield offset so collision matches the visual mesh
                if (shape)
                {
                    shape->setLocalPose(PxTransform(PxVec3(
                        desc.heightOffset[0], desc.heightOffset[1], desc.heightOffset[2])));
                }
            }
        }
        if (!shape)
            shape = m_physics->createShape(PxBoxGeometry(desc.halfExtents[0], desc.halfExtents[1], desc.halfExtents[2]), *mat, true);
        break;
    }
    case BodyDesc::Shape::Box:
    default:
        shape = m_physics->createShape(PxBoxGeometry(desc.halfExtents[0], desc.halfExtents[1], desc.halfExtents[2]), *mat, true);
        break;
    }

    if (!shape) return InvalidBody;

    // Apply collider offset as local pose.
    if (desc.colliderOffset[0] != 0.0f || desc.colliderOffset[1] != 0.0f || desc.colliderOffset[2] != 0.0f)
        shape->setLocalPose(PxTransform(PxVec3(desc.colliderOffset[0], desc.colliderOffset[1], desc.colliderOffset[2])));

    // Sensor/trigger.
    if (desc.isSensor)
    {
        shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
        shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);
    }

    // ── Transform ──────────────────────────────────────────────────
    float qx, qy, qz, qw;
    eulerToQuat(desc.rotationEulerDeg[0], desc.rotationEulerDeg[1], desc.rotationEulerDeg[2], qx, qy, qz, qw);
    PxTransform pose(PxVec3(desc.position[0], desc.position[1], desc.position[2]),
                     PxQuat(qx, qy, qz, qw));

    // ── Actor ──────────────────────────────────────────────────────
    PxRigidActor* actor = nullptr;

    if (desc.motionType == BodyDesc::MotionType::Static)
    {
        PxRigidStatic* s = m_physics->createRigidStatic(pose);
        s->attachShape(*shape);
        actor = s;
    }
    else
    {
        PxRigidDynamic* d = m_physics->createRigidDynamic(pose);
        d->attachShape(*shape);

        if (desc.motionType == BodyDesc::MotionType::Kinematic)
            d->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, true);

        PxRigidBodyExt::updateMassAndInertia(*d, desc.mass > 0.0f ? (10.0f / desc.mass) : 10.0f);

        d->setLinearDamping(desc.linearDamping);
        d->setAngularDamping(desc.angularDamping);
        d->setMaxLinearVelocity(desc.maxLinearVelocity);
        d->setMaxAngularVelocity(desc.maxAngularVelocity);

        if (desc.motionQuality == BodyDesc::MotionQuality::LinearCast)
            d->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);

        if (!desc.allowSleeping)
            d->setWakeCounter(PX_MAX_F32);

        if (desc.velocity[0] != 0.0f || desc.velocity[1] != 0.0f || desc.velocity[2] != 0.0f)
            d->setLinearVelocity(PxVec3(desc.velocity[0], desc.velocity[1], desc.velocity[2]));
        if (desc.angularVelocity[0] != 0.0f || desc.angularVelocity[1] != 0.0f || desc.angularVelocity[2] != 0.0f)
        {
            constexpr float deg2rad = 3.14159265358979f / 180.0f;
            d->setAngularVelocity(PxVec3(desc.angularVelocity[0] * deg2rad,
                                         desc.angularVelocity[1] * deg2rad,
                                         desc.angularVelocity[2] * deg2rad));
        }

        actor = d;
    }

    shape->release();

    if (!actor) return InvalidBody;

    m_scene->addActor(*actor);

    const BodyHandle handle = m_nextHandle++;
    m_handleToActor[handle]        = actor;
    m_actorToHandle[actor]         = handle;
    m_entityToActor[desc.entityId] = actor;
    m_actorToEntity[actor]         = desc.entityId;

    return handle;
}

void PhysXBackend::removeBody(BodyHandle handle)
{
    auto it = m_handleToActor.find(handle);
    if (it == m_handleToActor.end()) return;

    PxRigidActor* actor = it->second;
    if (m_scene) m_scene->removeActor(*actor);

    auto eit = m_actorToEntity.find(actor);
    if (eit != m_actorToEntity.end())
    {
        m_entityToActor.erase(eit->second);
        m_actorToEntity.erase(eit);
    }
    m_actorToHandle.erase(actor);
    m_handleToActor.erase(it);

    actor->release();
}

void PhysXBackend::removeAllBodies()
{
    if (m_scene)
    {
        for (auto& [handle, actor] : m_handleToActor)
        {
            m_scene->removeActor(*actor);
            actor->release();
        }
    }
    m_handleToActor.clear();
    m_actorToHandle.clear();
    m_entityToActor.clear();
    m_actorToEntity.clear();
    m_nextHandle = 1;
}

void PhysXBackend::setBodyPositionRotation(BodyHandle handle,
                                           float px, float py, float pz,
                                           float eulerX, float eulerY, float eulerZ)
{
    auto it = m_handleToActor.find(handle);
    if (it == m_handleToActor.end()) return;

    float qx, qy, qz, qw;
    eulerToQuat(eulerX, eulerY, eulerZ, qx, qy, qz, qw);
    PxTransform pose(PxVec3(px, py, pz), PxQuat(qx, qy, qz, qw));

    PxRigidDynamic* dyn = it->second->is<PxRigidDynamic>();
    if (dyn && (dyn->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC))
        dyn->setKinematicTarget(pose);
    else
        it->second->setGlobalPose(pose);
}

IPhysicsBackend::BodyState PhysXBackend::getBodyState(BodyHandle handle) const
{
    BodyState state{};
    auto it = m_handleToActor.find(handle);
    if (it == m_handleToActor.end()) return state;

    PxTransform pose = it->second->getGlobalPose();
    state.position[0] = pose.p.x;
    state.position[1] = pose.p.y;
    state.position[2] = pose.p.z;

    quatToEuler(pose.q.x, pose.q.y, pose.q.z, pose.q.w,
                state.rotationEulerDeg[0], state.rotationEulerDeg[1], state.rotationEulerDeg[2]);

    const PxRigidDynamic* dyn = it->second->is<PxRigidDynamic>();
    if (dyn)
    {
        PxVec3 lv = dyn->getLinearVelocity();
        state.velocity[0] = lv.x; state.velocity[1] = lv.y; state.velocity[2] = lv.z;

        constexpr float rad2deg = 180.0f / 3.14159265358979f;
        PxVec3 av = dyn->getAngularVelocity();
        state.angularVelocityDeg[0] = av.x * rad2deg;
        state.angularVelocityDeg[1] = av.y * rad2deg;
        state.angularVelocityDeg[2] = av.z * rad2deg;
    }

    return state;
}

void PhysXBackend::setLinearVelocity(BodyHandle handle, float vx, float vy, float vz)
{
    auto it = m_handleToActor.find(handle);
    if (it == m_handleToActor.end()) return;
    PxRigidDynamic* dyn = it->second->is<PxRigidDynamic>();
    if (dyn) dyn->setLinearVelocity(PxVec3(vx, vy, vz));
}

void PhysXBackend::setAngularVelocity(BodyHandle handle, float vx, float vy, float vz)
{
    auto it = m_handleToActor.find(handle);
    if (it == m_handleToActor.end()) return;
    PxRigidDynamic* dyn = it->second->is<PxRigidDynamic>();
    if (dyn)
    {
        constexpr float deg2rad = 3.14159265358979f / 180.0f;
        dyn->setAngularVelocity(PxVec3(vx * deg2rad, vy * deg2rad, vz * deg2rad));
    }
}

// ── Collision events ─────────────────────────────────────────────────────

std::vector<IPhysicsBackend::CollisionEventData> PhysXBackend::drainCollisionEvents()
{
    std::lock_guard<std::mutex> lock(m_collisionMutex);
    std::vector<CollisionEventData> out;
    out.swap(m_pendingCollisions);
    return out;
}

// ── Raycast ──────────────────────────────────────────────────────────────

IPhysicsBackend::RaycastResult PhysXBackend::raycast(float ox, float oy, float oz,
                                                     float dx, float dy, float dz,
                                                     float maxDist) const
{
    RaycastResult result{};
    if (!m_scene) return result;

    PxVec3 origin(ox, oy, oz);
    PxVec3 dir(dx, dy, dz);
    dir.normalize();

    PxRaycastBuffer hit;
    if (m_scene->raycast(origin, dir, maxDist, hit) && hit.hasBlock)
    {
        result.hit      = true;
        result.distance = hit.block.distance;
        result.point[0] = hit.block.position.x;
        result.point[1] = hit.block.position.y;
        result.point[2] = hit.block.position.z;
        result.normal[0] = hit.block.normal.x;
        result.normal[1] = hit.block.normal.y;
        result.normal[2] = hit.block.normal.z;

        PxRigidActor* actor = hit.block.actor;
        if (actor)
        {
            auto it = m_actorToEntity.find(actor);
            if (it != m_actorToEntity.end())
                result.entity = it->second;
        }
    }
    return result;
}

// ── Sleep query ──────────────────────────────────────────────────────────

bool PhysXBackend::isBodySleeping(BodyHandle handle) const
{
    auto it = m_handleToActor.find(handle);
    if (it == m_handleToActor.end()) return true;
    const PxRigidDynamic* dyn = it->second->is<PxRigidDynamic>();
    return dyn ? dyn->isSleeping() : true;
}

IPhysicsBackend::BodyHandle PhysXBackend::getBodyForEntity(uint32_t entity) const
{
    auto it = m_entityToActor.find(entity);
    if (it == m_entityToActor.end()) return InvalidBody;
    auto hit = m_actorToHandle.find(it->second);
    return (hit != m_actorToHandle.end()) ? hit->second : InvalidBody;
}
