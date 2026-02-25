#include "PhysicsWorld.h"

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
    m_gravity[0] = 0.0f;
    m_gravity[1] = -9.81f;
    m_gravity[2] = 0.0f;
    m_fixedTimestep = 1.0f / 60.0f;
    m_accumulator = m_fixedTimestep;
    m_sleepThreshold = 0.01f;
    m_sleepTime = 0.5f;
    m_collisionEvents.clear();
    m_activeOverlaps.clear();
    m_beginOverlapEvents.clear();
    m_endOverlapEvents.clear();
    m_initialized = true;
    Logger::Instance().log(Logger::Category::Engine, "PhysicsWorld initialised.", Logger::LogLevel::INFO);
}

void PhysicsWorld::shutdown()
{
    m_bodies.clear();
    m_contacts.clear();
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
}

void PhysicsWorld::getGravity(float& x, float& y, float& z) const
{
    x = m_gravity[0];
    y = m_gravity[1];
    z = m_gravity[2];
}

// ── Main step (fixed-timestep accumulator) ──────────────────────────────

void PhysicsWorld::step(float dt)
{
    if (!m_initialized) return;
    if (dt <= 0.0f) return;

    // Clamp to avoid spiral of death
    if (dt > 0.1f) dt = 0.1f;

    m_collisionEvents.clear();
    m_accumulator += dt;

    // Read from ECS once – sub-steps must accumulate on the same body state
    gatherBodies();

    while (m_accumulator >= m_fixedTimestep)
    {
        integrate(m_fixedTimestep);
        detectCollisions();
        resolveCollisions();
        m_accumulator -= m_fixedTimestep;
    }

    writeback();
    updateOverlapTracking();
    fireCollisionEvents();
}

// ── Gather rigid bodies from ECS ────────────────────────────────────────

void PhysicsWorld::gatherBodies()
{
    m_bodies.clear();

    auto& ecs = ECS::ECSManager::Instance();
    auto schema = ECS::Schema().require<ECS::TransformComponent>().require<ECS::PhysicsComponent>();
    auto entities = ecs.getEntitiesMatchingSchema(schema);

    m_bodies.reserve(entities.size());

    for (auto entity : entities)
    {
        const auto* tc = ecs.getComponent<ECS::TransformComponent>(entity);
        const auto* pc = ecs.getComponent<ECS::PhysicsComponent>(entity);
        if (!tc || !pc) continue;

        RigidBody rb{};
        rb.entity = entity;
        std::memcpy(rb.position, tc->position, sizeof(float) * 3);
        std::memcpy(rb.rotation, tc->rotation, sizeof(float) * 3);
        std::memcpy(rb.scale, tc->scale, sizeof(float) * 3);
        std::memcpy(rb.velocity, pc->velocity, sizeof(float) * 3);
        std::memcpy(rb.angularVelocity, pc->angularVelocity, sizeof(float) * 3);
        rb.mass = pc->mass;
        rb.invMass = (pc->isStatic || pc->isKinematic || pc->mass <= 0.0f) ? 0.0f : (1.0f / pc->mass);
        rb.restitution = pc->restitution;
        rb.friction = pc->friction;
        std::memcpy(rb.colliderSize, pc->colliderSize, sizeof(float) * 3);
        rb.colliderType = static_cast<int>(pc->colliderType);
        rb.isStatic = pc->isStatic;
        rb.useGravity = pc->useGravity;
        rb.isKinematic = pc->isKinematic;

        m_bodies.push_back(rb);
    }
}

// ── Integration (semi-implicit Euler) ───────────────────────────────────

void PhysicsWorld::integrate(float ts)
{
    for (auto& b : m_bodies)
    {
        if (b.isStatic || b.isKinematic) continue;
        if (b.isSleeping) continue;

        // Apply gravity
        if (b.useGravity)
        {
            b.velocity[0] += m_gravity[0] * ts;
            b.velocity[1] += m_gravity[1] * ts;
            b.velocity[2] += m_gravity[2] * ts;
        }

        // Update position
        b.position[0] += b.velocity[0] * ts;
        b.position[1] += b.velocity[1] * ts;
        b.position[2] += b.velocity[2] * ts;

        // Simple angular integration (degrees)
        b.rotation[0] += b.angularVelocity[0] * ts;
        b.rotation[1] += b.angularVelocity[1] * ts;
        b.rotation[2] += b.angularVelocity[2] * ts;

        // Angular damping (prevents infinite spinning)
        constexpr float kAngularDamping = 0.95f;
        b.angularVelocity[0] *= kAngularDamping;
        b.angularVelocity[1] *= kAngularDamping;
        b.angularVelocity[2] *= kAngularDamping;

        // Sleep check
        const float speedSq = b.velocity[0] * b.velocity[0]
                             + b.velocity[1] * b.velocity[1]
                             + b.velocity[2] * b.velocity[2];
        const float angSpeedSq = b.angularVelocity[0] * b.angularVelocity[0]
                               + b.angularVelocity[1] * b.angularVelocity[1]
                               + b.angularVelocity[2] * b.angularVelocity[2];
        const float threshSq = m_sleepThreshold * m_sleepThreshold;

        if (speedSq < threshSq && angSpeedSq < threshSq)
        {
            b.sleepTimer += ts;
            if (b.sleepTimer >= m_sleepTime)
            {
                b.isSleeping = true;
                b.velocity[0] = b.velocity[1] = b.velocity[2] = 0.0f;
                b.angularVelocity[0] = b.angularVelocity[1] = b.angularVelocity[2] = 0.0f;
            }
        }
        else
        {
            b.sleepTimer = 0.0f;
        }
    }
}

