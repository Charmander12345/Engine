#include "JoltBackend.h"

// Jolt.h must be included before any other Jolt header.
#include <Jolt/Jolt.h>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/Body.h>

#include <cmath>
#include <algorithm>
#include <cstring>
#include <thread>
#include <mutex>

#include "../Logger/Logger.h"

// Suppress Jolt warnings
JPH_SUPPRESS_WARNINGS

// ── Object / broadphase layer constants ────────────────────────────────

namespace Layers
{
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING     = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}

namespace BPLayers
{
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint NUM_LAYERS = 2;
}

// ── Layer interface implementations ────────────────────────────────────

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
    BPLayerInterfaceImpl()
    {
        m_map[Layers::NON_MOVING] = BPLayers::NON_MOVING;
        m_map[Layers::MOVING]     = BPLayers::MOVING;
    }

    JPH::uint GetNumBroadPhaseLayers() const override { return BPLayers::NUM_LAYERS; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return m_map[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
    {
        switch ((JPH::BroadPhaseLayer::Type)inLayer)
        {
        case (JPH::BroadPhaseLayer::Type)BPLayers::NON_MOVING: return "NON_MOVING";
        case (JPH::BroadPhaseLayer::Type)BPLayers::MOVING:     return "MOVING";
        default: return "INVALID";
        }
    }
#endif

private:
    JPH::BroadPhaseLayer m_map[Layers::NUM_LAYERS];
};

class ObjVsBPLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
    {
        switch (inLayer1)
        {
        case Layers::NON_MOVING: return inLayer2 == BPLayers::MOVING;
        case Layers::MOVING:     return true;
        default:                 return false;
        }
    }
};

class ObjLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
    {
        switch (inObject1)
        {
        case Layers::NON_MOVING: return inObject2 == Layers::MOVING;
        case Layers::MOVING:     return true;
        default:                 return false;
        }
    }
};

// ── Contact listener that collects collision events ────────────────────

class EngineContactListener : public JPH::ContactListener
{
public:
    void SetMappings(const std::map<uint32_t, uint32_t>* bodyIdToEntity)
    {
        m_bodyIDToEntity = bodyIdToEntity;
    }

    JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2,
                                     JPH::RVec3Arg /*inBaseOffset*/,
                                     const JPH::CollideShapeResult& /*inCollisionResult*/) override
    {
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
                        const JPH::ContactManifold& inManifold,
                        JPH::ContactSettings& /*ioSettings*/) override
    {
        RecordContact(inBody1, inBody2, inManifold);
    }

    void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
                            const JPH::ContactManifold& inManifold,
                            JPH::ContactSettings& /*ioSettings*/) override
    {
        RecordContact(inBody1, inBody2, inManifold);
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& /*inSubShapePair*/) override
    {
    }

    std::vector<IPhysicsBackend::CollisionEventData> DrainEvents()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<IPhysicsBackend::CollisionEventData> out;
        out.swap(m_events);
        return out;
    }

private:
    void RecordContact(const JPH::Body& inBody1, const JPH::Body& inBody2,
                       const JPH::ContactManifold& inManifold)
    {
        if (!m_bodyIDToEntity) return;

        uint32_t rawA = inBody1.GetID().GetIndexAndSequenceNumber();
        uint32_t rawB = inBody2.GetID().GetIndexAndSequenceNumber();

        std::lock_guard<std::mutex> lock(m_mutex);

        auto itA = m_bodyIDToEntity->find(rawA);
        auto itB = m_bodyIDToEntity->find(rawB);
        if (itA == m_bodyIDToEntity->end() || itB == m_bodyIDToEntity->end())
            return;

        IPhysicsBackend::CollisionEventData ev{};
        ev.entityA = itA->second;
        ev.entityB = itB->second;

        JPH::Vec3 n = inManifold.mWorldSpaceNormal;
        ev.normal[0] = n.GetX();
        ev.normal[1] = n.GetY();
        ev.normal[2] = n.GetZ();
        ev.depth     = inManifold.mPenetrationDepth;

        if (inManifold.mRelativeContactPointsOn1.size() > 0)
        {
            JPH::RVec3 cp = inManifold.GetWorldSpaceContactPointOn1(0);
            ev.contactPoint[0] = static_cast<float>(cp.GetX());
            ev.contactPoint[1] = static_cast<float>(cp.GetY());
            ev.contactPoint[2] = static_cast<float>(cp.GetZ());
        }

        m_events.push_back(ev);
    }

    const std::map<uint32_t, uint32_t>* m_bodyIDToEntity{ nullptr };
    std::mutex m_mutex;
    std::vector<IPhysicsBackend::CollisionEventData> m_events;
};

