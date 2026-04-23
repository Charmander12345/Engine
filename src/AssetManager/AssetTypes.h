#pragma once
#include "json.hpp"
using json = nlohmann::json;

enum class AssetType
{
    Unknown,
    Texture,
    Material,
    Model2D,
    Model3D,
    StaticMesh,
    SkeletalMesh,
    PointLight,
    Audio,
    Script,
    Shader,
    Level,
    Widget,
    Skybox,
    Prefab,
    Entity,
    NativeScript,
    InputAction,
    InputMapping,
    ActorAsset
};