// ── Collision detection ─────────────────────────────────────────────────

static float vec3Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float vec3Length(const float v[3])
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void vec3Normalize(float v[3])
{
    const float len = vec3Length(v);
    if (len > 1e-8f)
    {
        v[0] /= len;
        v[1] /= len;
        v[2] /= len;
    }
}

static void mat3FromEuler(const float eulerDegrees[3], float R[3][3]) {
    constexpr float deg2rad = 3.14159265f / 180.0f;
    float p = eulerDegrees[0] * deg2rad; // pitch
    float y = eulerDegrees[1] * deg2rad; // yaw
    float r = eulerDegrees[2] * deg2rad; // roll

    float sp = sin(p), cp = cos(p);
    float sy = sin(y), cy = cos(y);
    float sr = sin(r), cr = cos(r);

    // Rotation order: Y(yaw) * X(pitch) * Z(roll)
    R[0][0] = cy * cr + sy * sp * sr;
    R[0][1] = cy * -sr + sy * sp * cr;
    R[0][2] = sy * cp;
    R[1][0] = cp * sr;
    R[1][1] = cp * cr;
    R[1][2] = -sp;
    R[2][0] = -sy * cr + cy * sp * sr;
    R[2][1] = sy * sr + cy * sp * cr;
    R[2][2] = cy * cp;
}

static void computeRotatedAABB(const float halfExtents[3], const float eulerDegrees[3], float outHE[3])
{
    float R[3][3];
    mat3FromEuler(eulerDegrees, R);

    // halfExtents are already half-extents (colliderSize * scale), do NOT halve again
    outHE[0] = halfExtents[0] * std::abs(R[0][0]) + halfExtents[1] * std::abs(R[0][1]) + halfExtents[2] * std::abs(R[0][2]);
    outHE[1] = halfExtents[0] * std::abs(R[1][0]) + halfExtents[1] * std::abs(R[1][1]) + halfExtents[2] * std::abs(R[1][2]);
    outHE[2] = halfExtents[0] * std::abs(R[2][0]) + halfExtents[1] * std::abs(R[2][1]) + halfExtents[2] * std::abs(R[2][2]);
}

static void vec3Cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

void PhysicsWorld::detectCollisions()
{
    m_contacts.clear();

    const int count = static_cast<int>(m_bodies.size());
    for (int i = 0; i < count; ++i)
    {
        for (int j = i + 1; j < count; ++j)
        {
            // Skip if both are static/kinematic
            if ((m_bodies[i].isStatic || m_bodies[i].isKinematic) &&
                (m_bodies[j].isStatic || m_bodies[j].isKinematic))
                continue;

            // Skip if both are sleeping
            if (m_bodies[i].isSleeping && m_bodies[j].isSleeping)
                continue;

            ContactPoint cp{};
            cp.bodyA = i;
            cp.bodyB = j;

            bool hit = false;
            // Mesh collider (2) falls back to Box (0) until mesh collision is implemented
            const int tA = (m_bodies[i].colliderType == 2) ? 0 : m_bodies[i].colliderType;
            const int tB = (m_bodies[j].colliderType == 2) ? 0 : m_bodies[j].colliderType;

            if (tA == 1 && tB == 1) // Sphere-Sphere
            {
                hit = testSphereSphere(m_bodies[i], m_bodies[j], cp);
            }
            else if (tA == 0 && tB == 0) // Box-Box (includes Mesh fallback)
            {
                hit = testBoxBox(m_bodies[i], m_bodies[j], cp);
            }
            else if (tA == 1 && tB == 0) // Sphere-Box
            {
                hit = testSphereBox(m_bodies[i], m_bodies[j], cp);
            }
            else if (tA == 0 && tB == 1) // Box-Sphere
            {
                hit = testSphereBox(m_bodies[j], m_bodies[i], cp);
                if (hit)
                {
                    // Swap so bodyA is i and bodyB is j, flip normal
                    cp.bodyA = i;
                    cp.bodyB = j;
                    cp.normal[0] = -cp.normal[0];
                    cp.normal[1] = -cp.normal[1];
                    cp.normal[2] = -cp.normal[2];
                }
            }

            if (hit)
            {
                m_contacts.push_back(cp);
            }
        }
    }
}

// ── Sphere-Sphere ───────────────────────────────────────────────────────

