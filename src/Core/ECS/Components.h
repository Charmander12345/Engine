#pragma once
#include <string>
#include <vector>
#include "../SkeletalData.h"


namespace ECS
{
	struct TransformComponent
	{
		float position[3]{ 0.0f, 0.0f, 0.0f };
		float rotation[3]{ 0.0f, 0.0f, 0.0f }; // Euler angles in degrees
		float scale[3]{ 1.0f, 1.0f, 1.0f };
	};

	struct MeshComponent
	{
		std::string meshAssetPath;
		unsigned int meshAssetId{ 0 };
	};

	/// Per-entity material parameter overrides.  When a flag is set the
	/// corresponding value is used instead of the base material's value.
	struct MaterialOverrides
	{
		float colorTint[3]{ 1.0f, 1.0f, 1.0f };   ///< Multiplicative RGB tint
		float metallic{ 0.0f };
		float roughness{ 0.5f };
		float shininess{ 32.0f };
		float emissiveColor[3]{ 0.0f, 0.0f, 0.0f };
		bool hasColorTint{ false };
		bool hasMetallic{ false };
		bool hasRoughness{ false };
		bool hasShininess{ false };
		bool hasEmissive{ false };

		bool hasAnyOverride() const
		{
			return hasColorTint || hasMetallic || hasRoughness || hasShininess || hasEmissive;
		}
	};

	struct MaterialComponent
	{
		std::string materialAssetPath;
		unsigned int materialAssetId{ 0 };
		MaterialOverrides overrides;
	};

	struct LightComponent
	{
		enum class LightType
		{
			Point,
			Directional,
			Spot
		} type{ LightType::Point };
		float color[3]{ 1.0f, 1.0f, 1.0f }; // RGB
		float intensity{ 1.0f };
		float range{ 10.0f }; // For point and spot lights
		float spotAngle{ 30.0f }; // For spot lights
	};

	struct CameraComponent
	{
		float fov{ 60.0f }; // Field of view in degrees
		float nearClip{ 0.1f };
		float farClip{ 1000.0f };
		bool isActive{ false }; // Mark as the primary runtime camera
	};

	struct CollisionComponent
	{
		enum class ColliderType
		{
			Box,
			Sphere,
			Capsule,
			Cylinder,
			Mesh,
			HeightField
		} colliderType{ ColliderType::Box };
		float colliderSize[3]{ 0.5f, 0.5f, 0.5f };   // Box: half-extents, Sphere: radius(x), Capsule/Cylinder: radius(x)+halfHeight(y)
		float colliderOffset[3]{ 0.0f, 0.0f, 0.0f };  // Local offset from entity transform
		float restitution{ 0.3f };  // Bounciness (0 = no bounce, 1 = perfect bounce)
		float friction{ 0.5f };     // Surface friction coefficient
		bool  isSensor{ false };    // Trigger volume (overlap events only, no physics response)
	};

	struct PhysicsComponent
	{
		enum class MotionType
		{
			Static,
			Kinematic,
			Dynamic
		} motionType{ MotionType::Dynamic };
		float mass{ 1.0f };
		float gravityFactor{ 1.0f };          // 0 = no gravity, 1 = full, can be >1
		float linearDamping{ 0.05f };         // Velocity decay per second
		float angularDamping{ 0.05f };        // Angular velocity decay per second
		float maxLinearVelocity{ 500.0f };    // Clamp (m/s)
		float maxAngularVelocity{ 47.1239f }; // Clamp (rad/s, ~0.25*PI*60 Jolt default)
		enum class MotionQuality
		{
			Discrete,
			LinearCast   // Continuous collision detection
		} motionQuality{ MotionQuality::Discrete };
		bool  allowSleeping{ true };
		float velocity[3]{ 0.0f, 0.0f, 0.0f };
		float angularVelocity[3]{ 0.0f, 0.0f, 0.0f };
	};

	struct ScriptComponent
	{
		std::string scriptPath; // Path to the script file
		unsigned int scriptAssetId{ 0 };
	};

	struct NameComponent
	{
		std::string displayName;
	};

	struct HeightFieldComponent
	{
		std::vector<float> heights;
		int sampleCount{ 0 };
		float offsetX{ 0.0f };
		float offsetY{ 0.0f };
		float offsetZ{ 0.0f };
		float scaleX{ 1.0f };
		float scaleY{ 1.0f };
		float scaleZ{ 1.0f };
	};

	/// Skeletal animation state – references the skeleton embedded in the mesh asset.
	struct AnimationComponent
	{
		int currentClipIndex{-1};           ///< -1 = no animation playing
		float currentTime{0.0f};
		float speed{1.0f};
		bool playing{false};
		bool loop{true};
	};

	/// Level of Detail – multiple mesh variants sorted by camera distance.
	/// Levels must be ordered by ascending maxDistance.  The last level
	/// (or any level with maxDistance <= 0) acts as the fallback.
	struct LodComponent
	{
		struct LodLevel
		{
			std::string meshAssetPath;
			float maxDistance{ 0.0f }; // 0 = fallback (lowest quality / farthest)
		};
		std::vector<LodLevel> levels;
	};
}
