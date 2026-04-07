#include "PhysicsWorld.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <filesystem>

#include "../AssetManager/json.hpp"

#include "../Core/ECS/ECS.h"
#include "../Diagnostics/DiagnosticsManager.h"
#include "../Logger/Logger.h"

using json = nlohmann::json;

namespace
{
    uint64_t mixBodyConfigHash(uint64_t seed, uint64_t value)
    {
        seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        return seed;
    }

    uint64_t hashBodyConfig(const JoltBackend::BodyDesc& desc)
    {
        uint64_t hash = 1469598103934665603ull;

        auto hashFloat = [&hash](float value)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            hash = mixBodyConfigHash(hash, bits);
        };

        hash = mixBodyConfigHash(hash, static_cast<uint64_t>(desc.shape));
        for (float v : desc.halfExtents) hashFloat(v);
        hashFloat(desc.radius);
        hashFloat(desc.halfHeight);

        hash = mixBodyConfigHash(hash, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(desc.heightData)));
        hash = mixBodyConfigHash(hash, static_cast<uint64_t>(desc.heightSampleCount));
        for (float v : desc.heightOffset) hashFloat(v);
        for (float v : desc.heightScale) hashFloat(v);
        hash = mixBodyConfigHash(hash, static_cast<uint64_t>(desc.meshAssetId));
        hash = mixBodyConfigHash(hash, desc.meshAssetSignature);
        hash = mixBodyConfigHash(hash, static_cast<uint64_t>(desc.meshVertexFloatCount));
        hash = mixBodyConfigHash(hash, static_cast<uint64_t>(desc.meshIndexCount));
        hash = mixBodyConfigHash(hash, desc.meshTreatAsConvex ? 1ull : 0ull);
        for (float v : desc.meshScale) hashFloat(v);
        for (float v : desc.colliderOffset) hashFloat(v);

        hash = mixBodyConfigHash(hash, static_cast<uint64_t>(desc.motionType));
        hash = mixBodyConfigHash(hash, static_cast<uint64_t>(desc.motionQuality));
        hashFloat(desc.restitution);
        hashFloat(desc.friction);
        hash = mixBodyConfigHash(hash, desc.isSensor ? 1ull : 0ull);

        hashFloat(desc.mass);
        hashFloat(desc.gravityFactor);
        hashFloat(desc.linearDamping);
        hashFloat(desc.angularDamping);
        hashFloat(desc.maxLinearVelocity);
        hashFloat(desc.maxAngularVelocity);
        hash = mixBodyConfigHash(hash, desc.allowSleeping ? 1ull : 0ull);

        return hash;
    }

    uint64_t hashBodyTransform(const float position[3], const float rotationEulerDeg[3])
    {
        uint64_t hash = 1469598103934665603ull;

        auto hashFloat = [&hash](float value)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            hash = mixBodyConfigHash(hash, bits);
        };

        for (int i = 0; i < 3; ++i) hashFloat(position[i]);
        for (int i = 0; i < 3; ++i) hashFloat(rotationEulerDeg[i]);
        return hash;
    }

    void inverseRotatePoint(const float euler[3], const float in[3], float out[3])
    {
        constexpr float deg2rad = 3.14159265358979f / 180.0f;
        const float cx = cosf(euler[0] * deg2rad), sx = sinf(euler[0] * deg2rad);
        const float cy = cosf(euler[1] * deg2rad), sy = sinf(euler[1] * deg2rad);
        const float cz = cosf(euler[2] * deg2rad), sz = sinf(euler[2] * deg2rad);

        const float r00 = cy * cz + sy * sx * sz;
        const float r01 = -cy * sz + sy * sx * cz;
        const float r02 = sy * cx;
        const float r10 = cx * sz;
        const float r11 = cx * cz;
        const float r12 = -sx;
        const float r20 = -sy * cz + cy * sx * sz;
        const float r21 = sy * sz + cy * sx * cz;
        const float r22 = cy * cx;

        out[0] = r00 * in[0] + r10 * in[1] + r20 * in[2];
        out[1] = r01 * in[0] + r11 * in[1] + r21 * in[2];
        out[2] = r02 * in[0] + r12 * in[1] + r22 * in[2];
    }

    float wrapAngle(float a)
    {
        a = fmodf(a + 180.0f, 360.0f);
        if (a < 0.0f) a += 360.0f;
        return a - 180.0f;
    }

    void writeBackPhysicsTransform(ECS::ECSManager& ecs, uint32_t entity,
        const float worldPosition[3], const float* worldRotationEulerDeg)
    {
        auto* tc = ecs.getComponent<ECS::TransformComponent>(entity);
        if (!tc)
            return;

        tc->position[0] = worldPosition[0];
        tc->position[1] = worldPosition[1];
        tc->position[2] = worldPosition[2];

        if (worldRotationEulerDeg)
        {
            tc->rotation[0] = worldRotationEulerDeg[0];
            tc->rotation[1] = worldRotationEulerDeg[1];
            tc->rotation[2] = worldRotationEulerDeg[2];
        }

        if (tc->parent == ECS::InvalidEntity)
        {
            std::memcpy(tc->localPosition, tc->position, sizeof(tc->localPosition));
            if (worldRotationEulerDeg)
                std::memcpy(tc->localRotation, tc->rotation, sizeof(tc->localRotation));
        }
        else if (const auto* parentTc = ecs.getComponent<ECS::TransformComponent>(tc->parent))
        {
            float diff[3] = {
                tc->position[0] - parentTc->position[0],
                tc->position[1] - parentTc->position[1],
                tc->position[2] - parentTc->position[2]
            };
            float unrotated[3]{};
            inverseRotatePoint(parentTc->rotation, diff, unrotated);

            tc->localPosition[0] = (std::abs(parentTc->scale[0]) > 1.0e-6f) ? (unrotated[0] / parentTc->scale[0]) : 0.0f;
            tc->localPosition[1] = (std::abs(parentTc->scale[1]) > 1.0e-6f) ? (unrotated[1] / parentTc->scale[1]) : 0.0f;
            tc->localPosition[2] = (std::abs(parentTc->scale[2]) > 1.0e-6f) ? (unrotated[2] / parentTc->scale[2]) : 0.0f;

            if (worldRotationEulerDeg)
            {
                tc->localRotation[0] = wrapAngle(tc->rotation[0] - parentTc->rotation[0]);
                tc->localRotation[1] = wrapAngle(tc->rotation[1] - parentTc->rotation[1]);
                tc->localRotation[2] = wrapAngle(tc->rotation[2] - parentTc->rotation[2]);
            }
        }

        tc->dirty = false;
        for (uint32_t child : tc->children)
            ecs.markTransformDirty(child);
    }

    struct MeshCollisionShapeData
    {
        std::vector<float> vertices;
        std::vector<uint32_t> indices;
        uint64_t signature{ 0 };
        bool hasGeometry{ false };
    };

    std::optional<std::filesystem::path> resolveMeshAssetPath(const ECS::MeshComponent* mesh)
    {
        if (!mesh || mesh->meshAssetPath.empty())
            return std::nullopt;

        const std::filesystem::path rawPath(mesh->meshAssetPath);
        std::vector<std::filesystem::path> candidates;
        if (rawPath.is_absolute())
            candidates.push_back(rawPath);
        else
        {
            candidates.push_back(rawPath);

            const auto& projectInfo = DiagnosticsManager::Instance().getProjectInfo();
            if (!projectInfo.projectPath.empty())
            {
                candidates.push_back(std::filesystem::path(projectInfo.projectPath) / "Content" / rawPath);
                candidates.push_back(std::filesystem::path(projectInfo.projectPath) / rawPath);
            }

            const std::filesystem::path cwd = std::filesystem::current_path();
            candidates.push_back(cwd / "Content" / rawPath);
            candidates.push_back(cwd / rawPath);
            candidates.push_back(cwd.parent_path() / "Content" / rawPath);
        }

        for (const auto& candidate : candidates)
        {
            std::error_code ec;
            if (!candidate.empty() && std::filesystem::exists(candidate, ec) && !ec)
                return candidate;
        }

        return std::nullopt;
    }

    MeshCollisionShapeData loadMeshCollisionShapeData(const ECS::MeshComponent* mesh)
    {
        MeshCollisionShapeData result;
        if (!mesh)
            return result;

        result.signature = mixBodyConfigHash(result.signature, static_cast<uint64_t>(mesh->meshAssetId));
        result.signature = mixBodyConfigHash(result.signature, std::hash<std::string>{}(mesh->meshAssetPath));

        auto resolvedPath = resolveMeshAssetPath(mesh);
        if (!resolvedPath)
            return result;

        std::error_code ec;
        result.signature = mixBodyConfigHash(result.signature, static_cast<uint64_t>(std::filesystem::file_size(*resolvedPath, ec)));
        ec.clear();
        result.signature = mixBodyConfigHash(result.signature,
            static_cast<uint64_t>(std::filesystem::last_write_time(*resolvedPath, ec).time_since_epoch().count()));

        std::ifstream in(*resolvedPath, std::ios::in | std::ios::binary);
        if (!in.is_open())
            return result;

        try
        {
            json root;
            in >> root;
            const json* data = &root;
            if (root.is_object())
            {
                auto it = root.find("data");
                if (it != root.end() && it->is_object())
                    data = &(*it);
            }

            auto itVertices = data->find("m_vertices");
            auto itIndices = data->find("m_indices");
            if (itVertices == data->end() || !itVertices->is_array())
                return result;

            result.vertices = itVertices->get<std::vector<float>>();
            if (itIndices != data->end() && itIndices->is_array())
                result.indices = itIndices->get<std::vector<uint32_t>>();

            result.hasGeometry = !result.vertices.empty() && (result.vertices.size() % 5) == 0;
        }
        catch (...)
        {
            result.vertices.clear();
            result.indices.clear();
            result.hasGeometry = false;
        }

        return result;
    }
}