bool PhysicsWorld::testSphereSphere(const RigidBody& a, const RigidBody& b, ContactPoint& out) const
{
    const float rA = a.colliderSize[0] * std::max({ a.scale[0], a.scale[1], a.scale[2] });
    const float rB = b.colliderSize[0] * std::max({ b.scale[0], b.scale[1], b.scale[2] });

    float diff[3] = {
        b.position[0] - a.position[0],
        b.position[1] - a.position[1],
        b.position[2] - a.position[2]
    };
    const float dist = vec3Length(diff);
    const float sumR = rA + rB;

    if (dist >= sumR || dist < 1e-8f)
        return false;

    out.depth = sumR - dist;
    out.normal[0] = diff[0] / dist;
    out.normal[1] = diff[1] / dist;
    out.normal[2] = diff[2] / dist;

    out.contactPoint[0] = a.position[0] + out.normal[0] * rA;
    out.contactPoint[1] = a.position[1] + out.normal[1] * rA;
    out.contactPoint[2] = a.position[2] + out.normal[2] * rA;
    return true;
}

// ── OBB-OBB (Separating Axis Theorem) ───────────────────────────────────

bool PhysicsWorld::testBoxBox(const RigidBody& a, const RigidBody& b, ContactPoint& out) const
{
    // Half-extents (colliderSize is already half-extents, scaled by entity scale)
    const float eA[3] = { a.colliderSize[0] * a.scale[0],
                          a.colliderSize[1] * a.scale[1],
                          a.colliderSize[2] * a.scale[2] };
    const float eB[3] = { b.colliderSize[0] * b.scale[0],
                          b.colliderSize[1] * b.scale[1],
                          b.colliderSize[2] * b.scale[2] };

    // Rotation matrices (columns = local axes in world space)
    float RA[3][3], RB[3][3];
    mat3FromEuler(a.rotation, RA);
    mat3FromEuler(b.rotation, RB);

    // Translation from A centre to B centre in world space
    const float tw[3] = { b.position[0] - a.position[0],
                          b.position[1] - a.position[1],
                          b.position[2] - a.position[2] };

    // Translation in A's local frame
    const float T[3] = { tw[0]*RA[0][0] + tw[1]*RA[1][0] + tw[2]*RA[2][0],
                         tw[0]*RA[0][1] + tw[1]*RA[1][1] + tw[2]*RA[2][1],
                         tw[0]*RA[0][2] + tw[1]*RA[1][2] + tw[2]*RA[2][2] };

    // R[i][j] = dot(A_axis_i, B_axis_j) = (RA^T * RB)
    // AbsR with epsilon for numerical robustness on near-parallel edges
    float R[3][3], AbsR[3][3];
    constexpr float kEps = 1e-6f;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            R[i][j] = RA[0][i]*RB[0][j] + RA[1][i]*RB[1][j] + RA[2][i]*RB[2][j];
            AbsR[i][j] = std::abs(R[i][j]) + kEps;
        }

    float minPen = 1e30f;
    int bestIdx = -1;

    // Helper: test one separating axis, track minimum penetration
    auto testAxis = [&](float ra, float rb, float d, int idx) -> bool {
        float pen = (ra + rb) - std::abs(d);
        if (pen <= 0.0f) return false;
        if (pen < minPen) {
            minPen = pen;
            bestIdx = idx;
        }
        return true;
    };

    // --- A's 3 face normals (axes 0-2) ---
    if (!testAxis(eA[0], eB[0]*AbsR[0][0]+eB[1]*AbsR[0][1]+eB[2]*AbsR[0][2], T[0], 0)) return false;
    if (!testAxis(eA[1], eB[0]*AbsR[1][0]+eB[1]*AbsR[1][1]+eB[2]*AbsR[1][2], T[1], 1)) return false;
    if (!testAxis(eA[2], eB[0]*AbsR[2][0]+eB[1]*AbsR[2][1]+eB[2]*AbsR[2][2], T[2], 2)) return false;

    // --- B's 3 face normals (axes 3-5) ---
    if (!testAxis(eA[0]*AbsR[0][0]+eA[1]*AbsR[1][0]+eA[2]*AbsR[2][0], eB[0],
                  T[0]*R[0][0]+T[1]*R[1][0]+T[2]*R[2][0], 3)) return false;
    if (!testAxis(eA[0]*AbsR[0][1]+eA[1]*AbsR[1][1]+eA[2]*AbsR[2][1], eB[1],
                  T[0]*R[0][1]+T[1]*R[1][1]+T[2]*R[2][1], 4)) return false;
    if (!testAxis(eA[0]*AbsR[0][2]+eA[1]*AbsR[1][2]+eA[2]*AbsR[2][2], eB[2],
                  T[0]*R[0][2]+T[1]*R[1][2]+T[2]*R[2][2], 5)) return false;

    // --- 9 edge-edge cross product axes (axes 6-14) ---
    // For A_i x B_j:  i1=(i+1)%3, i2=(i+2)%3,  j1=(j+1)%3, j2=(j+2)%3
    //   ra = eA[i1]*AbsR[i2][j] + eA[i2]*AbsR[i1][j]
    //   rb = eB[j1]*AbsR[i][j2] + eB[j2]*AbsR[i][j1]
    //   d  = T[i2]*R[i1][j] - T[i1]*R[i2][j]
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            const int i1 = (i+1)%3, i2 = (i+2)%3;
            const int j1 = (j+1)%3, j2 = (j+2)%3;
            const float ra = eA[i1]*AbsR[i2][j] + eA[i2]*AbsR[i1][j];
            const float rb = eB[j1]*AbsR[i][j2] + eB[j2]*AbsR[i][j1];
            const float d  = T[i2]*R[i1][j] - T[i1]*R[i2][j];
            const float pen = (ra + rb) - std::abs(d);
            if (pen <= 0.0f) return false; // separating axis found

            // Only consider for contact normal if cross product is non-degenerate
            const float lenSq = 1.0f - R[i][j]*R[i][j];
            if (lenSq > 1e-6f) {
                const float normalizedPen = pen / std::sqrt(lenSq);
                if (normalizedPen < minPen) {
                    minPen = normalizedPen;
                    bestIdx = 6 + i*3 + j;
                }
            }
        }
    }

    // Recover collision normal in world space
    float n[3] = {0, 0, 0};
    if (bestIdx < 3) {
        // A's face normal (column bestIdx of RA)
        n[0] = RA[0][bestIdx]; n[1] = RA[1][bestIdx]; n[2] = RA[2][bestIdx];
    } else if (bestIdx < 6) {
        // B's face normal
        const int j = bestIdx - 3;
        n[0] = RB[0][j]; n[1] = RB[1][j]; n[2] = RB[2][j];
    } else {
        // Edge-edge: A_i x B_j
        const int i = (bestIdx - 6) / 3;
        const int j = (bestIdx - 6) % 3;
        const float ai[3] = { RA[0][i], RA[1][i], RA[2][i] };
        const float bj[3] = { RB[0][j], RB[1][j], RB[2][j] };
        vec3Cross(ai, bj, n);
        vec3Normalize(n);
    }

    // Ensure normal points from A to B
    const float dot = n[0]*tw[0] + n[1]*tw[1] + n[2]*tw[2];
    if (dot < 0.0f) {
        n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];
    }

    out.depth = minPen;
    out.normal[0] = n[0]; out.normal[1] = n[1]; out.normal[2] = n[2];

    // --- Compute contact point on the collision surface ---
    if (bestIdx < 3) {
        // Contact on A's face (axis bestIdx toward B)
        const int fi = bestIdx;
        const float faceSign = (T[fi] >= 0.0f) ? 1.0f : -1.0f;
        float localCP[3];
        localCP[fi] = faceSign * eA[fi];
        for (int k = 0; k < 3; ++k)
            if (k != fi) localCP[k] = std::clamp(T[k], -eA[k], eA[k]);
        out.contactPoint[0] = a.position[0] + RA[0][0]*localCP[0] + RA[0][1]*localCP[1] + RA[0][2]*localCP[2];
        out.contactPoint[1] = a.position[1] + RA[1][0]*localCP[0] + RA[1][1]*localCP[1] + RA[1][2]*localCP[2];
        out.contactPoint[2] = a.position[2] + RA[2][0]*localCP[0] + RA[2][1]*localCP[1] + RA[2][2]*localCP[2];
    } else if (bestIdx < 6) {
        // Contact on B's face (axis bestIdx-3 toward A)
        const int fj = bestIdx - 3;
        const float localA[3] = {
            -tw[0]*RB[0][0] - tw[1]*RB[1][0] - tw[2]*RB[2][0],
            -tw[0]*RB[0][1] - tw[1]*RB[1][1] - tw[2]*RB[2][1],
            -tw[0]*RB[0][2] - tw[1]*RB[1][2] - tw[2]*RB[2][2]
        };
        const float faceSign = (localA[fj] >= 0.0f) ? 1.0f : -1.0f;
        float localCP[3];
        localCP[fj] = faceSign * eB[fj];
        for (int k = 0; k < 3; ++k)
            if (k != fj) localCP[k] = std::clamp(localA[k], -eB[k], eB[k]);
        out.contactPoint[0] = b.position[0] + RB[0][0]*localCP[0] + RB[0][1]*localCP[1] + RB[0][2]*localCP[2];
        out.contactPoint[1] = b.position[1] + RB[1][0]*localCP[0] + RB[1][1]*localCP[1] + RB[1][2]*localCP[2];
        out.contactPoint[2] = b.position[2] + RB[2][0]*localCP[0] + RB[2][1]*localCP[1] + RB[2][2]*localCP[2];
    } else {
        // Edge-edge: closest points on the two edges
        const int ei = (bestIdx - 6) / 3;
        const int ej = (bestIdx - 6) % 3;
        // Vertex of A closest to B (in A's local frame, zeroed on edge axis)
        float vA[3];
        for (int k = 0; k < 3; ++k)
            vA[k] = (k == ei) ? 0.0f : ((T[k] > 0.0f) ? eA[k] : -eA[k]);
        const float localA[3] = {
            -tw[0]*RB[0][0] - tw[1]*RB[1][0] - tw[2]*RB[2][0],
            -tw[0]*RB[0][1] - tw[1]*RB[1][1] - tw[2]*RB[2][1],
            -tw[0]*RB[0][2] - tw[1]*RB[1][2] - tw[2]*RB[2][2]
        };
        float vB[3];
        for (int k = 0; k < 3; ++k)
            vB[k] = (k == ej) ? 0.0f : ((localA[k] > 0.0f) ? eB[k] : -eB[k]);
        // Edge base points in world
        float pA[3] = {
            a.position[0] + RA[0][0]*vA[0] + RA[0][1]*vA[1] + RA[0][2]*vA[2],
            a.position[1] + RA[1][0]*vA[0] + RA[1][1]*vA[1] + RA[1][2]*vA[2],
            a.position[2] + RA[2][0]*vA[0] + RA[2][1]*vA[1] + RA[2][2]*vA[2]
        };
        float pB[3] = {
            b.position[0] + RB[0][0]*vB[0] + RB[0][1]*vB[1] + RB[0][2]*vB[2],
            b.position[1] + RB[1][0]*vB[0] + RB[1][1]*vB[1] + RB[1][2]*vB[2],
            b.position[2] + RB[2][0]*vB[0] + RB[2][1]*vB[1] + RB[2][2]*vB[2]
        };
        const float dA[3] = { RA[0][ei], RA[1][ei], RA[2][ei] };
        const float dB[3] = { RB[0][ej], RB[1][ej], RB[2][ej] };
        float w[3] = { pA[0]-pB[0], pA[1]-pB[1], pA[2]-pB[2] };
        const float aa = vec3Dot(dA, dA);
        const float ab = vec3Dot(dA, dB);
        const float bb = vec3Dot(dB, dB);
        const float aw = vec3Dot(dA, w);
        const float bw = vec3Dot(dB, w);
        const float denom = aa * bb - ab * ab;
        float s = 0.0f, t = 0.0f;
        if (std::abs(denom) > 1e-6f) {
            s = std::clamp((ab*bw - bb*aw) / denom, -eA[ei], eA[ei]);
            t = std::clamp((aa*bw - ab*aw) / denom, -eB[ej], eB[ej]);
        }
        out.contactPoint[0] = 0.5f*(pA[0]+s*dA[0] + pB[0]+t*dB[0]);
        out.contactPoint[1] = 0.5f*(pA[1]+s*dA[1] + pB[1]+t*dB[1]);
        out.contactPoint[2] = 0.5f*(pA[2]+s*dA[2] + pB[2]+t*dB[2]);
    }

    return true;
}

