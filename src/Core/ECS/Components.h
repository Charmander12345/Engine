#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../SkeletalData.h"


namespace ECS
{
	/// Invalid entity sentinel used for "no parent".
	static constexpr unsigned int InvalidEntity = UINT32_MAX;

	struct TransformComponent
	{
		// ── World-space values (computed from parent + local) ────────
		float position[3]{ 0.0f, 0.0f, 0.0f };
		float rotation[3]{ 0.0f, 0.0f, 0.0f }; // Euler angles in degrees
		float scale[3]{ 1.0f, 1.0f, 1.0f };

		// ── Local-space values (relative to parent) ─────────────────
		float localPosition[3]{ 0.0f, 0.0f, 0.0f };
		float localRotation[3]{ 0.0f, 0.0f, 0.0f };
		float localScale[3]{ 1.0f, 1.0f, 1.0f };

		// ── Hierarchy ───────────────────────────────────────────────
		unsigned int parent{ InvalidEntity };
		std::vector<unsigned int> children;

		/// When true the world transform needs recomputation from the
		/// local transform and the parent chain.
		bool dirty{ true };
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
		float specularMultiplier{ 1.0f };
		float emissiveColor[3]{ 0.0f, 0.0f, 0.0f };
		bool hasColorTint{ false };
		bool hasMetallic{ false };
		bool hasRoughness{ false };
		bool hasShininess{ false };
		bool hasSpecularMultiplier{ false };
		bool hasEmissive{ false };

		bool hasAnyOverride() const
		{
			return hasColorTint || hasMetallic || hasRoughness || hasShininess || hasSpecularMultiplier || hasEmissive;
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
		} motionQuality{ MotionQuality::LinearCast };
		bool  allowSleeping{ true };
		float velocity[3]{ 0.0f, 0.0f, 0.0f };
		float angularVelocity[3]{ 0.0f, 0.0f, 0.0f };
	};

	struct ConstraintComponent
	{
		enum class ConstraintType
		{
			Hinge,
			BallSocket,
			Fixed,
			Slider,
			Distance,
			Spring,
			Cone
        };

		struct ConstraintEntry
		{
			ConstraintType type{ ConstraintType::Fixed };
			unsigned int connectedEntity{ 0 };
			float anchor[3]{ 0.0f, 0.0f, 0.0f };
			float connectedAnchor[3]{ 0.0f, 0.0f, 0.0f };
			float axis[3]{ 1.0f, 0.0f, 0.0f };
			float limits[2]{ 0.0f, 0.0f };
			float springStiffness{ 0.0f };
			float springDamping{ 0.0f };
			bool breakable{ false };
			float breakForce{ 0.0f };
			float breakTorque{ 0.0f };
		};

		std::vector<ConstraintEntry> constraints{ ConstraintEntry{} };
	};

	struct LogicComponent
	{
		std::string scriptPath;      // Python script path (empty if not used)
		std::string nativeClassName; // C++ native script class name (empty if not used)
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

	/// Character Controller – kinematic capsule for player/NPC movement.
	/// Mutually exclusive with PhysicsComponent (entity has either CC or Rigidbody).
	struct CharacterControllerComponent
	{
		// ── Shape (Capsule) ─────────────────────────────────
		float radius{ 0.3f };          ///< Capsule radius (meters)
		float height{ 1.8f };          ///< Total height including hemispherical caps

		// ── Movement Parameters ─────────────────────────────
		float maxSlopeAngle{ 45.0f };  ///< Max walkable slope angle (degrees)
		float stepUpHeight{ 0.3f };    ///< Max step-up height (meters)
		float skinWidth{ 0.02f };      ///< Collision skin / contact offset

		// ── Gravity & Falling ───────────────────────────────
		float gravityFactor{ 1.0f };   ///< Scales world gravity (0 = no gravity)
		float maxFallSpeed{ 50.0f };   ///< Terminal velocity (m/s)

		// ── Runtime State (not serialized) ──────────────────
		bool  isGrounded{ false };
		float groundNormal[3]{ 0.0f, 1.0f, 0.0f };
		float groundAngle{ 0.0f };     ///< Current ground slope in degrees
		float velocity[3]{ 0.0f, 0.0f, 0.0f }; ///< Current movement velocity
	};

	/// Audio source – plays audio from the entity's position (3D) or globally (2D).
	struct AudioSourceComponent
	{
		std::string assetPath;                   ///< Content-relative path to audio asset
		unsigned int assetId{ 0 };               ///< Resolved asset id
		bool is3D{ false };                      ///< true = positional 3D, false = 2D (non-spatial)
		float minDistance{ 1.0f };               ///< Reference distance for 3D attenuation
		float maxDistance{ 50.0f };              ///< Max audible distance for 3D
		float rolloffFactor{ 1.0f };             ///< Rolloff factor for 3D attenuation
		float gain{ 1.0f };                      ///< Volume (0..1+)
		bool loop{ false };                      ///< Loop playback
		bool autoPlay{ false };                  ///< Start playing automatically on level load
		unsigned int runtimeHandle{ 0 };         ///< Runtime-only: AudioManager source handle (not serialized)
	};

	/// Particle emitter – spawns billboard particles from the entity's position.
	struct ParticleEmitterComponent
	{
		int maxParticles{ 100 };
		float emissionRate{ 20.0f };       ///< Particles per second
		float lifetime{ 2.0f };            ///< Seconds each particle lives
		float speed{ 2.0f };               ///< Initial speed (m/s)
		float speedVariance{ 0.5f };       ///< Random speed ± variance
		float size{ 0.2f };                ///< Billboard size (world units)
		float sizeEnd{ 0.0f };             ///< Size at end of life (0 = shrink to nothing)
		float gravity{ -9.81f };           ///< Y-axis gravity acceleration
		float colorR{ 1.0f };              ///< Start color
		float colorG{ 0.8f };
		float colorB{ 0.2f };
		float colorA{ 1.0f };
		float colorEndR{ 1.0f };           ///< End color (at death)
		float colorEndG{ 0.1f };
		float colorEndB{ 0.0f };
		float colorEndA{ 0.0f };
		float coneAngle{ 30.0f };          ///< Emission cone half-angle (degrees), 0 = straight up
		float emissionAccumulator{ 0.0f }; ///< Runtime accumulator (not serialized)
		bool  enabled{ true };
		bool  loop{ true };                ///< Restart emission when all particles dead
	};

	}