// ── Euler <-> Quat helpers ──────────────────────────────────────────────

static JPH::Quat eulerDegreesToQuat(float pitch, float yaw, float roll)
{
    constexpr float deg2rad = 3.14159265358979f / 180.0f;
    float p = pitch * deg2rad;
    float y = yaw   * deg2rad;
    float r = roll  * deg2rad;
    // Rotation order: Y(yaw) * X(pitch) * Z(roll)
    return JPH::Quat::sRotation(JPH::Vec3::sAxisY(), y)
         * JPH::Quat::sRotation(JPH::Vec3::sAxisX(), p)
         * JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), r);
}

static void quatToEulerDegrees(JPH::Quat q, float& pitch, float& yaw, float& roll)
{
    constexpr float rad2deg = 180.0f / 3.14159265358979f;

    JPH::Mat44 m = JPH::Mat44::sRotation(q);
    float sp = -m(1, 2);
    sp = std::clamp(sp, -1.0f, 1.0f);
    pitch = std::asin(sp) * rad2deg;

    if (std::abs(sp) < 0.999999f)
    {
        yaw  = std::atan2(m(0, 2), m(2, 2)) * rad2deg;
        roll = std::atan2(m(1, 0), m(1, 1)) * rad2deg;
    }
    else
    {
        yaw  = std::atan2(-m(2, 0), m(0, 0)) * rad2deg;
        roll = 0.0f;
    }
}

// ── Constructor / Destructor ────────────────────────────────────────────

JoltBackend::JoltBackend() = default;

JoltBackend::~JoltBackend()
{
    if (m_initialized)
        shutdown();
}

// ── Lifecycle ───────────────────────────────────────────────────────────

void JoltBackend::initialize()
{
    if (m_initialized)
        shutdown();

    JPH::RegisterDefaultAllocator();

    if (!JPH::Factory::sInstance)
        JPH::Factory::sInstance = new JPH::Factory();

    JPH::RegisterTypes();

    m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);

    int numThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, numThreads);

    m_broadPhaseLayerInterface  = std::make_unique<BPLayerInterfaceImpl>();
    m_objectVsBroadPhaseFilter  = std::make_unique<ObjVsBPLayerFilterImpl>();
    m_objectLayerPairFilter     = std::make_unique<ObjLayerPairFilterImpl>();

    const JPH::uint cMaxBodies             = 8192;
    const JPH::uint cNumBodyMutexes        = 0;
    const JPH::uint cMaxBodyPairs          = 8192;
    const JPH::uint cMaxContactConstraints = 4096;

    m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    m_physicsSystem->Init(
        cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
        *m_broadPhaseLayerInterface,
        *m_objectVsBroadPhaseFilter,
        *m_objectLayerPairFilter);

    m_physicsSystem->SetGravity(JPH::Vec3(m_gravity[0], m_gravity[1], m_gravity[2]));

    auto listener = std::make_unique<EngineContactListener>();
    listener->SetMappings(&m_bodyIDToEntity);
    m_physicsSystem->SetContactListener(listener.get());
    m_contactListener = std::move(listener);

    m_entityToBodyID.clear();
    m_bodyIDToEntity.clear();

    m_initialized = true;
    Logger::Instance().log(Logger::Category::Engine, "JoltBackend initialised (Jolt Physics).", Logger::LogLevel::INFO);
}

