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
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <cmath>
#include <algorithm>
#include <cstring>
#include <thread>
#include <mutex>
#include <array>
#include <unordered_set>

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

namespace
{
    constexpr float cShapeMinExtent = 0.001f;
    constexpr float cBoxLikeTolerance = 1.0e-4f;

    bool isNear(float a, float b, float tolerance)
    {
        return std::abs(a - b) <= tolerance;
    }

    bool tryBuildBoxShapeFromMesh(const JoltBackend::BodyDesc& desc, JPH::RefConst<JPH::Shape>& outShape)
    {
        constexpr int cMeshVertexStrideFloats = 5;
        if (desc.meshVertices == nullptr || desc.meshVertexFloatCount < (8 * cMeshVertexStrideFloats)
            || (desc.meshVertexFloatCount % cMeshVertexStrideFloats) != 0)
        {
            return false;
        }

        const int vertexCount = desc.meshVertexFloatCount / cMeshVertexStrideFloats;
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        float maxZ = std::numeric_limits<float>::lowest();

        for (int i = 0; i < vertexCount; ++i)
        {
            const int base = i * cMeshVertexStrideFloats;
            const float x = desc.meshVertices[base + 0] * desc.meshScale[0];
            const float y = desc.meshVertices[base + 1] * desc.meshScale[1];
            const float z = desc.meshVertices[base + 2] * desc.meshScale[2];
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
            minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
        }

        const float hx = std::max((maxX - minX) * 0.5f, cShapeMinExtent);
        const float hy = std::max((maxY - minY) * 0.5f, cShapeMinExtent);
        const float hz = std::max((maxZ - minZ) * 0.5f, cShapeMinExtent);
        const float cx = (minX + maxX) * 0.5f;
        const float cy = (minY + maxY) * 0.5f;
        const float cz = (minZ + maxZ) * 0.5f;

        for (int i = 0; i < vertexCount; ++i)
        {
            const int base = i * cMeshVertexStrideFloats;
            const float x = desc.meshVertices[base + 0] * desc.meshScale[0];
            const float y = desc.meshVertices[base + 1] * desc.meshScale[1];
            const float z = desc.meshVertices[base + 2] * desc.meshScale[2];

            if ((!isNear(x, minX, cBoxLikeTolerance) && !isNear(x, maxX, cBoxLikeTolerance))
                || (!isNear(y, minY, cBoxLikeTolerance) && !isNear(y, maxY, cBoxLikeTolerance))
                || (!isNear(z, minZ, cBoxLikeTolerance) && !isNear(z, maxZ, cBoxLikeTolerance)))
            {
                return false;
            }
        }

        outShape = new JPH::BoxShape(JPH::Vec3(hx, hy, hz));
        if (!isNear(cx, 0.0f, cBoxLikeTolerance) || !isNear(cy, 0.0f, cBoxLikeTolerance) || !isNear(cz, 0.0f, cBoxLikeTolerance))
        {
            outShape = new JPH::OffsetCenterOfMassShape(outShape, JPH::Vec3(cx, cy, cz));
        }

        return true;
    }