// ── Sphere-Box (OBB-aware) ──────────────────────────────────────────────

bool PhysicsWorld::testSphereBox(const RigidBody& sphere, const RigidBody& box, ContactPoint& out) const
{
    const float radius = sphere.colliderSize[0] * std::max({ sphere.scale[0], sphere.scale[1], sphere.scale[2] });
    const float he[3] = { box.colliderSize[0] * box.scale[0],
                          box.colliderSize[1] * box.scale[1],
                          box.colliderSize[2] * box.scale[2] };

    // Box rotation matrix
    float R[3][3];
    mat3FromEuler(box.rotation, R);

    // Sphere centre relative to box centre in world space
    const float dw[3] = { sphere.position[0] - box.position[0],
                          sphere.position[1] - box.position[1],
                          sphere.position[2] - box.position[2] };

    // Transform into box local frame: R^T * dw
    const float local[3] = { dw[0]*R[0][0] + dw[1]*R[1][0] + dw[2]*R[2][0],
                             dw[0]*R[0][1] + dw[1]*R[1][1] + dw[2]*R[2][1],
                             dw[0]*R[0][2] + dw[1]*R[1][2] + dw[2]*R[2][2] };

    // Closest point on OBB in local frame
    const float closest[3] = { std::clamp(local[0], -he[0], he[0]),
                               std::clamp(local[1], -he[1], he[1]),
                               std::clamp(local[2], -he[2], he[2]) };

    // Difference in local frame (from closest point to sphere centre)
    const float diff[3] = { local[0] - closest[0],
                            local[1] - closest[1],
                            local[2] - closest[2] };
    const float distSq = diff[0]*diff[0] + diff[1]*diff[1] + diff[2]*diff[2];

    if (distSq >= radius * radius || distSq < 1e-12f)
        return false;

    const float dist = std::sqrt(distSq);
    out.depth = radius - dist;

    // Normal in local frame (from box surface toward sphere centre)
    const float localNormal[3] = { diff[0] / dist, diff[1] / dist, diff[2] / dist };

    // Transform normal back to world frame: R * localNormal
    // Normal should point from A (sphere) to B (box), so negate
    out.normal[0] = -(R[0][0]*localNormal[0] + R[0][1]*localNormal[1] + R[0][2]*localNormal[2]);
    out.normal[1] = -(R[1][0]*localNormal[0] + R[1][1]*localNormal[1] + R[1][2]*localNormal[2]);
    out.normal[2] = -(R[2][0]*localNormal[0] + R[2][1]*localNormal[1] + R[2][2]*localNormal[2]);

    // Contact point in world frame: box centre + R * closest
    out.contactPoint[0] = box.position[0] + R[0][0]*closest[0] + R[0][1]*closest[1] + R[0][2]*closest[2];
    out.contactPoint[1] = box.position[1] + R[1][0]*closest[0] + R[1][1]*closest[1] + R[1][2]*closest[2];
    out.contactPoint[2] = box.position[2] + R[2][0]*closest[0] + R[2][1]*closest[1] + R[2][2]*closest[2];
    return true;
}