// ── Singleton ───────────────────────────────────────────────────────────

PhysicsWorld& PhysicsWorld::Instance()
{
    static PhysicsWorld instance;
    return instance;
}

// ── Lifecycle ───────────────────────────────────────────────────────────

void PhysicsWorld::initialize()
{
    initialize(Backend::Jolt);
}

void PhysicsWorld::initialize(Backend backend)
{
    if (m_initialized)
        shutdown();

    (void)backend;

    m_backend = std::make_unique<JoltBackend>();
    Logger::Instance().log(Logger::Category::Engine, "PhysicsWorld: using Jolt backend.", Logger::LogLevel::INFO);

    m_backend->initialize();

    m_gravity[0] = 0.0f;
    m_gravity[1] = -9.81f;
    m_gravity[2] = 0.0f;
    m_fixedTimestep = 1.0f / 60.0f;
    m_accumulator = m_fixedTimestep;
    m_sleepThreshold = 0.05f;

    m_backend->setGravity(m_gravity[0], m_gravity[1], m_gravity[2]);

    m_collisionEvents.clear();
    m_activeOverlaps.clear();
    m_beginOverlapEvents.clear();
    m_endOverlapEvents.clear();
    m_trackedEntities.clear();
    m_trackedCharacters.clear();
    m_bodyConfigHashes.clear();
    m_bodyTransformHashes.clear();

    m_initialized = true;
    Logger::Instance().log(Logger::Category::Engine, "PhysicsWorld initialised.", Logger::LogLevel::INFO);
}

