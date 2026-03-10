#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../Core/MathTypes.h"

class AssetData;
class Texture;
struct Skeleton;

class IRenderObject3D
{
public:
    virtual ~IRenderObject3D() = default;

    virtual bool prepare() = 0;
    virtual void render() = 0;
    virtual void setTextures(const std::vector<std::shared_ptr<Texture>>& textures) = 0;
    virtual void setMaterialCacheKeySuffix(const std::string& suffix) = 0;
    virtual void setFragmentShaderOverride(const std::string& name) = 0;
    virtual void setShininess(float shininess) = 0;

    virtual bool hasLocalBounds() const = 0;
    virtual Vec3 getLocalBoundsMin() const = 0;
    virtual Vec3 getLocalBoundsMax() const = 0;
    virtual int  getVertexCount() const = 0;
    virtual int  getIndexCount() const = 0;
    virtual bool isSkinned() const { return false; }
    virtual const Skeleton* getSkeleton() const { return nullptr; }
};
