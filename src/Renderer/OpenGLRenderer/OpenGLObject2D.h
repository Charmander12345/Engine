#pragma once

#include <memory>
#include <glm/glm.hpp>

class AssetData;
class OpenGLMaterial;
class Texture;

class OpenGLObject2D
{
public:
    explicit OpenGLObject2D(std::shared_ptr<AssetData> asset);

    bool prepare();
    void setMatrices(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection);
    void render();
    void setTextures(const std::vector<std::shared_ptr<Texture>>& textures);

    static void ClearCache();

    std::shared_ptr<AssetData> getAsset() const { return m_asset; }

private:
    std::shared_ptr<AssetData> m_asset;
    std::shared_ptr<OpenGLMaterial> m_material;
};