void PhysicsWorld::shutdown()
{
    if (m_backend)
    {
        m_backend->shutdown();
        m_backend.reset();
    }

    m_trackedEntities.clear();
    m_trackedCharacters.clear();
    m_bodyConfigHashes.clear();
    m_bodyTransformHashes.clear();
    m_collisionEvents.clear();
    m_collisionCallback = nullptr;
    m_activeOverlaps.clear();
    m_beginOverlapEvents.clear();
    m_endOverlapEvents.clear();

    m_initialized = false;
}

void PhysicsWorld::setGravity(float x, float y, float z)
{
    m_gravity[0] = x;
    m_gravity[1] = y;
    m_gravity[2] = z;
    if (m_backend)
        m_backend->setGravity(x, y, z);
}

void PhysicsWorld::getGravity(float& x, float& y, float& z) const
{
    x = m_gravity[0];
    y = m_gravity[1];
    z = m_gravity[2];
}

// ── Main step ───────────────────────────────────────────────────────────

void PhysicsWorld::step(float dt)
{
    if (!m_initialized || !m_backend) return;
    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;

    std::vector<unsigned int> dirtyPhysicsEntities;
    DiagnosticsManager::Instance().consumeDirtyPhysicsEntities(dirtyPhysicsEntities);
    for (unsigned int entity : dirtyPhysicsEntities)
    {
        requestBodyRebuild(entity);
    }

    m_collisionEvents.clear();

    // Update world transforms from local transforms (parenting hierarchy)
    ECS::ECSManager::Instance().updateWorldTransforms();

    // Sync ECS → Backend (create/update bodies)
    syncBodiesToBackend();
    syncCharactersToBackend();

    m_accumulator += dt;

    while (m_accumulator >= m_fixedTimestep)
    {
        m_backend->update(m_fixedTimestep);
        updateCharacters(m_fixedTimestep);
        m_accumulator -= m_fixedTimestep;
    }

    // Collect collision events from backend
    auto backendEvents = m_backend->drainCollisionEvents();
    m_collisionEvents.reserve(backendEvents.size());
    for (const auto& be : backendEvents)
    {
        CollisionEvent ev{};
        ev.entityA = be.entityA;
        ev.entityB = be.entityB;
        std::memcpy(ev.normal, be.normal, sizeof(ev.normal));
        ev.depth = be.depth;
        std::memcpy(ev.contactPoint, be.contactPoint, sizeof(ev.contactPoint));
        m_collisionEvents.push_back(ev);
    }

    // Sync Backend → ECS
    syncBodiesFromBackend();
    syncCharactersFromBackend();
    ECS::ECSManager::Instance().updateWorldTransforms();

    updateOverlapTracking();
    fireCollisionEvents();
}

