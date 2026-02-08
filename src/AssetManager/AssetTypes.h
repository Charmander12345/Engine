#pragma once
#include "json.hpp"
using json = nlohmann::json;

enum class AssetType
{
#define NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(AssetType)
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
    Widget
};
