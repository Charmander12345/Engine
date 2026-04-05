#include "PhysicsWorld.h"
#include "IPhysicsBackend.h"
#include "JoltBackend.h"

#ifdef ENGINE_PHYSX_BACKEND_AVAILABLE
#include "PhysXBackend.h"
#endif

#include <cmath>
#include <algorithm>
#include <cstring>

#include "../Core/ECS/ECS.h"
#include "../Logger/Logger.h"

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

    // Create the selected backend.
    switch (backend)
    {
#ifdef ENGINE_PHYSX_BACKEND_AVAILABLE
    case Backend::PhysX:
        m_backend = std::make_unique<PhysXBackend>();
        Logger::Instance().log(Logger::Category::Engine, "PhysicsWorld: using PhysX backend.", Logger::LogLevel::INFO);
        break;
#endif
    case Backend::Jolt:
    default:
        m_backend = std::make_unique<JoltBackend>();
        Logger::Instance().log(Logger::Category::Engine, "PhysicsWorld: using Jolt backend.", Logger::LogLevel::INFO);
        break;
    }

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

        aliveEntities.insert(entity);

        auto existingHandle = m_backend->getBodyForEntity(entity);

        if (existingHandle != IPhysicsBackend::InvalidBody)
        {
            // Body already exists — update kinematic/static position
            IPhysicsBackend::BodyDesc::MotionType mt = IPhysicsBackend::BodyDesc::MotionType::Kinematic;
            if (pc)
            {
                switch (pc->motionType)
                {
                case ECS::PhysicsComponent::MotionType::Dynamic:   mt = IPhysicsBackend::BodyDesc::MotionType::Dynamic;   break;
                case ECS::PhysicsComponent::MotionType::Kinematic:  mt = IPhysicsBackend::BodyDesc::MotionType::Kinematic;  break;
                case ECS::PhysicsComponent::MotionType::Static:     mt = IPhysicsBackend::BodyDesc::MotionType::Static;     break;
                default: break;
                }
            }
            if (mt != IPhysicsBackend::BodyDesc::MotionType::Dynamic)
            {
                m_backend->setBodyPositionRotation(existingHandle,
                    tc->position[0], tc->position[1], tc->position[2],
                    tc->rotation[0], tc->rotation[1], tc->rotation[2]);
            }
        }
        else
        {
            // Build a BodyDesc from ECS components
            IPhysicsBackend::BodyDesc desc{};
            desc.entityId = entity;

            // Shape
            int colliderType = static_cast<int>(cc->colliderType);
            if (colliderType == 4) colliderType = 0; // Mesh → Box fallback

            switch (colliderType)
            {
            case 1: // Sphere
                desc.shape  = IPhysicsBackend::BodyDesc::Shape::Sphere;
                desc.radius = cc->colliderSize[0] * std::max({ tc->scale[0], tc->scale[1], tc->scale[2] });
                break;
            case 2: // Capsule
                desc.shape      = IPhysicsBackend::BodyDesc::Shape::Capsule;
                desc.radius     = cc->colliderSize[0] * std::max(tc->scale[0], tc->scale[2]);
                desc.halfHeight = cc->colliderSize[1] * tc->scale[1];
                break;
            case 3: // Cylinder
                desc.shape      = IPhysicsBackend::BodyDesc::Shape::Cylinder;
                desc.radius     = cc->colliderSize[0] * std::max(tc->scale[0], tc->scale[2]);
                desc.halfHeight = cc->colliderSize[1] * tc->scale[1];
                break;
            case 5: // HeightField
            {
                desc.shape = IPhysicsBackend::BodyDesc::Shape::HeightField;
                const auto* hfc = ecs.getComponent<ECS::HeightFieldComponent>(entity);
                if (hfc && hfc->sampleCount > 0 && !hfc->heights.empty())
                {
                    desc.heightData        = hfc->heights.data();
                    desc.heightSampleCount = hfc->sampleCount;
                    desc.heightOffset[0]   = hfc->offsetX;
                    desc.heightOffset[1]   = hfc->offsetY;
                    desc.heightOffset[2]   = hfc->offsetZ;
                    desc.heightScale[0]    = hfc->scaleX;
                    desc.heightScale[1]    = hfc->scaleY;
                    desc.heightScale[2]    = hfc->scaleZ;
                }
                break;
            }
            default: // Box (0) or fallback
                desc.shape          = IPhysicsBackend::BodyDesc::Shape::Box;
                desc.halfExtents[0] = cc->colliderSize[0] * tc->scale[0];
                desc.halfExtents[1] = cc->colliderSize[1] * tc->scale[1];
                desc.halfExtents[2] = cc->colliderSize[2] * tc->scale[2];
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
            // Entities without PhysicsComponent default to Kinematic so their
            // collider stays active on the MOVING layer and detects overlaps.
            // PhysicsComponent is only needed for dynamics (gravity, forces).
            if (pc)
            {
                desc.motionType = IPhysicsBackend::BodyDesc::MotionType::Static;
                switch (pc->motionType)
                {
                case ECS::PhysicsComponent::MotionType::Dynamic:   desc.motionType = IPhysicsBackend::BodyDesc::MotionType::Dynamic;   break;
                case ECS::PhysicsComponent::MotionType::Kinematic:  desc.motionType = IPhysicsBackend::BodyDesc::MotionType::Kinematic;  break;
                default: break;
                }

                desc.mass              = pc->mass;
                desc.gravityFactor     = pc->gravityFactor;
                desc.linearDamping     = pc->linearDamping;
                desc.angularDamping    = pc->angularDamping;
                desc.maxLinearVelocity = pc->maxLinearVelocity;
                desc.maxAngularVelocity = pc->maxAngularVelocity;
                desc.allowSleeping     = pc->allowSleeping;
                desc.motionQuality     = (pc->motionQuality == ECS::PhysicsComponent::MotionQuality::LinearCast)
                    ? IPhysicsBackend::BodyDesc::MotionQuality::LinearCast
                    : IPhysicsBackend::BodyDesc::MotionQuality::Discrete;
                std::memcpy(desc.velocity, pc->velocity, sizeof(desc.velocity));
                std::memcpy(desc.angularVelocity, pc->angularVelocity, sizeof(desc.angularVelocity));
            }
            else
            {
                // No PhysicsComponent → Kinematic so the collider follows the
                // entity transform and participates in overlap detection.
                desc.motionType    = IPhysicsBackend::BodyDesc::MotionType::Kinematic;
                desc.allowSleeping = false;
                desc.gravityFactor = 0.0f;
            }

            // Sensors with explicit Static motion type still need Kinematic
            // for overlap detection (two Static bodies never generate contacts).
            if (desc.isSensor && desc.motionType == IPhysicsBackend::BodyDesc::MotionType::Static)
            {
                desc.motionType    = IPhysicsBackend::BodyDesc::MotionType::Kinematic;
                desc.allowSleeping = false;
                desc.gravityFactor = 0.0f;
            }

            m_backend->createBody(desc);
            m_trackedEntities.insert(entity);
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
        if (handle != IPhysicsBackend::InvalidBody)
            m_backend->removeBody(handle);
        m_trackedEntities.erase(entity);
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
        if (handle == IPhysicsBackend::InvalidBody) continue;

        auto state = m_backend->getBodyState(handle);

        tc->position[0] = state.position[0];
        tc->position[1] = state.position[1];
        tc->position[2] = state.position[2];

        tc->rotation[0] = state.rotationEulerDeg[0];
        tc->rotation[1] = state.rotationEulerDeg[1];
        tc->rotation[2] = state.rotationEulerDeg[2];

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
    if (handle == IPhysicsBackend::InvalidBody) return false;

    return m_backend->isBodySleeping(handle);
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
        if (existingHandle != IPhysicsBackend::InvalidCharacter)
        {
            // Character already exists — nothing to update (position is managed by backend)
            continue;
        }

        // Create a new character in the backend
        IPhysicsBackend::CharacterDesc desc{};
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
        if (handle != IPhysicsBackend::InvalidCharacter)
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
        if (handle == IPhysicsBackend::InvalidCharacter) continue;

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
        if (handle == IPhysicsBackend::InvalidCharacter) continue;

        auto state = m_backend->getCharacterState(handle);

        tc->position[0] = state.position[0];
        tc->position[1] = state.position[1];
        tc->position[2] = state.position[2];

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
