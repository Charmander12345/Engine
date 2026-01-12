#pragma once

#include "EngineObject.h"
#include <string>
#include <vector>

class MaterialAsset : public EngineObject
{
public:
    MaterialAsset() = default;
    ~MaterialAsset() override = default;

    void setTextureAssetPaths(std::vector<std::string> paths) { m_textureAssetPaths = std::move(paths); }
    const std::vector<std::string>& getTextureAssetPaths() const { return m_textureAssetPaths; }

private:
    std::vector<std::string> m_textureAssetPaths;
};