void JoltBackend::shutdown()
{
    removeAllBodies();

    if (m_physicsSystem)
        m_physicsSystem->SetContactListener(nullptr);

    m_contactListener.reset();
    m_physicsSystem.reset();
    m_jobSystem.reset();
    m_tempAllocator.reset();
    m_broadPhaseLayerInterface.reset();
    m_objectVsBroadPhaseFilter.reset();
    m_objectLayerPairFilter.reset();

    if (JPH::Factory::sInstance)
    {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

    m_initialized = false;
}

void JoltBackend::update(float fixedDt)
{
    if (!m_initialized || !m_physicsSystem) return;
    m_physicsSystem->Update(fixedDt, 1, m_tempAllocator.get(), m_jobSystem.get());
}

// ── Gravity ─────────────────────────────────────────────────────────────

void JoltBackend::setGravity(float x, float y, float z)
{
    m_gravity[0] = x;
    m_gravity[1] = y;
    m_gravity[2] = z;
    if (m_physicsSystem)
        m_physicsSystem->SetGravity(JPH::Vec3(x, y, z));
}

void JoltBackend::getGravity(float& x, float& y, float& z) const
{
    x = m_gravity[0];
    y = m_gravity[1];
    z = m_gravity[2];
}

// ── Body management ─────────────────────────────────────────────────────

IPhysicsBackend::BodyHandle JoltBackend::createBody(const BodyDesc& desc)
{
    if (!m_physicsSystem) return InvalidBody;

    JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();

    // Determine Jolt motion type and layer
    JPH::EMotionType motionType = JPH::EMotionType::Static;
    JPH::ObjectLayer layer      = Layers::NON_MOVING;
    switch (desc.motionType)
    {
    case BodyDesc::MotionType::Dynamic:
        motionType = JPH::EMotionType::Dynamic;
        layer      = Layers::MOVING;
        break;
    case BodyDesc::MotionType::Kinematic:
        motionType = JPH::EMotionType::Kinematic;
        layer      = Layers::MOVING;
        break;
    default:
        break;
    }

    // Build shape
    JPH::RefConst<JPH::Shape> shape;
    switch (desc.shape)
    {
    case BodyDesc::Shape::Sphere:
    {
        float r = desc.radius;
        if (r < 0.001f) r = 0.001f;
        shape = new JPH::SphereShape(r);
        break;
    }
    case BodyDesc::Shape::Capsule:
    {
        float r = desc.radius;
        float hh = desc.halfHeight;
        if (r < 0.001f) r = 0.001f;
        if (hh < 0.001f) hh = 0.001f;
        shape = new JPH::CapsuleShape(hh, r);
        break;
    }
    case BodyDesc::Shape::Cylinder:
    {
        float r = desc.radius;
        float hh = desc.halfHeight;
        if (r < 0.001f) r = 0.001f;
        if (hh < 0.001f) hh = 0.001f;
        shape = new JPH::CylinderShape(hh, r);
        break;
    }
    case BodyDesc::Shape::HeightField:
    {
        if (desc.heightData && desc.heightSampleCount > 1)
        {
            int sampleCount = desc.heightSampleCount;
            const float* heightData = desc.heightData;
            float scaleX = desc.heightScale[0];
            float scaleZ = desc.heightScale[2];

            // Jolt requires sampleCount to be (2^n + 1).
            // If it doesn't satisfy this, resample to the next valid count.
            std::vector<float> resampledData;
            const int sc_m1 = sampleCount - 1;
            const bool isPow2Plus1 = (sc_m1 >= 2) && ((sc_m1 & (sc_m1 - 1)) == 0);

            if (!isPow2Plus1)
            {
                // Find the next power of 2 >= (sampleCount - 1)
                int nextPow2 = 2;
                while (nextPow2 < sc_m1) nextPow2 *= 2;
                const int validCount = nextPow2 + 1;

                // Bilinear resample from old grid to new grid
                resampledData.resize(static_cast<size_t>(validCount) * validCount);
                for (int r = 0; r < validCount; ++r)
                {
                    const float srcR = static_cast<float>(r) / static_cast<float>(validCount - 1)
                                     * static_cast<float>(sampleCount - 1);
                    const int r0 = std::min(static_cast<int>(srcR), sampleCount - 2);
                    const int r1 = r0 + 1;
                    const float fr = srcR - static_cast<float>(r0);

                    for (int c = 0; c < validCount; ++c)
                    {
                        const float srcC = static_cast<float>(c) / static_cast<float>(validCount - 1)
                                         * static_cast<float>(sampleCount - 1);
                        const int c0 = std::min(static_cast<int>(srcC), sampleCount - 2);
                        const int c1 = c0 + 1;
                        const float fc = srcC - static_cast<float>(c0);

                        const float h00 = heightData[r0 * sampleCount + c0];
                        const float h10 = heightData[r0 * sampleCount + c1];
                        const float h01 = heightData[r1 * sampleCount + c0];
                        const float h11 = heightData[r1 * sampleCount + c1];

                        resampledData[r * validCount + c] =
                            h00 * (1.0f - fc) * (1.0f - fr) + h10 * fc * (1.0f - fr) +
                            h01 * (1.0f - fc) * fr           + h11 * fc * fr;
                    }
                }

                // Adjust scale so the resampled grid covers the same physical area
                const float ratio = static_cast<float>(sampleCount - 1)
                                  / static_cast<float>(validCount - 1);
                scaleX *= ratio;
                scaleZ *= ratio;

                heightData = resampledData.data();
                sampleCount = validCount;
            }

            JPH::HeightFieldShapeSettings hfSettings(
                heightData,
                JPH::Vec3(desc.heightOffset[0], desc.heightOffset[1], desc.heightOffset[2]),
                JPH::Vec3(scaleX, desc.heightScale[1], scaleZ),
                sampleCount
            );
            JPH::ShapeSettings::ShapeResult result = hfSettings.Create();
            if (result.HasError())
            {
                Logger::Instance().log(Logger::Category::Engine,
                    "Failed to create HeightFieldShape: " + std::string(result.GetError().c_str()),
                    Logger::LogLevel::ERROR);
                shape = new JPH::BoxShape(JPH::Vec3(1.0f, 1.0f, 1.0f));
            }
            else
            {
                shape = result.Get();
            }
        }
        else
        {
            shape = new JPH::BoxShape(JPH::Vec3(1.0f, 1.0f, 1.0f));
        }
        break;
    }
    default: // Box
    {
        float hx = desc.halfExtents[0];
        float hy = desc.halfExtents[1];
        float hz = desc.halfExtents[2];
        if (hx < 0.001f) hx = 0.001f;
        if (hy < 0.001f) hy = 0.001f;
        if (hz < 0.001f) hz = 0.001f;
        shape = new JPH::BoxShape(JPH::Vec3(hx, hy, hz));
        break;
    }
    }

    // Apply collider offset if non-zero
    bool hasOffset = (desc.colliderOffset[0] != 0.0f || desc.colliderOffset[1] != 0.0f || desc.colliderOffset[2] != 0.0f);
    if (hasOffset)
    {
        shape = new JPH::OffsetCenterOfMassShape(shape,
            JPH::Vec3(desc.colliderOffset[0], desc.colliderOffset[1], desc.colliderOffset[2]));
    }

    JPH::Quat rotation = eulerDegreesToQuat(desc.rotationEulerDeg[0], desc.rotationEulerDeg[1], desc.rotationEulerDeg[2]);
    JPH::RVec3 position(desc.position[0], desc.position[1], desc.position[2]);

    JPH::BodyCreationSettings settings(shape, position, rotation, motionType, layer);
    settings.mRestitution = desc.restitution;
    settings.mFriction    = desc.friction;
    settings.mIsSensor    = desc.isSensor;

    if (desc.motionType != BodyDesc::MotionType::Static)
    {
        if (motionType == JPH::EMotionType::Dynamic && desc.mass > 0.0f)
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = desc.mass;
        }
        settings.mGravityFactor      = desc.gravityFactor;
        settings.mLinearDamping      = desc.linearDamping;
        settings.mAngularDamping     = desc.angularDamping;
        settings.mMaxLinearVelocity  = desc.maxLinearVelocity;
        settings.mMaxAngularVelocity = desc.maxAngularVelocity;
        settings.mAllowSleeping      = desc.allowSleeping;
        settings.mMotionQuality      = (desc.motionQuality == BodyDesc::MotionQuality::LinearCast)
            ? JPH::EMotionQuality::LinearCast
            : JPH::EMotionQuality::Discrete;
    }

    JPH::Body* body = bi.CreateBody(settings);
    if (!body) return InvalidBody;

    JPH::BodyID id = body->GetID();
    bi.AddBody(id, motionType == JPH::EMotionType::Static
        ? JPH::EActivation::DontActivate
        : JPH::EActivation::Activate);

    // Set initial velocity for dynamic bodies
    if (desc.motionType == BodyDesc::MotionType::Dynamic)
    {
        float vLen = desc.velocity[0]*desc.velocity[0] + desc.velocity[1]*desc.velocity[1] + desc.velocity[2]*desc.velocity[2];
        if (vLen > 1e-8f)
            bi.SetLinearVelocity(id, JPH::Vec3(desc.velocity[0], desc.velocity[1], desc.velocity[2]));

        float avLen = desc.angularVelocity[0]*desc.angularVelocity[0] + desc.angularVelocity[1]*desc.angularVelocity[1] + desc.angularVelocity[2]*desc.angularVelocity[2];
        if (avLen > 1e-8f)
        {
            constexpr float deg2rad = 3.14159265358979f / 180.0f;
            bi.SetAngularVelocity(id, JPH::Vec3(
                desc.angularVelocity[0] * deg2rad,
                desc.angularVelocity[1] * deg2rad,
                desc.angularVelocity[2] * deg2rad));
        }
    }

    uint32_t rawId = id.GetIndexAndSequenceNumber();
    m_entityToBodyID[desc.entityId] = rawId;
    m_bodyIDToEntity[rawId]         = desc.entityId;

    return static_cast<BodyHandle>(rawId);
}