// ── Sync ECS entities → Backend bodies ──────────────────────────────────

void PhysicsWorld::syncBodiesToBackend()
{
    auto& ecs = ECS::ECSManager::Instance();
    auto schema = ECS::Schema().require<ECS::TransformComponent>().require<ECS::CollisionComponent>();
    auto entities = ecs.getEntitiesMatchingSchema(schema);

    std::set<uint32_t> aliveEntities;

    for (auto entity : entities)
    {
        const auto* tc = ecs.getComponent<ECS::TransformComponent>(entity);
        const auto* cc = ecs.getComponent<ECS::CollisionComponent>(entity);
        if (!tc || !cc) continue;

        // Skip entities that use a Character Controller instead of a Rigidbody
        if (ecs.hasComponent<ECS::CharacterControllerComponent>(entity)) continue;

        const auto* pc = ecs.getComponent<ECS::PhysicsComponent>(entity);
        const auto* mc = ecs.getComponent<ECS::MeshComponent>(entity);

        aliveEntities.insert(entity);

        JoltBackend::BodyDesc desc{};
        desc.entityId = entity;
        const float absScaleX = std::abs(tc->scale[0]);
        const float absScaleY = std::abs(tc->scale[1]);
        const float absScaleZ = std::abs(tc->scale[2]);
        MeshCollisionShapeData meshShapeData;

        // Shape
        int colliderType = static_cast<int>(cc->colliderType);

        switch (colliderType)
        {
        case 1: // Sphere
            desc.shape  = JoltBackend::BodyDesc::Shape::Sphere;
            desc.radius = cc->colliderSize[0] * std::max({ absScaleX, absScaleY, absScaleZ });
            break;
        case 2: // Capsule
            desc.shape      = JoltBackend::BodyDesc::Shape::Capsule;
            desc.radius     = cc->colliderSize[0] * std::max(absScaleX, absScaleZ);
            desc.halfHeight = cc->colliderSize[1] * absScaleY;
            break;
        case 3: // Cylinder
            desc.shape      = JoltBackend::BodyDesc::Shape::Cylinder;
            desc.radius     = cc->colliderSize[0] * std::max(absScaleX, absScaleZ);
            desc.halfHeight = cc->colliderSize[1] * absScaleY;
            break;
        case 4: // Mesh
            meshShapeData = loadMeshCollisionShapeData(mc);
            desc.meshAssetId = mc ? mc->meshAssetId : 0;
            desc.meshAssetSignature = meshShapeData.signature;
            desc.meshScale[0] = absScaleX;
            desc.meshScale[1] = absScaleY;
            desc.meshScale[2] = absScaleZ;
            desc.meshTreatAsConvex = (pc != nullptr);
            if (meshShapeData.hasGeometry)
            {
                desc.shape = JoltBackend::BodyDesc::Shape::Mesh;
                desc.meshVertices = meshShapeData.vertices.data();
                desc.meshVertexFloatCount = static_cast<int>(meshShapeData.vertices.size());
                desc.meshIndices = meshShapeData.indices.empty() ? nullptr : meshShapeData.indices.data();
                desc.meshIndexCount = static_cast<int>(meshShapeData.indices.size());
            }
            else
            {
                desc.shape          = JoltBackend::BodyDesc::Shape::Box;
                desc.halfExtents[0] = cc->colliderSize[0] * absScaleX;
                desc.halfExtents[1] = cc->colliderSize[1] * absScaleY;
                desc.halfExtents[2] = cc->colliderSize[2] * absScaleZ;
            }
            break;
        case 5: // HeightField
        {
            desc.shape = JoltBackend::BodyDesc::Shape::HeightField;
            const auto* hfc = ecs.getComponent<ECS::HeightFieldComponent>(entity);
            if (hfc && hfc->sampleCount > 0 && !hfc->heights.empty())
            {
                desc.heightData        = hfc->heights.data();
                desc.heightSampleCount = hfc->sampleCount;
                desc.heightOffset[0]   = hfc->offsetX;
                desc.heightOffset[1]   = hfc->offsetY;
                desc.heightOffset[2]   = hfc->offsetZ;
                desc.heightScale[0]    = hfc->scaleX * absScaleX;
                desc.heightScale[1]    = hfc->scaleY * absScaleY;
                desc.heightScale[2]    = hfc->scaleZ * absScaleZ;
            }
            desc.motionType    = JoltBackend::BodyDesc::MotionType::Static;
            desc.allowSleeping = true;
            break;
        }
        default: // Box (0) or fallback
            desc.shape          = JoltBackend::BodyDesc::Shape::Box;
            desc.halfExtents[0] = cc->colliderSize[0] * absScaleX;
            desc.halfExtents[1] = cc->colliderSize[1] * absScaleY;
            desc.halfExtents[2] = cc->colliderSize[2] * absScaleZ;
            break;
        }

        // Collider offset
        std::memcpy(desc.colliderOffset, cc->colliderOffset, sizeof(desc.colliderOffset));

        // Transform
        std::memcpy(desc.position, tc->position, sizeof(desc.position));
        std::memcpy(desc.rotationEulerDeg, tc->rotation, sizeof(desc.rotationEulerDeg));

        // Material
        desc.restitution = cc->restitution;
        desc.friction    = cc->friction;
        desc.isSensor    = cc->isSensor;

        // Motion type
        // Entities without PhysicsComponent default to Static so regular
        // collision-only scene geometry behaves like stable world geometry.
        // Sensors are the exception: they need Kinematic to generate overlap
        // events against other static bodies.
        if (pc)
        {
            desc.motionType = JoltBackend::BodyDesc::MotionType::Static;
            switch (pc->motionType)
            {
            case ECS::PhysicsComponent::MotionType::Dynamic:   desc.motionType = JoltBackend::BodyDesc::MotionType::Dynamic;   break;
            case ECS::PhysicsComponent::MotionType::Kinematic:  desc.motionType = JoltBackend::BodyDesc::MotionType::Kinematic;  break;
            default: break;
            }

            desc.mass               = pc->mass;
            desc.gravityFactor      = pc->gravityFactor;
            desc.linearDamping      = pc->linearDamping;
            desc.angularDamping     = pc->angularDamping;
            desc.maxLinearVelocity  = pc->maxLinearVelocity;
            desc.maxAngularVelocity = pc->maxAngularVelocity;
            desc.allowSleeping      = pc->allowSleeping;
            desc.motionQuality      = (pc->motionQuality == ECS::PhysicsComponent::MotionQuality::LinearCast)
                ? JoltBackend::BodyDesc::MotionQuality::LinearCast
                : JoltBackend::BodyDesc::MotionQuality::Discrete;
            std::memcpy(desc.velocity, pc->velocity, sizeof(desc.velocity));
            std::memcpy(desc.angularVelocity, pc->angularVelocity, sizeof(desc.angularVelocity));
        }
        else
        {
            desc.motionType    = desc.isSensor
                ? JoltBackend::BodyDesc::MotionType::Kinematic
                : JoltBackend::BodyDesc::MotionType::Static;
            desc.allowSleeping = false;
            desc.gravityFactor = 0.0f;
        }

        if (desc.isSensor && desc.motionType == JoltBackend::BodyDesc::MotionType::Static)
        {
            desc.motionType    = JoltBackend::BodyDesc::MotionType::Kinematic;
            desc.allowSleeping = false;
            desc.gravityFactor = 0.0f;
        }

        if (desc.shape == JoltBackend::BodyDesc::Shape::Mesh
            && desc.motionType != JoltBackend::BodyDesc::MotionType::Static)
        {
            desc.meshTreatAsConvex = true;
        }

        auto existingHandle = m_backend->getBodyForEntity(entity);
        uint64_t configHash = hashBodyConfig(desc);
        uint64_t transformHash = hashBodyTransform(desc.position, desc.rotationEulerDeg);

        if (existingHandle != JoltBackend::InvalidBody)
        {
            auto it = m_bodyConfigHashes.find(entity);
            bool needsRebuild = (it == m_bodyConfigHashes.end()) || (it->second != configHash);

            if (needsRebuild)
            {
                if (desc.motionType == JoltBackend::BodyDesc::MotionType::Dynamic)
                {
                    auto state = m_backend->getBodyState(existingHandle);
                    std::memcpy(desc.position, state.position, sizeof(desc.position));
                    std::memcpy(desc.rotationEulerDeg, state.rotationEulerDeg, sizeof(desc.rotationEulerDeg));
                    std::memcpy(desc.velocity, state.velocity, sizeof(desc.velocity));
                    std::memcpy(desc.angularVelocity, state.angularVelocityDeg, sizeof(desc.angularVelocity));
                }

                m_backend->removeBody(existingHandle);
                m_backend->createBody(desc);
                m_bodyConfigHashes[entity] = configHash;
                m_bodyTransformHashes[entity] = transformHash;
            }
            else if (desc.motionType != JoltBackend::BodyDesc::MotionType::Dynamic)
            {
                auto itTransform = m_bodyTransformHashes.find(entity);
                bool transformChanged = (itTransform == m_bodyTransformHashes.end()) || (itTransform->second != transformHash);
                if (transformChanged)
                {
                    m_backend->setBodyPositionRotation(existingHandle,
                        tc->position[0], tc->position[1], tc->position[2],
                        tc->rotation[0], tc->rotation[1], tc->rotation[2]);
                    m_bodyTransformHashes[entity] = transformHash;
                }
            }
        }
        else
        {
            m_backend->createBody(desc);
            m_trackedEntities.insert(entity);
            m_bodyConfigHashes[entity] = configHash;
            m_bodyTransformHashes[entity] = transformHash;
        }
    }

    // Remove bodies for entities that no longer exist
    std::vector<uint32_t> toRemove;
    for (uint32_t entity : m_trackedEntities)
    {
        if (aliveEntities.find(entity) == aliveEntities.end())
            toRemove.push_back(entity);
    }
    for (uint32_t entity : toRemove)
    {
        auto handle = m_backend->getBodyForEntity(entity);
        if (handle != JoltBackend::InvalidBody)
            m_backend->removeBody(handle);
        m_trackedEntities.erase(entity);
        m_bodyConfigHashes.erase(entity);
        m_bodyTransformHashes.erase(entity);
    }
}