// ── Impulse-based collision resolution ──────────────────────────────────

void PhysicsWorld::resolveCollisions()
{
    for (auto& c : m_contacts)
    {
        auto& a = m_bodies[c.bodyA];
        auto& b = m_bodies[c.bodyB];

        // Wake sleeping bodies on contact
        if (a.isSleeping) { a.isSleeping = false; a.sleepTimer = 0.0f; }
        if (b.isSleeping) { b.isSleeping = false; b.sleepTimer = 0.0f; }

        // Store collision event
        CollisionEvent ev{};
        ev.entityA = a.entity;
        ev.entityB = b.entity;
        std::memcpy(ev.normal, c.normal, sizeof(float) * 3);
        ev.depth = c.depth;
        std::memcpy(ev.contactPoint, c.contactPoint, sizeof(float) * 3);
        m_collisionEvents.push_back(ev);

        const float invMassSum = a.invMass + b.invMass;
        if (invMassSum <= 0.0f) continue;

        // Positional correction (prevent sinking)
        constexpr float kSlop = 0.005f;
        constexpr float kCorrection = 0.8f;
        const float corrMag = std::max(c.depth - kSlop, 0.0f) * kCorrection / invMassSum;
        for (int k = 0; k < 3; ++k)
        {
            a.position[k] -= c.normal[k] * corrMag * a.invMass;
            b.position[k] += c.normal[k] * corrMag * b.invMass;
        }

        // --- Rotational Impulse Calculation ---

        // Relative position vectors from CoM to contact point
        float rA[3] = { c.contactPoint[0] - a.position[0], c.contactPoint[1] - a.position[1], c.contactPoint[2] - a.position[2] };
        float rB[3] = { c.contactPoint[0] - b.position[0], c.contactPoint[1] - b.position[1], c.contactPoint[2] - b.position[2] };

        // Angular velocity in rad/s (engine uses deg/s)
        constexpr float deg2rad = 3.14159265f / 180.0f;
        float wA_rad[3] = { a.angularVelocity[0] * deg2rad, a.angularVelocity[1] * deg2rad, a.angularVelocity[2] * deg2rad };
        float wB_rad[3] = { b.angularVelocity[0] * deg2rad, b.angularVelocity[1] * deg2rad, b.angularVelocity[2] * deg2rad };

        // Relative velocity at contact point
        float vA_contact[3], vB_contact[3];
        vec3Cross(wA_rad, rA, vA_contact);
        vec3Cross(wB_rad, rB, vB_contact);
        for (int k=0; k<3; ++k) {
            vA_contact[k] += a.velocity[k];
            vB_contact[k] += b.velocity[k];
        }
        float relVel[3] = { vB_contact[0] - vA_contact[0], vB_contact[1] - vA_contact[1], vB_contact[2] - vA_contact[2] };
        
        const float velAlongNormal = vec3Dot(relVel, c.normal);

        // Don't resolve if separating
        if (velAlongNormal > 0.0f) continue;

        // Restitution
        const float e = std::min(a.restitution, b.restitution);

        // Simplified scalar inverse inertia
        auto getInvInertiaScalar = [](const RigidBody& rb) {
            if (rb.isStatic || rb.isKinematic || rb.mass <= 0.0f) return 0.0f;

            if (rb.colliderType == 1) // Sphere: I = (2/5) * m * r^2
            {
                // colliderSize[0] IS the radius for spheres
                const float r = rb.colliderSize[0] * std::max({ rb.scale[0], rb.scale[1], rb.scale[2] });
                const float I = 0.4f * rb.mass * r * r;
                return (I > 1e-6f) ? 1.0f / I : 0.0f;
            }
            else // Box: I = (1/12) * m * (W^2 + H^2 + D^2) with full widths
            {
                // colliderSize is half-extents; full width = 2 * half-extent * scale
                const float w = rb.colliderSize[0] * rb.scale[0] * 2.0f;
                const float h = rb.colliderSize[1] * rb.scale[1] * 2.0f;
                const float d = rb.colliderSize[2] * rb.scale[2] * 2.0f;
                const float I = (1.0f/12.0f) * rb.mass * (w*w + h*h + d*d);
                return (I > 1e-6f) ? 1.0f / I : 0.0f;
            }
        };
        const float invInertiaA = getInvInertiaScalar(a);
        const float invInertiaB = getInvInertiaScalar(b);

        // Denominator for impulse calculation
        float rACrossN[3], rBCrossN[3];
        vec3Cross(rA, c.normal, rACrossN);
        vec3Cross(rB, c.normal, rBCrossN);
        const float angFactorA = invInertiaA * vec3Dot(rACrossN, rACrossN);
        const float angFactorB = invInertiaB * vec3Dot(rBCrossN, rBCrossN);
        const float denominator = invMassSum + angFactorA + angFactorB;
        if (std::abs(denominator) < 1e-6f) continue;

        // Impulse magnitude
        const float j = -(1.0f + e) * velAlongNormal / denominator;
        float impulse[3] = { c.normal[0] * j, c.normal[1] * j, c.normal[2] * j };

        // Apply linear impulse
        for (int k = 0; k < 3; ++k) {
            a.velocity[k] -= impulse[k] * a.invMass;
            b.velocity[k] += impulse[k] * b.invMass;
        }

        // Apply angular impulse
        constexpr float rad2deg = 180.0f / 3.14159265f;
        float torqueA[3], torqueB[3];
        vec3Cross(rA, impulse, torqueA);
        vec3Cross(rB, impulse, torqueB);
        
        a.angularVelocity[0] -= invInertiaA * torqueA[0] * rad2deg;
        a.angularVelocity[1] -= invInertiaA * torqueA[1] * rad2deg;
        a.angularVelocity[2] -= invInertiaA * torqueA[2] * rad2deg;

        b.angularVelocity[0] += invInertiaB * torqueB[0] * rad2deg;
        b.angularVelocity[1] += invInertiaB * torqueB[1] * rad2deg;
        b.angularVelocity[2] += invInertiaB * torqueB[2] * rad2deg;

        // Friction impulse (tangential)
        float tangent[3] = {
            relVel[0] - c.normal[0] * velAlongNormal,
            relVel[1] - c.normal[1] * velAlongNormal,
            relVel[2] - c.normal[2] * velAlongNormal
        };
        const float tangentLen = vec3Length(tangent);
        if (tangentLen > 1e-8f)
        {
            tangent[0] /= tangentLen;
            tangent[1] /= tangentLen;
            tangent[2] /= tangentLen;

            const float velAlongTangent = vec3Dot(relVel, tangent);

            float rACrossT[3], rBCrossT[3];
            vec3Cross(rA, tangent, rACrossT);
            vec3Cross(rB, tangent, rBCrossT);
            const float angFactorTA = invInertiaA * vec3Dot(rACrossT, rACrossT);
            const float angFactorTB = invInertiaB * vec3Dot(rBCrossT, rBCrossT);
            const float frictionDenominator = invMassSum + angFactorTA + angFactorTB;
            if (std::abs(frictionDenominator) < 1e-6f) continue;

            const float jt = -velAlongTangent / frictionDenominator;
            const float mu = std::sqrt(a.friction * b.friction);
            const float frictionJ = std::clamp(jt, -std::abs(j) * mu, std::abs(j) * mu);
            
            float frictionImpulse[3] = { tangent[0] * frictionJ, tangent[1] * frictionJ, tangent[2] * frictionJ };

            for (int k = 0; k < 3; ++k)
            {
                a.velocity[k] -= frictionImpulse[k] * a.invMass;
                b.velocity[k] += frictionImpulse[k] * b.invMass;
            }

            vec3Cross(rA, frictionImpulse, torqueA);
            vec3Cross(rB, frictionImpulse, torqueB);

            a.angularVelocity[0] -= invInertiaA * torqueA[0] * rad2deg;
            a.angularVelocity[1] -= invInertiaA * torqueA[1] * rad2deg;
            a.angularVelocity[2] -= invInertiaA * torqueA[2] * rad2deg;

            b.angularVelocity[0] += invInertiaB * torqueB[0] * rad2deg;
            b.angularVelocity[1] += invInertiaB * torqueB[1] * rad2deg;
            b.angularVelocity[2] += invInertiaB * torqueB[2] * rad2deg;
        }
    }
}

