#pragma once

/// Reflection registration for all ECS component types.
/// Include this file in exactly ONE translation unit (e.g. ECS.cpp)
/// so the static registrar variables run before main().

#include "../Reflection.h"
#include "Components.h"

namespace ECS {

// ═══════════════════════════════════════════════════════════════════════
// Enum tables — static arrays referenced by REFLECT_ENUM properties
// ═══════════════════════════════════════════════════════════════════════

inline constexpr EnumEntry LightType_Entries[] = {
    { "Point",       0 },
    { "Directional", 1 },
    { "Spot",        2 }
};

inline constexpr EnumEntry ColliderType_Entries[] = {
    { "Box",         0 },
    { "Sphere",      1 },
    { "Capsule",     2 },
    { "Cylinder",    3 },
    { "Mesh",        4 },
    { "HeightField", 5 }
};

inline constexpr EnumEntry MotionType_Entries[] = {
    { "Static",    0 },
    { "Kinematic", 1 },
    { "Dynamic",   2 }
};

inline constexpr EnumEntry MotionQuality_Entries[] = {
    { "Discrete",         0 },
    { "LinearCast (CCD)", 1 }
};

inline constexpr EnumEntry ConstraintType_Entries[] = {
    { "Hinge",       0 },
    { "Ball Socket", 1 },
    { "Fixed",       2 },
    { "Slider",      3 },
    { "Distance",    4 },
    { "Spring",      5 },
    { "Cone",        6 }
};

// ═══════════════════════════════════════════════════════════════════════
// TransformComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(TransformComponent, "Transform")
    REFLECT_VEC3  (position,      "World",     PF_EditAnywhere)
    REFLECT_VEC3  (rotation,      "World",     PF_EditAnywhere)
    REFLECT_VEC3  (scale,         "World",     PF_EditAnywhere)
    REFLECT_VEC3  (localPosition, "Local",     PF_EditAnywhere)
    REFLECT_VEC3  (localRotation, "Local",     PF_EditAnywhere)
    REFLECT_VEC3  (localScale,    "Local",     PF_EditAnywhere)
    REFLECT_ENTITY_REF(parent,    "Hierarchy", PF_VisibleOnly)
    REFLECT_BOOL  (dirty,         "",          PF_Transient | PF_Hidden)
REFLECT_END(TransformComponent)

// ═══════════════════════════════════════════════════════════════════════
// MeshComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(MeshComponent, "Mesh")
    REFLECT_ASSET_PATH(meshAssetPath, "Asset", PF_EditAnywhere)
    REFLECT_INT       (meshAssetId,   "Asset", PF_VisibleOnly)
REFLECT_END(MeshComponent)

// ═══════════════════════════════════════════════════════════════════════
// MaterialOverrides
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(MaterialOverrides, "Material Overrides")
    REFLECT_COLOR3(colorTint,          "Overrides", PF_EditAnywhere)
    REFLECT_FLOAT (metallic,           "Overrides", PF_EditAnywhere)
    REFLECT_FLOAT (roughness,          "Overrides", PF_EditAnywhere)
    REFLECT_FLOAT (shininess,          "Overrides", PF_EditAnywhere)
    REFLECT_FLOAT (specularMultiplier, "Overrides", PF_EditAnywhere)
    REFLECT_COLOR3(emissiveColor,      "Overrides", PF_EditAnywhere)
    REFLECT_BOOL  (hasColorTint,       "Flags",     PF_Hidden)
    REFLECT_BOOL  (hasMetallic,        "Flags",     PF_Hidden)
    REFLECT_BOOL  (hasRoughness,       "Flags",     PF_Hidden)
    REFLECT_BOOL  (hasShininess,       "Flags",     PF_Hidden)
    REFLECT_BOOL  (hasSpecularMultiplier, "Flags",  PF_Hidden)
    REFLECT_BOOL  (hasEmissive,        "Flags",     PF_Hidden)
REFLECT_END(MaterialOverrides)

// ═══════════════════════════════════════════════════════════════════════
// MaterialComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(MaterialComponent, "Material")
    REFLECT_ASSET_PATH(materialAssetPath, "Asset", PF_EditAnywhere)
    REFLECT_INT       (materialAssetId,   "Asset", PF_VisibleOnly)
REFLECT_END(MaterialComponent)

// ═══════════════════════════════════════════════════════════════════════
// LightComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(LightComponent, "Light")
    REFLECT_ENUM  (type,      "Light", PF_EditAnywhere, LightType_Entries, 3)
    REFLECT_COLOR3(color,     "Light", PF_EditAnywhere)
    REFLECT_FLOAT (intensity, "Light", PF_EditAnywhere)
    REFLECT_FLOAT (range,     "Light", PF_EditAnywhere)
    REFLECT_FLOAT_CLAMPED(spotAngle, "Light", PF_EditAnywhere, 0.0f, 180.0f)
REFLECT_END(LightComponent)

// ═══════════════════════════════════════════════════════════════════════
// CameraComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(CameraComponent, "Camera")
    REFLECT_FLOAT_CLAMPED(fov,     "Camera", PF_EditAnywhere, 1.0f, 179.0f)
    REFLECT_FLOAT(nearClip,        "Camera", PF_EditAnywhere)
    REFLECT_FLOAT(farClip,         "Camera", PF_EditAnywhere)
    REFLECT_BOOL (isActive,        "Camera", PF_EditAnywhere)
REFLECT_END(CameraComponent)

// ═══════════════════════════════════════════════════════════════════════
// CollisionComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(CollisionComponent, "Collision")
    REFLECT_ENUM  (colliderType,   "Shape",    PF_EditAnywhere, ColliderType_Entries, 6)
    REFLECT_VEC3  (colliderSize,   "Shape",    PF_EditAnywhere)
    REFLECT_VEC3  (colliderOffset, "Shape",    PF_EditAnywhere)
    REFLECT_FLOAT (restitution,    "Material", PF_EditAnywhere)
    REFLECT_FLOAT (friction,       "Material", PF_EditAnywhere)
    REFLECT_BOOL  (isSensor,       "Flags",    PF_EditAnywhere)
REFLECT_END(CollisionComponent)

// ═══════════════════════════════════════════════════════════════════════
// PhysicsComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(PhysicsComponent, "Physics")
    REFLECT_ENUM  (motionType,       "Motion",  PF_EditAnywhere, MotionType_Entries, 3)
    REFLECT_FLOAT (mass,             "Body",    PF_EditAnywhere)
    REFLECT_FLOAT (gravityFactor,    "Body",    PF_EditAnywhere)
    REFLECT_FLOAT (linearDamping,    "Damping", PF_EditAnywhere)
    REFLECT_FLOAT (angularDamping,   "Damping", PF_EditAnywhere)
    REFLECT_FLOAT (maxLinearVelocity,  "Limits", PF_EditAnywhere)
    REFLECT_FLOAT (maxAngularVelocity, "Limits", PF_EditAnywhere)
    REFLECT_ENUM  (motionQuality,    "Motion",  PF_EditAnywhere, MotionQuality_Entries, 2)
    REFLECT_BOOL  (allowSleeping,    "Body",    PF_EditAnywhere)
    REFLECT_VEC3  (velocity,         "State",   PF_EditAnywhere)
    REFLECT_VEC3  (angularVelocity,  "State",   PF_EditAnywhere)
REFLECT_END(PhysicsComponent)

// ═══════════════════════════════════════════════════════════════════════
// LogicComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(LogicComponent, "Logic")
    REFLECT_ASSET_PATH(scriptPath,      "Script", PF_EditAnywhere)
    REFLECT_STRING    (nativeClassName,  "Script", PF_EditAnywhere)
    REFLECT_INT       (scriptAssetId,   "Script", PF_VisibleOnly)
REFLECT_END(LogicComponent)

// ═══════════════════════════════════════════════════════════════════════
// NameComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(NameComponent, "Name")
    REFLECT_STRING(displayName, "General", PF_EditAnywhere)
REFLECT_END(NameComponent)

// ═══════════════════════════════════════════════════════════════════════
// HeightFieldComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(HeightFieldComponent, "Height Field")
    REFLECT_INT  (sampleCount, "Data",   PF_VisibleOnly)
    REFLECT_FLOAT(offsetX,     "Offset", PF_EditAnywhere)
    REFLECT_FLOAT(offsetY,     "Offset", PF_EditAnywhere)
    REFLECT_FLOAT(offsetZ,     "Offset", PF_EditAnywhere)
    REFLECT_FLOAT(scaleX,      "Scale",  PF_EditAnywhere)
    REFLECT_FLOAT(scaleY,      "Scale",  PF_EditAnywhere)
    REFLECT_FLOAT(scaleZ,      "Scale",  PF_EditAnywhere)
REFLECT_END(HeightFieldComponent)

// ═══════════════════════════════════════════════════════════════════════
// AnimationComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(AnimationComponent, "Animation")
    REFLECT_INT  (currentClipIndex, "Playback", PF_EditAnywhere)
    REFLECT_FLOAT(currentTime,      "Playback", PF_Transient | PF_Hidden)
    REFLECT_FLOAT(speed,            "Playback", PF_EditAnywhere)
    REFLECT_BOOL (playing,          "Playback", PF_EditAnywhere)
    REFLECT_BOOL (loop,             "Playback", PF_EditAnywhere)
REFLECT_END(AnimationComponent)

// ═══════════════════════════════════════════════════════════════════════
// LodComponent (LodLevel is a nested struct – registered separately)
// ═══════════════════════════════════════════════════════════════════════

// LodComponent contains a vector of complex sub-structs; individual
// LodLevel entries are registered for reference but the editor handles
// the vector itself via custom UI.

REFLECT_BEGIN_NESTED(LodComponent::LodLevel, LodLevel, "LOD Level")
    REFLECT_ASSET_PATH(meshAssetPath, "LOD", PF_EditAnywhere)
    REFLECT_FLOAT     (maxDistance,    "LOD", PF_EditAnywhere)
REFLECT_END_NESTED(LodComponent::LodLevel, LodLevel)

REFLECT_BEGIN(LodComponent, "LOD")
REFLECT_END(LodComponent)

// ═══════════════════════════════════════════════════════════════════════
// CharacterControllerComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(CharacterControllerComponent, "Character Controller")
    REFLECT_FLOAT(radius,        "Shape",    PF_EditAnywhere)
    REFLECT_FLOAT(height,        "Shape",    PF_EditAnywhere)
    REFLECT_FLOAT_CLAMPED(maxSlopeAngle, "Movement", PF_EditAnywhere, 0.0f, 90.0f)
    REFLECT_FLOAT(stepUpHeight,  "Movement", PF_EditAnywhere)
    REFLECT_FLOAT(skinWidth,     "Movement", PF_EditAnywhere)
    REFLECT_FLOAT(gravityFactor, "Gravity",  PF_EditAnywhere)
    REFLECT_FLOAT(maxFallSpeed,  "Gravity",  PF_EditAnywhere)
    REFLECT_BOOL (isGrounded,    "State",    PF_Transient | PF_VisibleOnly)
    REFLECT_VEC3 (groundNormal,  "State",    PF_Transient | PF_VisibleOnly)
    REFLECT_FLOAT(groundAngle,   "State",    PF_Transient | PF_VisibleOnly)
    REFLECT_VEC3 (velocity,      "State",    PF_Transient | PF_VisibleOnly)
REFLECT_END(CharacterControllerComponent)

// ═══════════════════════════════════════════════════════════════════════
// AudioSourceComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(AudioSourceComponent, "Audio Source")
    REFLECT_ASSET_PATH(assetPath,     "Asset",     PF_EditAnywhere)
    REFLECT_INT       (assetId,       "Asset",     PF_VisibleOnly)
    REFLECT_BOOL      (is3D,          "Spatial",   PF_EditAnywhere)
    REFLECT_FLOAT     (minDistance,    "Spatial",   PF_EditAnywhere)
    REFLECT_FLOAT     (maxDistance,    "Spatial",   PF_EditAnywhere)
    REFLECT_FLOAT     (rolloffFactor, "Spatial",   PF_EditAnywhere)
    REFLECT_FLOAT_CLAMPED(gain,       "Playback",  PF_EditAnywhere, 0.0f, 10.0f)
    REFLECT_BOOL      (loop,          "Playback",  PF_EditAnywhere)
    REFLECT_BOOL      (autoPlay,      "Playback",  PF_EditAnywhere)
    REFLECT_INT       (runtimeHandle, "",          PF_Transient | PF_Hidden)
REFLECT_END(AudioSourceComponent)

// ═══════════════════════════════════════════════════════════════════════
// ParticleEmitterComponent
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN(ParticleEmitterComponent, "Particle Emitter")
    REFLECT_INT  (maxParticles,  "Emission", PF_EditAnywhere)
    REFLECT_FLOAT(emissionRate,  "Emission", PF_EditAnywhere)
    REFLECT_FLOAT(lifetime,      "Particle", PF_EditAnywhere)
    REFLECT_FLOAT(speed,         "Particle", PF_EditAnywhere)
    REFLECT_FLOAT(speedVariance, "Particle", PF_EditAnywhere)
    REFLECT_FLOAT(size,          "Particle", PF_EditAnywhere)
    REFLECT_FLOAT(sizeEnd,       "Particle", PF_EditAnywhere)
    REFLECT_FLOAT(gravity,       "Forces",   PF_EditAnywhere)
    REFLECT_FLOAT(colorR,        "Start Color", PF_EditAnywhere)
    REFLECT_FLOAT(colorG,        "Start Color", PF_EditAnywhere)
    REFLECT_FLOAT(colorB,        "Start Color", PF_EditAnywhere)
    REFLECT_FLOAT(colorA,        "Start Color", PF_EditAnywhere)
    REFLECT_FLOAT(colorEndR,     "End Color",   PF_EditAnywhere)
    REFLECT_FLOAT(colorEndG,     "End Color",   PF_EditAnywhere)
    REFLECT_FLOAT(colorEndB,     "End Color",   PF_EditAnywhere)
    REFLECT_FLOAT(colorEndA,     "End Color",   PF_EditAnywhere)
    REFLECT_FLOAT_CLAMPED(coneAngle, "Emission", PF_EditAnywhere, 0.0f, 180.0f)
    REFLECT_FLOAT(emissionAccumulator, "", PF_Transient | PF_Hidden)
    REFLECT_BOOL (enabled,       "General",  PF_EditAnywhere)
    REFLECT_BOOL (loop,          "General",  PF_EditAnywhere)
REFLECT_END(ParticleEmitterComponent)

// ═══════════════════════════════════════════════════════════════════════
// ConstraintComponent::ConstraintEntry
// ═══════════════════════════════════════════════════════════════════════

REFLECT_BEGIN_NESTED(ConstraintComponent::ConstraintEntry, ConstraintEntry, "Constraint Entry")
    REFLECT_ENUM      (type,             "Constraint", PF_EditAnywhere, ConstraintType_Entries, 7)
    REFLECT_ENTITY_REF(connectedEntity,  "Constraint", PF_EditAnywhere)
    REFLECT_VEC3      (anchor,           "Anchors",    PF_EditAnywhere)
    REFLECT_VEC3      (connectedAnchor,  "Anchors",    PF_EditAnywhere)
    REFLECT_VEC3      (axis,             "Axis",       PF_EditAnywhere)
    REFLECT_VEC2      (limits,           "Limits",     PF_EditAnywhere)
    REFLECT_FLOAT     (springStiffness,  "Spring",     PF_EditAnywhere)
    REFLECT_FLOAT     (springDamping,    "Spring",     PF_EditAnywhere)
    REFLECT_BOOL      (breakable,        "Breaking",   PF_EditAnywhere)
    REFLECT_FLOAT     (breakForce,       "Breaking",   PF_EditAnywhere)
    REFLECT_FLOAT     (breakTorque,      "Breaking",   PF_EditAnywhere)
REFLECT_END_NESTED(ConstraintComponent::ConstraintEntry, ConstraintEntry)

REFLECT_BEGIN(ConstraintComponent, "Constraints")
REFLECT_END(ConstraintComponent)

} // namespace ECS