// ── Sync Backend → ECS ──────────────────────────────────────────────────

void PhysicsWorld::syncBodiesFromBackend()
{
    auto& ecs = ECS::ECSManager::Instance();

    for (uint32_t entity : m_trackedEntities)
    {
        auto* tc = ecs.getComponent<ECS::TransformComponent>(entity);
        if (!tc) continue;

        auto* pc = ecs.getComponent<ECS::PhysicsComponent>(entity);
        if (!pc) continue;
        if (pc->motionType != ECS::PhysicsComponent::MotionType::Dynamic) continue;

        auto handle = m_backend->getBodyForEntity(entity);
        if (handle == JoltBackend::InvalidBody) continue;

        auto state = m_backend->getBodyState(handle);

        writeBackPhysicsTransform(ecs, entity, state.position, state.rotationEulerDeg);

        pc->velocity[0] = state.velocity[0];
        pc->velocity[1] = state.velocity[1];
        pc->velocity[2] = state.velocity[2];

        pc->angularVelocity[0] = state.angularVelocityDeg[0];
        pc->angularVelocity[1] = state.angularVelocityDeg[1];
        pc->angularVelocity[2] = state.angularVelocityDeg[2];
    }
}

// ── Collision event dispatch ────────────────────────────────────────────

void PhysicsWorld::fireCollisionEvents()
{
    if (!m_collisionCallback) return;
    for (const auto& ev : m_collisionEvents)
    {
        m_collisionCallback(ev);
    }
}

