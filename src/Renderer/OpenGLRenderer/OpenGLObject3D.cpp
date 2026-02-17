#include "OpenGLObject3D.h"

#include <filesystem>
#include <vector>
#include <limits>

#include "OpenGLMaterial.h"
#include "OpenGLShader.h"
#include "Logger.h"

#include "../../Core/Asset.h"

#include <unordered_map>
#include <glm/glm.hpp>

namespace
{
    std::string ResolveShaderPath(const std::string& filename)
    {
        std::filesystem::path base = std::filesystem::current_path();
        std::filesystem::path shadersfolder = base / "shaders" / filename;
        if (std::filesystem::exists(shadersfolder))
        {
            return shadersfolder.string();
        }
        return {};
    }

    std::vector<float> ReadFloatArray(const json& data, const char* key)
    {
        if (!data.is_object())
        {
            return {};
        }
        auto it = data.find(key);
        if (it == data.end() || !it->is_array())
        {
            return {};
        }
        return it->get<std::vector<float>>();
    }

    std::vector<uint32_t> ReadIndexArray(const json& data, const char* key)
    {
        if (!data.is_object())
        {
            return {};
        }
        auto it = data.find(key);
        if (it == data.end() || !it->is_array())
        {
            return {};
        }
        return it->get<std::vector<uint32_t>>();
    }

    std::vector<float> BuildVerticesWithFlatNormals(const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
    {
        constexpr size_t inputStride = 5;
        if (vertices.empty() || (vertices.size() % inputStride) != 0)
        {
            return {};
        }

        const size_t vertexCount = vertices.size() / inputStride;
        glm::vec3 meshCenter(0.0f);
        for (size_t i = 0; i < vertexCount; ++i)
        {
            const size_t base = i * inputStride;
            meshCenter += glm::vec3(vertices[base + 0], vertices[base + 1], vertices[base + 2]);
        }
        if (vertexCount > 0)
        {
            meshCenter /= static_cast<float>(vertexCount);
        }
        std::vector<float> result;
        result.reserve((indices.empty() ? vertexCount : indices.size()) * 8);

        const auto addTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2)
        {
            if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
            {
                return;
            }

            const size_t base0 = static_cast<size_t>(i0) * inputStride;
            const size_t base1 = static_cast<size_t>(i1) * inputStride;
            const size_t base2 = static_cast<size_t>(i2) * inputStride;

            const glm::vec3 p0(vertices[base0 + 0], vertices[base0 + 1], vertices[base0 + 2]);
            const glm::vec3 p1(vertices[base1 + 0], vertices[base1 + 1], vertices[base1 + 2]);
            const glm::vec3 p2(vertices[base2 + 0], vertices[base2 + 1], vertices[base2 + 2]);

            const glm::vec3 edge1 = p1 - p0;
            const glm::vec3 edge2 = p2 - p0;
            glm::vec3 faceNormal = glm::cross(edge2, edge1);
            if (glm::length(faceNormal) > 0.0f)
            {
                faceNormal = glm::normalize(faceNormal);
            }

            const glm::vec3 faceCenter = (p0 + p1 + p2) / 3.0f;
            if (glm::dot(faceNormal, faceCenter - meshCenter) < 0.0f)
            {
                faceNormal = -faceNormal;
            }

            auto appendVertex = [&](size_t base)
            {
                result.push_back(vertices[base + 0]);
                result.push_back(vertices[base + 1]);
                result.push_back(vertices[base + 2]);
                result.push_back(faceNormal.x);
                result.push_back(faceNormal.y);
                result.push_back(faceNormal.z);
                result.push_back(vertices[base + 3]);
                result.push_back(vertices[base + 4]);
            };

            appendVertex(base0);
            appendVertex(base1);
            appendVertex(base2);
        };

        if (!indices.empty())
        {
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                addTriangle(indices[i], indices[i + 1], indices[i + 2]);
            }
        }
        else
        {
            for (size_t i = 0; i + 2 < vertexCount; i += 3)
            {
                addTriangle(static_cast<uint32_t>(i), static_cast<uint32_t>(i + 1), static_cast<uint32_t>(i + 2));
            }
        }

        return result;
    }

    std::unordered_map<std::string, std::shared_ptr<OpenGLMaterial>> s_materialCache;
}

OpenGLObject3D::OpenGLObject3D(const std::shared_ptr<AssetData>& asset)
    : m_asset(asset)
{
}

bool OpenGLObject3D::hasLocalBounds() const
{
    return m_hasLocalBounds;
}

const glm::vec3& OpenGLObject3D::getLocalBoundsMin() const
{
    return m_localBoundsMin;
}

const glm::vec3& OpenGLObject3D::getLocalBoundsMax() const
{
    return m_localBoundsMax;
}

GLuint OpenGLObject3D::getProgram() const
{
    return m_material ? m_material->getProgram() : 0;
}

GLuint OpenGLObject3D::getVao() const
{
    return m_material ? m_material->getVao() : 0;
}

GLsizei OpenGLObject3D::getVertexCount() const
{
    return m_material ? m_material->getVertexCount() : 0;
}

GLsizei OpenGLObject3D::getIndexCount() const
{
    return m_material ? m_material->getIndexCount() : 0;
}

void OpenGLObject3D::ClearCache()
{
    s_materialCache.clear();
}