    bool tryBuildBoundsBoxShapeFromMesh(const JoltBackend::BodyDesc& desc, JPH::RefConst<JPH::Shape>& outShape)
    {
        constexpr int cMeshVertexStrideFloats = 5;
        if (desc.meshVertices == nullptr || desc.meshVertexFloatCount < (3 * cMeshVertexStrideFloats)
            || (desc.meshVertexFloatCount % cMeshVertexStrideFloats) != 0)
        {
            return false;
        }

        const int vertexCount = desc.meshVertexFloatCount / cMeshVertexStrideFloats;
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float minZ = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        float maxZ = std::numeric_limits<float>::lowest();

        for (int i = 0; i < vertexCount; ++i)
        {
            const int base = i * cMeshVertexStrideFloats;
            const float x = desc.meshVertices[base + 0] * desc.meshScale[0];
            const float y = desc.meshVertices[base + 1] * desc.meshScale[1];
            const float z = desc.meshVertices[base + 2] * desc.meshScale[2];
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
            minZ = std::min(minZ, z); maxZ = std::max(maxZ, z);
        }

        const float hx = std::max((maxX - minX) * 0.5f, cShapeMinExtent);
        const float hy = std::max((maxY - minY) * 0.5f, cShapeMinExtent);
        const float hz = std::max((maxZ - minZ) * 0.5f, cShapeMinExtent);
        const float cx = (minX + maxX) * 0.5f;
        const float cy = (minY + maxY) * 0.5f;
        const float cz = (minZ + maxZ) * 0.5f;

        outShape = new JPH::BoxShape(JPH::Vec3(hx, hy, hz));
        if (!isNear(cx, 0.0f, cBoxLikeTolerance) || !isNear(cy, 0.0f, cBoxLikeTolerance) || !isNear(cz, 0.0f, cBoxLikeTolerance))
            outShape = new JPH::OffsetCenterOfMassShape(outShape, JPH::Vec3(cx, cy, cz));

        return true;
    }
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

    std::vector<PhysicsTypes::CollisionEventData> DrainEvents()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<PhysicsTypes::CollisionEventData> out;
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

        PhysicsTypes::CollisionEventData ev{};
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
    std::vector<PhysicsTypes::CollisionEventData> m_events;
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