// ── Overlap tracking (begin / end) ─────────────────────────────────────

void PhysicsWorld::updateOverlapTracking()
{
    m_beginOverlapEvents.clear();
    m_endOverlapEvents.clear();

    std::set<std::pair<uint32_t, uint32_t>> currentOverlaps;
    for (const auto& ev : m_collisionEvents)
    {
        auto pair = std::minmax(ev.entityA, ev.entityB);
        currentOverlaps.insert(pair);
    }

    for (const auto& pair : currentOverlaps)
    {
        if (m_activeOverlaps.find(pair) == m_activeOverlaps.end())
        {
            OverlapEvent oe{};
            oe.entityA = pair.first;
            oe.entityB = pair.second;
            m_beginOverlapEvents.push_back(oe);
        }
    }

    for (const auto& pair : m_activeOverlaps)
    {
        if (currentOverlaps.find(pair) == currentOverlaps.end())
        {
            OverlapEvent oe{};
            oe.entityA = pair.first;
            oe.entityB = pair.second;
            m_endOverlapEvents.push_back(oe);
        }
    }

    m_activeOverlaps = std::move(currentOverlaps);
}

// ── Sleep query ─────────────────────────────────────────────────────────

bool PhysicsWorld::isBodySleeping(uint32_t entity) const
{
    if (!m_backend) return false;

    auto handle = m_backend->getBodyForEntity(entity);
    if (handle == JoltBackend::InvalidBody) return false;

    return m_backend->isBodySleeping(handle);
}

