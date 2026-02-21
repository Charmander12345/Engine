#pragma once
#include <string>


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

	struct MaterialComponent
	{
		std::string materialAssetPath;
		unsigned int materialAssetId{ 0 };
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

	struct PhysicsComponent
	{
		enum class ColliderType
		{
			Box,
			Sphere,
			Mesh
		} colliderType{ ColliderType::Box };
		bool isStatic{ true };
		float mass{ 1.0f }; // Relevant if isStatic is false
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
}