    // Tune solver for better penetration recovery and more stable convex-convex
    // contacts. Jolt's PhysicsSettings documentation notes that
    // mMaxPenetrationDistance caps how much overlap can be corrected in a
    // single position iteration, while mNumVelocitySteps / mNumPositionSteps
    // control how much contact resolution work is done per step.
    JPH::PhysicsSettings ps = m_physicsSystem->GetPhysicsSettings();
    ps.mBaumgarte                  = 0.35f;  // default 0.2  — faster penetration recovery
    ps.mNumVelocitySteps           = 12;     // default 10   — stronger contact/friction solving
    ps.mNumPositionSteps           = 8;      // default 2    — more position correction passes
    ps.mMaxPenetrationDistance     = 1.0f;   // default 0.2  — allow deeper overlap recovery in one frame
    ps.mSpeculativeContactDistance = 0.05f;  // default 0.02 — wider contact buffer for edge stability
    m_physicsSystem->SetPhysicsSettings(ps);

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
    removeAllCharacters();
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
    m_physicsSystem->Update(fixedDt, 4, m_tempAllocator.get(), m_jobSystem.get());
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

JoltBackend::BodyHandle JoltBackend::createBody(const BodyDesc& desc)
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
    case BodyDesc::Shape::Mesh:
    {
        constexpr int cMeshVertexStrideFloats = 5;
        const bool hasVertices = desc.meshVertices != nullptr && desc.meshVertexFloatCount >= (3 * cMeshVertexStrideFloats)
            && (desc.meshVertexFloatCount % cMeshVertexStrideFloats) == 0;

        if (hasVertices)
        {
            const int vertexCount = desc.meshVertexFloatCount / cMeshVertexStrideFloats;

            if (desc.meshTreatAsConvex && tryBuildBoxShapeFromMesh(desc, shape))
                break;

            if (desc.motionType == BodyDesc::MotionType::Static && !desc.meshTreatAsConvex)
            {
                JPH::TriangleList triangles;
                const int triangleCount = desc.meshIndexCount >= 3 ? (desc.meshIndexCount / 3) : (vertexCount / 3);
                triangles.reserve(triangleCount);

                auto readPoint = [&](int index) -> JPH::Vec3
                {
                    const int base = index * cMeshVertexStrideFloats;
                    return JPH::Vec3(
                        desc.meshVertices[base + 0] * desc.meshScale[0],
                        desc.meshVertices[base + 1] * desc.meshScale[1],
                        desc.meshVertices[base + 2] * desc.meshScale[2]);
                };

                if (desc.meshIndices && desc.meshIndexCount >= 3)
                {
                    for (int i = 0; i + 2 < desc.meshIndexCount; i += 3)
                    {
                        const uint32_t i0 = desc.meshIndices[i + 0];
                        const uint32_t i1 = desc.meshIndices[i + 1];
                        const uint32_t i2 = desc.meshIndices[i + 2];
                        if (i0 >= static_cast<uint32_t>(vertexCount) || i1 >= static_cast<uint32_t>(vertexCount) || i2 >= static_cast<uint32_t>(vertexCount))
                            continue;
                        triangles.emplace_back(readPoint(static_cast<int>(i0)), readPoint(static_cast<int>(i1)), readPoint(static_cast<int>(i2)));
                    }
                }
                else
                {
                    for (int i = 0; i + 2 < vertexCount; i += 3)
                        triangles.emplace_back(readPoint(i + 0), readPoint(i + 1), readPoint(i + 2));
                }

                JPH::MeshShapeSettings meshSettings(triangles);
                meshSettings.mBuildQuality = JPH::MeshShapeSettings::EBuildQuality::FavorRuntimePerformance;
                JPH::ShapeSettings::ShapeResult result = meshSettings.Create();
                if (!result.HasError())
                    shape = result.Get();
            }

            if (shape == nullptr)
            {
                JPH::Array<JPH::Vec3> points;
                points.reserve(vertexCount);
                for (int i = 0; i < vertexCount; ++i)
                {
                    const int base = i * cMeshVertexStrideFloats;
                    points.push_back(JPH::Vec3(
                        desc.meshVertices[base + 0] * desc.meshScale[0],
                        desc.meshVertices[base + 1] * desc.meshScale[1],
                        desc.meshVertices[base + 2] * desc.meshScale[2]));
                }

                JPH::ConvexHullShapeSettings hullSettings(points);
                hullSettings.mMaxConvexRadius = 0.0f;
                hullSettings.mHullTolerance = 1.0e-4f;
                JPH::ShapeSettings::ShapeResult result = hullSettings.Create();
                if (!result.HasError())
                    shape = result.Get();
            }
        }

        if (shape == nullptr)
        {
            if (!tryBuildBoundsBoxShapeFromMesh(desc, shape))
            {
            Logger::Instance().log(Logger::Category::Engine,
                "Mesh collision shape creation failed or mesh data was missing. Falling back to BoxShape.",
                Logger::LogLevel::WARNING);
                shape = new JPH::BoxShape(JPH::Vec3(1.0f, 1.0f, 1.0f));
            }
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
            settings.mNumVelocityStepsOverride = 12;
            settings.mNumPositionStepsOverride = 8;
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
        settings.mEnhancedInternalEdgeRemoval = true;
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

    // Resolve initial penetration for dynamic bodies.  A rotated box whose
    // corners start inside a heightfield (one-sided triangle surface) can
    // trap the iterative solver because back-face contacts produce inverted
    // normals.  Detect the overlap right after creation and shift the body
    // out before simulation begins.
    if (desc.motionType == BodyDesc::MotionType::Dynamic)
    {
        JPH::RMat44 comTransform = bi.GetCenterOfMassTransform(id);

        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> depCollector;

        m_physicsSystem->GetNarrowPhaseQuery().CollideShape(
            shape,
            JPH::Vec3::sReplicate(1.0f),
            comTransform,
            JPH::CollideShapeSettings(),
            JPH::RVec3::sZero(),
            depCollector);

        JPH::Vec3 correction = JPH::Vec3::sZero();
        for (const auto& hit : depCollector.mHits)
        {
            if (hit.mBodyID2 == id) continue;  // skip self

            JPH::Vec3 axis = hit.mPenetrationAxis;
            float len = axis.Length();
            if (len < 1.0e-6f) continue;

            // mPenetrationAxis = direction to move the *hit body* out of
            // collision.  We move our body in the opposite direction.
            JPH::Vec3 pushback = (-axis / len) * hit.mPenetrationDepth;
            if (pushback.LengthSq() > correction.LengthSq())
                correction = pushback;
        }

        if (correction.LengthSq() > 1.0e-6f)
        {
            correction *= 1.02f;  // tiny margin for clean separation
            JPH::RVec3 curPos = bi.GetPosition(id);
            bi.SetPosition(id, curPos + JPH::RVec3(correction), JPH::EActivation::Activate);
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
    bi.SetPositionAndRotation(id, JPH::RVec3(px, py, pz), rotation, JPH::EActivation::Activate);
}

PhysicsTypes::BodyState JoltBackend::getBodyState(BodyHandle handle) const
{
    BodyState state{};
    if (!m_physicsSystem) return state;

    JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    JPH::BodyID id(static_cast<uint32_t>(handle));

    JPH::RVec3 pos = bi.GetPosition(id);
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

void JoltBackend::addForce(BodyHandle handle, float fx, float fy, float fz)
{
    if (!m_physicsSystem) return;
    JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    JPH::BodyID id(static_cast<uint32_t>(handle));
    bi.AddForce(id, JPH::Vec3(fx, fy, fz));
}

void JoltBackend::addImpulse(BodyHandle handle, float ix, float iy, float iz)
{
    if (!m_physicsSystem) return;
    JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    JPH::BodyID id(static_cast<uint32_t>(handle));
    bi.AddImpulse(id, JPH::Vec3(ix, iy, iz));
}

// ── Collision events ────────────────────────────────────────────────────

std::vector<JoltBackend::CollisionEventData> JoltBackend::drainCollisionEvents()
{
    auto* listener = static_cast<EngineContactListener*>(m_contactListener.get());
    if (listener)
        return listener->DrainEvents();
    return {};
}

// ── Queries ─────────────────────────────────────────────────────────────

JoltBackend::RaycastResult JoltBackend::raycast(float ox, float oy, float oz,
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

// ── Overlap / Sweep queries ─────────────────────────────────────────────

std::vector<uint32_t> JoltBackend::overlapSphere(float cx, float cy, float cz,
                                                  float radius) const
{
    std::vector<uint32_t> result;
    if (!m_physicsSystem) return result;
    std::unordered_set<uint32_t> uniqueEntities;

    JPH::SphereShape sphere(radius);
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;

    m_physicsSystem->GetNarrowPhaseQuery().CollideShape(
        &sphere,
        JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(JPH::RVec3(cx, cy, cz)),
        JPH::CollideShapeSettings(),
        JPH::RVec3::sZero(),
        collector);

    for (size_t i = 0; i < collector.mHits.size(); ++i)
    {
        uint32_t rawId = collector.mHits[i].mBodyID2.GetIndexAndSequenceNumber();
        auto it = m_bodyIDToEntity.find(rawId);
        if (it != m_bodyIDToEntity.end() && uniqueEntities.insert(it->second).second)
            result.push_back(it->second);
    }
    return result;
}

std::vector<uint32_t> JoltBackend::overlapBox(float cx, float cy, float cz,
                                               float hx, float hy, float hz,
                                               float eulerX, float eulerY, float eulerZ) const
{
    std::vector<uint32_t> result;
    if (!m_physicsSystem) return result;
    std::unordered_set<uint32_t> uniqueEntities;

    JPH::BoxShape box(JPH::Vec3(hx, hy, hz));

    constexpr float deg2rad = 3.14159265358979f / 180.0f;
    JPH::Quat rot = JPH::Quat::sEulerAngles(JPH::Vec3(eulerX * deg2rad, eulerY * deg2rad, eulerZ * deg2rad));
    JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(rot, JPH::RVec3(cx, cy, cz));

    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;

    m_physicsSystem->GetNarrowPhaseQuery().CollideShape(
        &box,
        JPH::Vec3::sReplicate(1.0f),
        transform,
        JPH::CollideShapeSettings(),
        JPH::RVec3::sZero(),
        collector);

    for (size_t i = 0; i < collector.mHits.size(); ++i)
    {
        uint32_t rawId = collector.mHits[i].mBodyID2.GetIndexAndSequenceNumber();
        auto it = m_bodyIDToEntity.find(rawId);
        if (it != m_bodyIDToEntity.end() && uniqueEntities.insert(it->second).second)
            result.push_back(it->second);
    }
    return result;
}

JoltBackend::RaycastResult JoltBackend::sweepSphere(float ox, float oy, float oz,
                                                    float radius,
                                                    float dx, float dy, float dz,
                                                    float maxDist) const
{
    RaycastResult result{};
    if (!m_physicsSystem) return result;

    float dirLen = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dirLen < 1e-8f) return result;
    dx /= dirLen; dy /= dirLen; dz /= dirLen;

    JPH::SphereShape sphere(radius);
    JPH::RShapeCast shapeCast(
        &sphere,
        JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(JPH::RVec3(ox, oy, oz)),
        JPH::Vec3(dx * maxDist, dy * maxDist, dz * maxDist));

    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;

    m_physicsSystem->GetNarrowPhaseQuery().CastShape(
        shapeCast, settings, JPH::RVec3::sZero(), collector);

    if (collector.HadHit())
    {
        result.hit = true;
        result.distance = collector.mHit.mFraction * maxDist;
        result.point[0] = ox + dx * result.distance;
        result.point[1] = oy + dy * result.distance;
        result.point[2] = oz + dz * result.distance;

        uint32_t rawId = collector.mHit.mBodyID2.GetIndexAndSequenceNumber();
        auto it = m_bodyIDToEntity.find(rawId);
        if (it != m_bodyIDToEntity.end())
            result.entity = it->second;

        JPH::Vec3 normal = -collector.mHit.mPenetrationAxis.Normalized();
        result.normal[0] = normal.GetX();
        result.normal[1] = normal.GetY();
        result.normal[2] = normal.GetZ();
    }
    return result;
}

JoltBackend::RaycastResult JoltBackend::sweepBox(float ox, float oy, float oz,
                                                 float hx, float hy, float hz,
                                                 float dx, float dy, float dz,
                                                 float maxDist) const
{
    RaycastResult result{};
    if (!m_physicsSystem) return result;

    float dirLen = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dirLen < 1e-8f) return result;
    dx /= dirLen; dy /= dirLen; dz /= dirLen;

    JPH::BoxShape box(JPH::Vec3(hx, hy, hz));
    JPH::RShapeCast shapeCast(
        &box,
        JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(JPH::RVec3(ox, oy, oz)),
        JPH::Vec3(dx * maxDist, dy * maxDist, dz * maxDist));

    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;

    m_physicsSystem->GetNarrowPhaseQuery().CastShape(
        shapeCast, settings, JPH::RVec3::sZero(), collector);

    if (collector.HadHit())
    {
        result.hit = true;
        result.distance = collector.mHit.mFraction * maxDist;
        result.point[0] = ox + dx * result.distance;
        result.point[1] = oy + dy * result.distance;
        result.point[2] = oz + dz * result.distance;

        uint32_t rawId = collector.mHit.mBodyID2.GetIndexAndSequenceNumber();
        auto it = m_bodyIDToEntity.find(rawId);
        if (it != m_bodyIDToEntity.end())
            result.entity = it->second;

        JPH::Vec3 normal = -collector.mHit.mPenetrationAxis.Normalized();
        result.normal[0] = normal.GetX();
        result.normal[1] = normal.GetY();
        result.normal[2] = normal.GetZ();
    }
    return result;
}

// ── Force / impulse at position ─────────────────────────────────────────

void JoltBackend::addForceAtPosition(BodyHandle handle,
                                      float fx, float fy, float fz,
                                      float px, float py, float pz)
{
    if (!m_physicsSystem) return;
    JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    JPH::BodyID id(static_cast<uint32_t>(handle));
    bi.AddForce(id, JPH::Vec3(fx, fy, fz), JPH::RVec3(px, py, pz));
}

void JoltBackend::addImpulseAtPosition(BodyHandle handle,
                                        float ix, float iy, float iz,
                                        float px, float py, float pz)
{
    if (!m_physicsSystem) return;
    JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    JPH::BodyID id(static_cast<uint32_t>(handle));
    bi.AddImpulse(id, JPH::Vec3(ix, iy, iz), JPH::RVec3(px, py, pz));
}

bool JoltBackend::isBodySleeping(BodyHandle handle) const
{
    if (!m_physicsSystem) return false;

    const JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
    JPH::BodyID id(static_cast<uint32_t>(handle));
    return !bi.IsActive(id);
}

PhysicsTypes::BodyHandle JoltBackend::getBodyForEntity(uint32_t entity) const
{
    auto it = m_entityToBodyID.find(entity);
    if (it != m_entityToBodyID.end())
        return static_cast<BodyHandle>(it->second);
    return InvalidBody;
}

// ── Character Controller (Jolt CharacterVirtual) ───────────────────────

PhysicsTypes::CharacterHandle JoltBackend::createCharacter(const CharacterDesc& desc)
{
    if (!m_physicsSystem) return InvalidCharacter;

    // Capsule half-height = (total height - 2*radius) / 2
    float cylinderHalfHeight = (desc.height - 2.0f * desc.radius) * 0.5f;
    if (cylinderHalfHeight < 0.0f) cylinderHalfHeight = 0.01f;

    JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(cylinderHalfHeight, desc.radius);

    JPH::CharacterVirtualSettings settings;
    settings.mShape = capsule;
    settings.mMaxSlopeAngle = desc.maxSlopeAngle * (3.14159265358979f / 180.0f);
    settings.mMaxStrength = 100.0f;
    settings.mCharacterPadding = desc.skinWidth;
    settings.mPenetrationRecoverySpeed = 1.0f;
    settings.mPredictiveContactDistance = 0.1f;

    JPH::RVec3 position(desc.position[0], desc.position[1], desc.position[2]);
    JPH::Quat  rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisY(),
                              desc.rotationYDeg * (3.14159265358979f / 180.0f));

    auto* character = new JPH::CharacterVirtual(&settings, position, rotation,
                                                 0, m_physicsSystem.get());

    CharacterHandle handle = m_nextCharacterHandle++;
    m_handleToCharacter[handle] = character;
    m_entityToCharacterHandle[desc.entityId] = handle;

    return handle;
}

void JoltBackend::removeCharacter(CharacterHandle handle)
{
    auto it = m_handleToCharacter.find(handle);
    if (it == m_handleToCharacter.end()) return;

    // Remove entity mapping
    for (auto eit = m_entityToCharacterHandle.begin(); eit != m_entityToCharacterHandle.end(); ++eit)
    {
        if (eit->second == handle)
        {
            m_entityToCharacterHandle.erase(eit);
            break;
        }
    }

    delete it->second;
    m_handleToCharacter.erase(it);
}

void JoltBackend::removeAllCharacters()
{
    for (auto& [handle, character] : m_handleToCharacter)
        delete character;

    m_handleToCharacter.clear();
    m_entityToCharacterHandle.clear();
}

void JoltBackend::updateCharacter(CharacterHandle handle,
                                   float dt,
                                   float desiredVelX, float desiredVelY, float desiredVelZ,
                                   float gravityX, float gravityY, float gravityZ)
{
    auto it = m_handleToCharacter.find(handle);
    if (it == m_handleToCharacter.end() || !m_physicsSystem) return;

    JPH::CharacterVirtual* character = it->second;

    JPH::Vec3 gravity(gravityX, gravityY, gravityZ);
    JPH::Vec3 desiredVel(desiredVelX, desiredVelY, desiredVelZ);

    // Apply gravity to vertical velocity if not grounded
    JPH::Vec3 currentVel = character->GetLinearVelocity();
    if (character->GetGroundState() != JPH::CharacterVirtual::EGroundState::OnGround)
    {
        desiredVel += JPH::Vec3(0, currentVel.GetY(), 0);
        desiredVel += gravity * dt;
    }
    else
    {
        // On ground: zero out vertical velocity (unless jumping via desiredVelY)
        if (desiredVelY <= 0.0f)
            desiredVel = JPH::Vec3(desiredVelX, 0.0f, desiredVelZ);
    }

    character->SetLinearVelocity(desiredVel);

    // Extended update handles step-up, stick-to-floor
    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
    updateSettings.mWalkStairsStepUp = JPH::Vec3(0, 0.3f, 0); // Will be configured per-entity
    updateSettings.mStickToFloorStepDown = JPH::Vec3(0, -0.5f, 0);
    updateSettings.mWalkStairsMinStepForward = 0.02f;
    updateSettings.mWalkStairsStepForwardTest = 0.15f;

    // Use the broad phase and object layer filters
    JPH::DefaultBroadPhaseLayerFilter bpFilter(
        *m_objectVsBroadPhaseFilter, Layers::MOVING);
    JPH::DefaultObjectLayerFilter objFilter(
        *m_objectLayerPairFilter, Layers::MOVING);

    character->ExtendedUpdate(dt, gravity, updateSettings,
                               bpFilter, objFilter,
                               {}, {},
                               *m_tempAllocator);
}

PhysicsTypes::CharacterState JoltBackend::getCharacterState(CharacterHandle handle) const
{
    CharacterState state{};
    auto it = m_handleToCharacter.find(handle);
    if (it == m_handleToCharacter.end()) return state;

    const JPH::CharacterVirtual* character = it->second;

    JPH::RVec3 pos = character->GetPosition();
    state.position[0] = static_cast<float>(pos.GetX());
    state.position[1] = static_cast<float>(pos.GetY());
    state.position[2] = static_cast<float>(pos.GetZ());

    auto groundState = character->GetGroundState();
    state.isGrounded = (groundState == JPH::CharacterVirtual::EGroundState::OnGround);

    if (state.isGrounded || groundState == JPH::CharacterVirtual::EGroundState::OnSteepGround)
    {
        JPH::Vec3 normal = character->GetGroundNormal();
        state.groundNormal[0] = normal.GetX();
        state.groundNormal[1] = normal.GetY();
        state.groundNormal[2] = normal.GetZ();

        // Angle between ground normal and up vector
        float dot = normal.GetY(); // dot(normal, up) where up = (0,1,0)
        dot = std::clamp(dot, -1.0f, 1.0f);
        state.groundAngle = std::acos(dot) * (180.0f / 3.14159265358979f);
    }

    JPH::Vec3 vel = character->GetLinearVelocity();
    state.velocity[0] = vel.GetX();
    state.velocity[1] = vel.GetY();
    state.velocity[2] = vel.GetZ();

    return state;
}

void JoltBackend::setCharacterPosition(CharacterHandle handle,
                                        float x, float y, float z)
{
    auto it = m_handleToCharacter.find(handle);
    if (it == m_handleToCharacter.end()) return;

    it->second->SetPosition(JPH::RVec3(x, y, z));
}

PhysicsTypes::CharacterHandle JoltBackend::getCharacterForEntity(uint32_t entity) const
{
    auto it = m_entityToCharacterHandle.find(entity);
    if (it != m_entityToCharacterHandle.end())
        return it->second;
    return InvalidCharacter;
}