void JoltBackend::removeBody(BodyHandle handle)
{
    if (!m_physicsSystem) return;

    uint32_t rawId = static_cast<uint32_t>(handle);
    JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    JPH::BodyID id(rawId);
    bi.RemoveBody(id);
    bi.DestroyBody(id);

    auto itEntity = m_bodyIDToEntity.find(rawId);
    if (itEntity != m_bodyIDToEntity.end())
    {
        m_entityToBodyID.erase(itEntity->second);
        m_bodyIDToEntity.erase(itEntity);
    }
}

void JoltBackend::removeAllBodies()
{
    if (!m_physicsSystem) return;

    JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    for (auto& [entity, rawId] : m_entityToBodyID)
    {
        JPH::BodyID id(rawId);
        bi.RemoveBody(id);
        bi.DestroyBody(id);
    }
    m_entityToBodyID.clear();
    m_bodyIDToEntity.clear();
}

void JoltBackend::setBodyPositionRotation(BodyHandle handle,
                                           float px, float py, float pz,
                                           float eulerX, float eulerY, float eulerZ)
{
    if (!m_physicsSystem) return;

    JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    JPH::BodyID id(static_cast<uint32_t>(handle));
    JPH::Quat rotation = eulerDegreesToQuat(eulerX, eulerY, eulerZ);
    bi.SetPositionAndRotation(id, JPH::RVec3(px, py, pz), rotation, JPH::EActivation::DontActivate);
}

