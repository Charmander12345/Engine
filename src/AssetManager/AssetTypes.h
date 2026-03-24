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
    PointLight,
    Audio,
    Script,
    Shader,
    Level,
    Widget,
    Skybox,
    Prefab
};