bool OpenGLObject3D::prepare()
{
    auto& logger = Logger::Instance();
    if (!m_asset)
        return false;

    if (m_material)
        return true;

    const std::string path = m_asset->getPath();
    const std::string cacheKey = m_materialCacheKeySuffix.empty() ? path : (path + "|" + m_materialCacheKeySuffix);
    if (!cacheKey.empty())
    {
        auto it = s_materialCache.find(cacheKey);
        if (it != s_materialCache.end())
        {
            m_material = it->second;
            return true;
        }
    }

    const auto& data = m_asset->getData();
    std::string vertexOverride;
    std::string fragmentOverride;
    if (data.is_object())
    {
        if (data.contains("m_shaderVertex"))
        {
            vertexOverride = data.at("m_shaderVertex").get<std::string>();
        }
        if (data.contains("m_shaderFragment"))
        {
            fragmentOverride = data.at("m_shaderFragment").get<std::string>();
        }
    }

    const std::string vertexPath = ResolveShaderPath(vertexOverride.empty() ? "vertex.glsl" : vertexOverride);
    const std::string fragmentPath = ResolveShaderPath(fragmentOverride.empty() ? "fragment.glsl" : fragmentOverride);
    if (vertexPath.empty() || fragmentPath.empty())
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: Couldn't locate vertex.glsl and/or fragment.glsl.", Logger::LogLevel::ERROR);
        return false;
    }

    auto vertexShader = std::make_shared<OpenGLShader>();
    auto fragmentShader = std::make_shared<OpenGLShader>();

    if (!vertexShader->loadFromFile(Shader::Type::Vertex, vertexPath))
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: Failed to load/compile vertex shader.", Logger::LogLevel::ERROR);
        return false;
    }
    if (!fragmentShader->loadFromFile(Shader::Type::Fragment, fragmentPath))
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: Failed to load/compile fragment shader.", Logger::LogLevel::ERROR);
        return false;
    }

    auto mat = std::make_shared<OpenGLMaterial>();
    mat->addShader(vertexShader);
    mat->addShader(fragmentShader);

    auto vertices = ReadFloatArray(data, "m_vertices");
    auto indices = ReadIndexArray(data, "m_indices");
    const auto vCount = vertices.size();
    const auto iCount = indices.size();
    logger.log(Logger::Category::Rendering,
        "OpenGLObject3D: vertexFloats=" + std::to_string(vCount) +
        ", indexCount=" + std::to_string(iCount),
        Logger::LogLevel::INFO);

    if (vCount == 0)
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: WARNING: vertices are empty.", Logger::LogLevel::WARNING);
    }
    else if ((vCount % 5) != 0)
    {
        logger.log(Logger::Category::Rendering,
            "OpenGLObject3D: WARNING: vertex float count is not divisible by 5 (expected layout: pos3+uv2).",
            Logger::LogLevel::WARNING);
    }

    if (vCount == 0)
    {
        return false;
    }

    if ((vCount % 5) == 0)
    {
        glm::vec3 minPos(std::numeric_limits<float>::max());
        glm::vec3 maxPos(std::numeric_limits<float>::lowest());
        for (size_t i = 0; i + 4 < vCount; i += 5)
        {
            glm::vec3 pos(vertices[i + 0], vertices[i + 1], vertices[i + 2]);
            minPos = glm::min(minPos, pos);
            maxPos = glm::max(maxPos, pos);
        }

        m_localBoundsMin = minPos;
        m_localBoundsMax = maxPos;
        m_hasLocalBounds = true;
    }

    auto verticesWithNormals = BuildVerticesWithFlatNormals(vertices, indices);
    if (verticesWithNormals.empty())
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: Failed to build vertex normals.", Logger::LogLevel::ERROR);
        return false;
    }

    mat->setVertexData(verticesWithNormals);
    mat->setIndexData({});

    // Default layout: positions (x,y,z) + normals (x,y,z) + texcoords (u,v)
    const GLsizei stride = static_cast<GLsizei>(8 * sizeof(float));
    std::vector<OpenGLMaterial::LayoutElement> layout;
    layout.push_back(OpenGLMaterial::LayoutElement{ 0, 3, GL_FLOAT, GL_FALSE, stride, 0 });
    layout.push_back(OpenGLMaterial::LayoutElement{ 1, 3, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(3 * sizeof(float)) });
    layout.push_back(OpenGLMaterial::LayoutElement{ 2, 2, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(6 * sizeof(float)) });
    mat->setLayout(layout);

    if (!mat->build())
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: Failed to build OpenGL material.", Logger::LogLevel::ERROR);
        return false;
    }

    m_material = mat;
    if (!cacheKey.empty())
    {
        s_materialCache[cacheKey] = m_material;
    }
    return true;
}

void OpenGLObject3D::setLightData(const glm::vec3& position, const glm::vec3& color, float intensity)
{
    if (!m_material)
    {
        return;
    }
    m_material->setLightData(position, color, intensity);
}

void OpenGLObject3D::setLights(const std::vector<OpenGLMaterial::LightData>& lights)
{
    if (!m_material)
    {
        return;
    }
    m_material->setLights(lights);
}

void OpenGLObject3D::setMatrices(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection)
{
    if (!m_material)
        return;

    m_material->setModelMatrix(model);
    m_material->setViewMatrix(view);
    m_material->setProjectionMatrix(projection);
}

void OpenGLObject3D::render()
{
    if (m_material)
    {
        m_material->render();
    }
}

void OpenGLObject3D::renderBatchContinuation()
{
    if (m_material)
    {
        m_material->renderBatchContinuation();
    }
}

void OpenGLObject3D::setTextures(const std::vector<std::shared_ptr<Texture>>& textures)
{
    if (m_material)
    {
        m_material->setTextures(textures);
    }
}

void OpenGLObject3D::setShininess(float shininess)
{
    if (m_material)
    {
        m_material->setShininess(shininess);
    }
}