IPhysicsBackend::BodyState JoltBackend::getBodyState(BodyHandle handle) const
{
    BodyState state{};
    if (!m_physicsSystem) return state;

    JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    JPH::BodyID id(static_cast<uint32_t>(handle));

    JPH::RVec3 pos = bi.GetCenterOfMassPosition(id);
    state.position[0] = static_cast<float>(pos.GetX());
    state.position[1] = static_cast<float>(pos.GetY());
    state.position[2] = static_cast<float>(pos.GetZ());

    JPH::Quat rot = bi.GetRotation(id);
    quatToEulerDegrees(rot, state.rotationEulerDeg[0], state.rotationEulerDeg[1], state.rotationEulerDeg[2]);

    constexpr float rad2deg = 180.0f / 3.14159265358979f;

    JPH::Vec3 vel = bi.GetLinearVelocity(id);
    state.velocity[0] = vel.GetX();
    state.velocity[1] = vel.GetY();
    state.velocity[2] = vel.GetZ();

    JPH::Vec3 angVel = bi.GetAngularVelocity(id);
    state.angularVelocityDeg[0] = angVel.GetX() * rad2deg;
    state.angularVelocityDeg[1] = angVel.GetY() * rad2deg;
    state.angularVelocityDeg[2] = angVel.GetZ() * rad2deg;

    return state;
}

