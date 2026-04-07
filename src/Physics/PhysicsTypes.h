#pragma once

#include <cstdint>

namespace PhysicsTypes
{
    using BodyHandle = uint64_t;
    constexpr BodyHandle InvalidBody = 0;

    struct BodyDesc
    {
        enum class Shape { Box, Sphere, Capsule, Cylinder, Mesh, HeightField };
        Shape shape{ Shape::Box };

        float halfExtents[3]{ 0.5f, 0.5f, 0.5f };
        float radius{ 0.5f };
        float halfHeight{ 0.5f };

        const float* heightData{ nullptr };
        int          heightSampleCount{ 0 };
        float        heightOffset[3]{ 0.0f, 0.0f, 0.0f };
        float        heightScale[3]{ 1.0f, 1.0f, 1.0f };

        const float*    meshVertices{ nullptr };
        int             meshVertexFloatCount{ 0 };
        const uint32_t* meshIndices{ nullptr };
        int             meshIndexCount{ 0 };
        float           meshScale[3]{ 1.0f, 1.0f, 1.0f };
        uint32_t        meshAssetId{ 0 };
        uint64_t        meshAssetSignature{ 0 };
        bool            meshTreatAsConvex{ false };

        float colliderOffset[3]{ 0.0f, 0.0f, 0.0f };

        float position[3]{ 0.0f, 0.0f, 0.0f };
        float rotationEulerDeg[3]{ 0.0f, 0.0f, 0.0f };

        enum class MotionType { Static, Kinematic, Dynamic };
        MotionType motionType{ MotionType::Static };

        enum class MotionQuality { Discrete, LinearCast };
        MotionQuality motionQuality{ MotionQuality::Discrete };

        float restitution{ 0.3f };
        float friction{ 0.5f };
        bool  isSensor{ false };

        float mass{ 1.0f };
        float gravityFactor{ 1.0f };
        float linearDamping{ 0.05f };
        float angularDamping{ 0.05f };
        float maxLinearVelocity{ 500.0f };
        float maxAngularVelocity{ 47.1239f };
        bool  allowSleeping{ true };
        float velocity[3]{ 0.0f, 0.0f, 0.0f };
        float angularVelocity[3]{ 0.0f, 0.0f, 0.0f };

        uint32_t entityId{ 0 };
    };

    struct BodyState
    {
        float position[3]{};
        float rotationEulerDeg[3]{};
        float velocity[3]{};
        float angularVelocityDeg[3]{};
    };

    struct CollisionEventData
    {
        uint32_t entityA{ 0 };
        uint32_t entityB{ 0 };
        float    normal[3]{};
        float    depth{ 0.0f };
        float    contactPoint[3]{};
    };

    struct RaycastResult
    {
        uint32_t entity{ 0 };
        float    point[3]{};
        float    normal[3]{};
        float    distance{ 0.0f };
        bool     hit{ false };
    };

    using CharacterHandle = uint64_t;
    constexpr CharacterHandle InvalidCharacter = 0;

    struct CharacterDesc
    {
        float radius{ 0.3f };
        float height{ 1.8f };
        float maxSlopeAngle{ 45.0f };
        float stepUpHeight{ 0.3f };
        float skinWidth{ 0.02f };

        float position[3]{ 0.0f, 0.0f, 0.0f };
        float rotationYDeg{ 0.0f };

        uint32_t entityId{ 0 };
    };

    struct CharacterState
    {
        float position[3]{};
        bool  isGrounded{ false };
        float groundNormal[3]{ 0.0f, 1.0f, 0.0f };
        float groundAngle{ 0.0f };
        float velocity[3]{};
    };
}