void PhysicsWorld::requestBodyRebuild(uint32_t entity)
{
    m_bodyConfigHashes.erase(entity);
    m_bodyTransformHashes.erase(entity);
}

void PhysicsWorld::requestAllBodyRebuilds()
{
    m_bodyConfigHashes.clear();
    m_bodyTransformHashes.clear();
}

// ── Raycast ─────────────────────────────────────────────────────────────

PhysicsWorld::RaycastHit PhysicsWorld::raycast(float ox, float oy, float oz,
                                               float dx, float dy, float dz,
                                               float maxDist) const
{
    RaycastHit result{};
    if (!m_backend) return result;

    auto br = m_backend->raycast(ox, oy, oz, dx, dy, dz, maxDist);
    result.hit      = br.hit;
    result.entity   = br.entity;
    result.distance = br.distance;
    std::memcpy(result.point, br.point, sizeof(result.point));
    std::memcpy(result.normal, br.normal, sizeof(result.normal));

    return result;
}

// ── Overlap queries ─────────────────────────────────────────────────────

std::vector<uint32_t> PhysicsWorld::overlapSphere(float cx, float cy, float cz,
                                                   float radius) const
{
    if (!m_backend) return {};
    return m_backend->overlapSphere(cx, cy, cz, radius);
}

std::vector<uint32_t> PhysicsWorld::overlapBox(float cx, float cy, float cz,
                                                float hx, float hy, float hz,
                                                float eulerX, float eulerY, float eulerZ) const
{
    if (!m_backend) return {};
    return m_backend->overlapBox(cx, cy, cz, hx, hy, hz, eulerX, eulerY, eulerZ);
}

// ── Sweep queries ───────────────────────────────────────────────────────

PhysicsWorld::RaycastHit PhysicsWorld::sweepSphere(float ox, float oy, float oz,
                                                    float radius,
                                                    float dx, float dy, float dz,
                                                    float maxDist) const
{
    RaycastHit result{};
    if (!m_backend) return result;

    auto br = m_backend->sweepSphere(ox, oy, oz, radius, dx, dy, dz, maxDist);
    result.hit      = br.hit;
    result.entity   = br.entity;
    result.distance = br.distance;
    std::memcpy(result.point, br.point, sizeof(result.point));
    std::memcpy(result.normal, br.normal, sizeof(result.normal));
    return result;
}

PhysicsWorld::RaycastHit PhysicsWorld::sweepBox(float ox, float oy, float oz,
                                                 float hx, float hy, float hz,
                                                 float dx, float dy, float dz,
                                                 float maxDist) const
{
    RaycastHit result{};
    if (!m_backend) return result;

    auto br = m_backend->sweepBox(ox, oy, oz, hx, hy, hz, dx, dy, dz, maxDist);
    result.hit      = br.hit;
    result.entity   = br.entity;
    result.distance = br.distance;
    std::memcpy(result.point, br.point, sizeof(result.point));
    std::memcpy(result.normal, br.normal, sizeof(result.normal));
    return result;
}

// ── Force / impulse ─────────────────────────────────────────────────────

void PhysicsWorld::addForce(uint32_t entity, float fx, float fy, float fz)
{
    if (!m_backend) return;
    auto handle = m_backend->getBodyForEntity(entity);
    if (handle == JoltBackend::InvalidBody) return;
    m_backend->addForce(handle, fx, fy, fz);
}

void PhysicsWorld::addImpulse(uint32_t entity, float ix, float iy, float iz)
{
    if (!m_backend) return;
    auto handle = m_backend->getBodyForEntity(entity);
    if (handle == JoltBackend::InvalidBody) return;
    m_backend->addImpulse(handle, ix, iy, iz);
}

void PhysicsWorld::addForceAtPosition(uint32_t entity,
                                       float fx, float fy, float fz,
                                       float px, float py, float pz)
{
    if (!m_backend) return;
    auto handle = m_backend->getBodyForEntity(entity);
    if (handle == JoltBackend::InvalidBody) return;
    m_backend->addForceAtPosition(handle, fx, fy, fz, px, py, pz);
}

void PhysicsWorld::addImpulseAtPosition(uint32_t entity,
                                         float ix, float iy, float iz,
                                         float px, float py, float pz)
{
    if (!m_backend) return;
    auto handle = m_backend->getBodyForEntity(entity);
    if (handle == JoltBackend::InvalidBody) return;
    m_backend->addImpulseAtPosition(handle, ix, iy, iz, px, py, pz);
}

void PhysicsWorld::setVelocity(uint32_t entity, float vx, float vy, float vz)
{
    if (!m_backend) return;
    auto handle = m_backend->getBodyForEntity(entity);
    if (handle == JoltBackend::InvalidBody) return;
    m_backend->setLinearVelocity(handle, vx, vy, vz);
}