void JoltBackend::setLinearVelocity(BodyHandle handle, float vx, float vy, float vz)
{
    if (!m_physicsSystem) return;
    JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    bi.SetLinearVelocity(JPH::BodyID(static_cast<uint32_t>(handle)), JPH::Vec3(vx, vy, vz));
}

void JoltBackend::setAngularVelocity(BodyHandle handle, float vx, float vy, float vz)
{
    if (!m_physicsSystem) return;
    constexpr float deg2rad = 3.14159265358979f / 180.0f;
    JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    bi.SetAngularVelocity(JPH::BodyID(static_cast<uint32_t>(handle)),
        JPH::Vec3(vx * deg2rad, vy * deg2rad, vz * deg2rad));
}

// ── Collision events ────────────────────────────────────────────────────

std::vector<IPhysicsBackend::CollisionEventData> JoltBackend::drainCollisionEvents()
{
    auto* listener = static_cast<EngineContactListener*>(m_contactListener.get());
    if (listener)
        return listener->DrainEvents();
    return {};
}

// ── Queries ─────────────────────────────────────────────────────────────

IPhysicsBackend::RaycastResult JoltBackend::raycast(float ox, float oy, float oz,
                                                     float dx, float dy, float dz,
                                                     float maxDist) const
{
    RaycastResult result{};
    if (!m_physicsSystem) return result;

    float dirLen = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dirLen < 1e-8f) return result;
    dx /= dirLen;
    dy /= dirLen;
    dz /= dirLen;

    JPH::RRayCast ray(JPH::RVec3(ox, oy, oz), JPH::Vec3(dx * maxDist, dy * maxDist, dz * maxDist));

    JPH::RayCastResult hit;
    bool didHit = m_physicsSystem->GetNarrowPhaseQuery().CastRay(ray, hit);

    if (didHit)
    {
        result.hit = true;
        result.distance = hit.mFraction * maxDist;
        result.point[0] = ox + dx * result.distance;
        result.point[1] = oy + dy * result.distance;
        result.point[2] = oz + dz * result.distance;

        uint32_t rawId = hit.mBodyID.GetIndexAndSequenceNumber();
        auto it = m_bodyIDToEntity.find(rawId);
        if (it != m_bodyIDToEntity.end())
            result.entity = it->second;

        JPH::BodyLockRead lock(m_physicsSystem->GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded())
        {
            const JPH::Body& body = lock.GetBody();
            JPH::Vec3 normal = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2,
                JPH::RVec3(result.point[0], result.point[1], result.point[2]));
            result.normal[0] = normal.GetX();
            result.normal[1] = normal.GetY();
            result.normal[2] = normal.GetZ();
        }
    }

    return result;
}

bool JoltBackend::isBodySleeping(BodyHandle handle) const
{
    if (!m_physicsSystem) return false;

    const JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    JPH::BodyID id(static_cast<uint32_t>(handle));
    return !bi.IsActive(id);
}

IPhysicsBackend::BodyHandle JoltBackend::getBodyForEntity(uint32_t entity) const
{
    auto it = m_entityToBodyID.find(entity);
    if (it != m_entityToBodyID.end())
        return static_cast<BodyHandle>(it->second);
    return InvalidBody;
}
