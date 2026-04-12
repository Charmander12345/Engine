#pragma once

#include "Components.h"
#include "../Archive.h"

/// Free functions for bidirectional binary serialization of all ECS
/// components.  Each function uses the same Archive reference for both
/// save and load – the direction is determined by ar.isLoading().
///
/// Version tags enable forward-compatible deserialization when fields
/// are added in the future.

namespace ECS
{

// ── TransformComponent ──────────────────────────────────────────────

inline void serialize(Archive& ar, TransformComponent& c)
{
    ar.serializeVersion(1);
    ar.serializeFloatArray(c.position);
    ar.serializeFloatArray(c.rotation);
    ar.serializeFloatArray(c.scale);
    ar.serializeFloatArray(c.localPosition);
    ar.serializeFloatArray(c.localRotation);
    ar.serializeFloatArray(c.localScale);
    ar << c.parent;
    // children vector is rebuilt from parent links – not serialized
    c.dirty = false;
}

// ── MeshComponent ───────────────────────────────────────────────────

inline void serialize(Archive& ar, MeshComponent& c)
{
    ar.serializeVersion(1);
    ar << c.meshAssetPath;
}

// ── MaterialComponent ───────────────────────────────────────────────

inline void serialize(Archive& ar, MaterialOverrides& o)
{
    ar.serializeFloatArray(o.colorTint);
    ar << o.metallic << o.roughness << o.shininess << o.specularMultiplier;
    ar.serializeFloatArray(o.emissiveColor);
    ar << o.hasColorTint << o.hasMetallic << o.hasRoughness
       << o.hasShininess << o.hasSpecularMultiplier << o.hasEmissive;
}

inline void serialize(Archive& ar, MaterialComponent& c)
{
    ar.serializeVersion(1);
    ar << c.materialAssetPath;
    serialize(ar, c.overrides);
}

// ── LightComponent ──────────────────────────────────────────────────

inline void serialize(Archive& ar, LightComponent& c)
{
    ar.serializeVersion(1);
    int type = static_cast<int>(c.type);
    ar << type;
    if (ar.isLoading()) c.type = static_cast<LightComponent::LightType>(type);
    ar.serializeFloatArray(c.color);
    ar << c.intensity << c.range << c.spotAngle;
}

// ── CameraComponent ─────────────────────────────────────────────────

inline void serialize(Archive& ar, CameraComponent& c)
{
    ar.serializeVersion(1);
    ar << c.fov << c.nearClip << c.farClip << c.isActive;
}

// ── CollisionComponent ──────────────────────────────────────────────

inline void serialize(Archive& ar, CollisionComponent& c)
{
    ar.serializeVersion(1);
    int type = static_cast<int>(c.colliderType);
    ar << type;
    if (ar.isLoading()) c.colliderType = static_cast<CollisionComponent::ColliderType>(type);
    ar.serializeFloatArray(c.colliderSize);
    ar.serializeFloatArray(c.colliderOffset);
    ar << c.restitution << c.friction << c.isSensor;
}

// ── PhysicsComponent ────────────────────────────────────────────────

inline void serialize(Archive& ar, PhysicsComponent& c)
{
    ar.serializeVersion(1);
    int motionType = static_cast<int>(c.motionType);
    ar << motionType;
    if (ar.isLoading()) c.motionType = static_cast<PhysicsComponent::MotionType>(motionType);
    ar << c.mass << c.gravityFactor << c.linearDamping << c.angularDamping;
    ar << c.maxLinearVelocity << c.maxAngularVelocity;
    int motionQuality = static_cast<int>(c.motionQuality);
    ar << motionQuality;
    if (ar.isLoading()) c.motionQuality = static_cast<PhysicsComponent::MotionQuality>(motionQuality);
    ar << c.allowSleeping;
    ar.serializeFloatArray(c.velocity);
    ar.serializeFloatArray(c.angularVelocity);
}

// ── ConstraintComponent ─────────────────────────────────────────────

inline void serialize(Archive& ar, ConstraintComponent::ConstraintEntry& e)
{
    int type = static_cast<int>(e.type);
    ar << type;
    if (ar.isLoading()) e.type = static_cast<ConstraintComponent::ConstraintType>(type);
    ar << e.connectedEntity;
    ar.serializeFloatArray(e.anchor);
    ar.serializeFloatArray(e.connectedAnchor);
    ar.serializeFloatArray(e.axis);
    ar.serializeFloatArray(e.limits);
    ar << e.springStiffness << e.springDamping;
    ar << e.breakable << e.breakForce << e.breakTorque;
}

inline void serialize(Archive& ar, ConstraintComponent& c)
{
    ar.serializeVersion(1);
    uint32_t count = static_cast<uint32_t>(c.constraints.size());
    ar << count;
    if (ar.isLoading())
        c.constraints.resize(count);
    for (uint32_t i = 0; i < count; ++i)
        serialize(ar, c.constraints[i]);
}

// ── LogicComponent ──────────────────────────────────────────────────

inline void serialize(Archive& ar, LogicComponent& c)
{
    ar.serializeVersion(1);
    ar << c.scriptPath << c.nativeClassName;
}

// ── NameComponent ───────────────────────────────────────────────────

inline void serialize(Archive& ar, NameComponent& c)
{
    ar.serializeVersion(1);
    ar << c.displayName;
}

// ── HeightFieldComponent ────────────────────────────────────────────

inline void serialize(Archive& ar, HeightFieldComponent& c)
{
    ar.serializeVersion(1);
    ar << c.sampleCount;
    ar << c.offsetX << c.offsetY << c.offsetZ;
    ar << c.scaleX << c.scaleY << c.scaleZ;
    ar.serializeVector(c.heights);
}

// ── AnimationComponent ──────────────────────────────────────────────

inline void serialize(Archive& ar, AnimationComponent& c)
{
    ar.serializeVersion(1);
    ar << c.currentClipIndex << c.currentTime << c.speed;
    ar << c.playing << c.loop;
}

// ── LodComponent ────────────────────────────────────────────────────

inline void serialize(Archive& ar, LodComponent& c)
{
    ar.serializeVersion(1);
    uint32_t count = static_cast<uint32_t>(c.levels.size());
    ar << count;
    if (ar.isLoading())
        c.levels.resize(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        ar << c.levels[i].meshAssetPath;
        ar << c.levels[i].maxDistance;
    }
}

// ── CharacterControllerComponent ────────────────────────────────────

inline void serialize(Archive& ar, CharacterControllerComponent& c)
{
    ar.serializeVersion(1);
    ar << c.radius << c.height;
    ar << c.maxSlopeAngle << c.stepUpHeight << c.skinWidth;
    ar << c.gravityFactor << c.maxFallSpeed;
    // Runtime state (isGrounded, groundNormal, etc.) is not serialized
}

// ── AudioSourceComponent ────────────────────────────────────────────

inline void serialize(Archive& ar, AudioSourceComponent& c)
{
    ar.serializeVersion(1);
    ar << c.assetPath;
    ar << c.is3D << c.minDistance << c.maxDistance << c.rolloffFactor;
    ar << c.gain << c.loop << c.autoPlay;
    // runtimeHandle is not serialized
}

// ── ParticleEmitterComponent ────────────────────────────────────────

inline void serialize(Archive& ar, ParticleEmitterComponent& c)
{
    ar.serializeVersion(1);
    ar << c.maxParticles << c.emissionRate << c.lifetime;
    ar << c.speed << c.speedVariance;
    ar << c.size << c.sizeEnd;
    ar << c.gravity;
    ar << c.colorR << c.colorG << c.colorB << c.colorA;
    ar << c.colorEndR << c.colorEndG << c.colorEndB << c.colorEndA;
    ar << c.coneAngle;
    ar << c.enabled << c.loop;
    // emissionAccumulator is runtime-only, not serialized
}

} // namespace ECS