// ── Write simulation results back to ECS ────────────────────────────────

void PhysicsWorld::writeback()
{
    auto& ecs = ECS::ECSManager::Instance();

    for (auto& b : m_bodies)
    {
        if (b.isStatic || b.isKinematic) continue;

        auto* tc = ecs.getComponent<ECS::TransformComponent>(b.entity);
        auto* pc = ecs.getComponent<ECS::PhysicsComponent>(b.entity);
        if (!tc || !pc) continue;

        std::memcpy(tc->position, b.position, sizeof(float) * 3);
        std::memcpy(tc->rotation, b.rotation, sizeof(float) * 3);
        std::memcpy(pc->velocity, b.velocity, sizeof(float) * 3);
        std::memcpy(pc->angularVelocity, b.angularVelocity, sizeof(float) * 3);
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

    // Build current set of overlapping pairs from collision events
    std::set<std::pair<uint32_t, uint32_t>> currentOverlaps;
    for (const auto& ev : m_collisionEvents)
    {
        auto pair = std::minmax(ev.entityA, ev.entityB);
        currentOverlaps.insert(pair);
    }

    // Begin overlap: in current but not in previous
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

    // End overlap: in previous but not in current
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
    for (const auto& b : m_bodies)
    {
        if (b.entity == entity)
            return b.isSleeping;
    }
    return false;
}

// ── Raycast ─────────────────────────────────────────────────────────────

bool PhysicsWorld::rayTestBox(const float origin[3], const float dir[3], float maxDist,
                              const RigidBody& body, float& outDist, float outNormal[3]) const
{
    const float localHE[3] = { body.colliderSize[0] * body.scale[0], body.colliderSize[1] * body.scale[1], body.colliderSize[2] * body.scale[2] };
    float he[3];
    computeRotatedAABB(localHE, body.rotation, he);

    const float minB[3] = { body.position[0] - he[0], body.position[1] - he[1], body.position[2] - he[2] };
    const float maxB[3] = { body.position[0] + he[0], body.position[1] + he[1], body.position[2] + he[2] };

    float tmin = -1e30f;
    float tmax = 1e30f;
    int hitAxis = -1;
    float hitSign = 1.0f;

    for (int i = 0; i < 3; ++i)
    {
        if (std::abs(dir[i]) < 1e-8f)
        {
            if (origin[i] < minB[i] || origin[i] > maxB[i])
                return false;
        }
        else
        {
            float invD = 1.0f / dir[i];
            float t1 = (minB[i] - origin[i]) * invD;
            float t2 = (maxB[i] - origin[i]) * invD;
            float sign = -1.0f;
            if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }
            if (t1 > tmin) { tmin = t1; hitAxis = i; hitSign = sign; }
            if (t2 < tmax) { tmax = t2; }
            if (tmin > tmax) return false;
        }
    }

    if (tmin < 0.0f || tmin > maxDist) return false;

    outDist = tmin;
    outNormal[0] = outNormal[1] = outNormal[2] = 0.0f;
    if (hitAxis >= 0) outNormal[hitAxis] = hitSign;
    return true;
}