void PhysicsWorld::setAngularVelocity(uint32_t entity, float vx, float vy, float vz)
{
    if (!m_backend) return;
    auto handle = m_backend->getBodyForEntity(entity);
    if (handle == JoltBackend::InvalidBody) return;
    m_backend->setAngularVelocity(handle, vx, vy, vz);
}

// ── Character Controller sync ──────────────────────────────────────────

void PhysicsWorld::syncCharactersToBackend()
{
    auto& ecs = ECS::ECSManager::Instance();
    auto schema = ECS::Schema()
        .require<ECS::TransformComponent>()
        .require<ECS::CharacterControllerComponent>()
        .require<ECS::CollisionComponent>();
    auto entities = ecs.getEntitiesMatchingSchema(schema);

    std::set<uint32_t> aliveCharacters;

    for (auto entity : entities)
    {
        const auto* tc  = ecs.getComponent<ECS::TransformComponent>(entity);
        const auto* ccc = ecs.getComponent<ECS::CharacterControllerComponent>(entity);
        if (!tc || !ccc) continue;

        aliveCharacters.insert(entity);

        auto existingHandle = m_backend->getCharacterForEntity(entity);
        if (existingHandle != JoltBackend::InvalidCharacter)
        {
            // Character already exists — nothing to update (position is managed by backend)
            continue;
        }

        // Create a new character in the backend
        JoltBackend::CharacterDesc desc{};
        desc.entityId      = entity;
        desc.radius        = ccc->radius;
        desc.height        = ccc->height;
        desc.maxSlopeAngle = ccc->maxSlopeAngle;
        desc.stepUpHeight  = ccc->stepUpHeight;
        desc.skinWidth     = ccc->skinWidth;
        std::memcpy(desc.position, tc->position, sizeof(desc.position));
        desc.rotationYDeg  = tc->rotation[1]; // Yaw

        m_backend->createCharacter(desc);
        m_trackedCharacters.insert(entity);
    }

    // Remove characters for entities that no longer exist
    std::vector<uint32_t> toRemove;
    for (uint32_t entity : m_trackedCharacters)
    {
        if (aliveCharacters.find(entity) == aliveCharacters.end())
            toRemove.push_back(entity);
    }
    for (uint32_t entity : toRemove)
    {
        auto handle = m_backend->getCharacterForEntity(entity);
        if (handle != JoltBackend::InvalidCharacter)
            m_backend->removeCharacter(handle);
        m_trackedCharacters.erase(entity);
    }
}

void PhysicsWorld::updateCharacters(float dt)
{
    auto& ecs = ECS::ECSManager::Instance();

    for (uint32_t entity : m_trackedCharacters)
    {
        auto* ccc = ecs.getComponent<ECS::CharacterControllerComponent>(entity);
        if (!ccc) continue;

        auto handle = m_backend->getCharacterForEntity(entity);
        if (handle == JoltBackend::InvalidCharacter) continue;

        float gx = m_gravity[0] * ccc->gravityFactor;
        float gy = m_gravity[1] * ccc->gravityFactor;
        float gz = m_gravity[2] * ccc->gravityFactor;

        m_backend->updateCharacter(handle, dt,
            ccc->velocity[0], ccc->velocity[1], ccc->velocity[2],
            gx, gy, gz);
    }
}

void PhysicsWorld::syncCharactersFromBackend()
{
    auto& ecs = ECS::ECSManager::Instance();

    for (uint32_t entity : m_trackedCharacters)
    {
        auto* tc  = ecs.getComponent<ECS::TransformComponent>(entity);
        auto* ccc = ecs.getComponent<ECS::CharacterControllerComponent>(entity);
        if (!tc || !ccc) continue;

        auto handle = m_backend->getCharacterForEntity(entity);
        if (handle == JoltBackend::InvalidCharacter) continue;

        auto state = m_backend->getCharacterState(handle);

        writeBackPhysicsTransform(ecs, entity, state.position, nullptr);

        ccc->isGrounded = state.isGrounded;
        std::memcpy(ccc->groundNormal, state.groundNormal, sizeof(ccc->groundNormal));
        ccc->groundAngle = state.groundAngle;

        // Clamp fall speed
        float vy = state.velocity[1];
        if (vy < -ccc->maxFallSpeed)
            vy = -ccc->maxFallSpeed;

        ccc->velocity[0] = state.velocity[0];
        ccc->velocity[1] = vy;
        ccc->velocity[2] = state.velocity[2];
    }
}
