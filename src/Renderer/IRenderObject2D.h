#pragma once

#include <memory>
#include <vector>

class AssetData;
class Texture;

class IRenderObject2D
{
public:
    virtual ~IRenderObject2D() = default;

    virtual bool prepare() = 0;
    virtual void render() = 0;
    virtual void setTextures(const std::vector<std::shared_ptr<Texture>>& textures) = 0;
    virtual std::shared_ptr<AssetData> getAsset() const = 0;
};