bool PhysicsWorld::rayTestSphere(const float origin[3], const float dir[3], float maxDist,
                                 const RigidBody& body, float& outDist, float outNormal[3]) const
{
    const float radius = body.colliderSize[0] * std::max({ body.scale[0], body.scale[1], body.scale[2] });
    const float oc[3] = {
        origin[0] - body.position[0],
        origin[1] - body.position[1],
        origin[2] - body.position[2]
    };

    const float a = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
    const float b = 2.0f * (oc[0] * dir[0] + oc[1] * dir[1] + oc[2] * dir[2]);
    const float c = oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2] - radius * radius;
    const float disc = b * b - 4.0f * a * c;

    if (disc < 0.0f) return false;

    const float sqrtDisc = std::sqrt(disc);
    float t = (-b - sqrtDisc) / (2.0f * a);
    if (t < 0.0f) t = (-b + sqrtDisc) / (2.0f * a);
    if (t < 0.0f || t > maxDist) return false;

    outDist = t;
    const float hitPt[3] = {
        origin[0] + dir[0] * t - body.position[0],
        origin[1] + dir[1] * t - body.position[1],
        origin[2] + dir[2] * t - body.position[2]
    };
    const float len = std::sqrt(hitPt[0] * hitPt[0] + hitPt[1] * hitPt[1] + hitPt[2] * hitPt[2]);
    if (len > 1e-8f)
    {
        outNormal[0] = hitPt[0] / len;
        outNormal[1] = hitPt[1] / len;
        outNormal[2] = hitPt[2] / len;
    }
    else
    {
        outNormal[0] = outNormal[1] = outNormal[2] = 0.0f;
    }
    return true;
}

PhysicsWorld::RaycastHit PhysicsWorld::raycast(float ox, float oy, float oz,
                                               float dx, float dy, float dz,
                                               float maxDist) const
{
    RaycastHit result{};

    // Normalize direction
    float dir[3] = { dx, dy, dz };
    const float dirLen = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (dirLen < 1e-8f) return result;
    dir[0] /= dirLen; dir[1] /= dirLen; dir[2] /= dirLen;

    const float origin[3] = { ox, oy, oz };
    float closestDist = maxDist + 1.0f;

    for (const auto& body : m_bodies)
    {
        float dist = 0.0f;
        float normal[3] = {};
        bool hit = false;

        const int ct = (body.colliderType == 2) ? 0 : body.colliderType;
        if (ct == 0) // Box (includes Mesh fallback)
            hit = rayTestBox(origin, dir, maxDist, body, dist, normal);
        else if (ct == 1) // Sphere
            hit = rayTestSphere(origin, dir, maxDist, body, dist, normal);

        if (hit && dist < closestDist)
        {
            closestDist = dist;
            result.entity = body.entity;
            result.point[0] = origin[0] + dir[0] * dist;
            result.point[1] = origin[1] + dir[1] * dist;
            result.point[2] = origin[2] + dir[2] * dist;
            result.normal[0] = normal[0];
            result.normal[1] = normal[1];
            result.normal[2] = normal[2];
            result.distance = dist;
            result.hit = true;
        }
    }

    return result;
}
