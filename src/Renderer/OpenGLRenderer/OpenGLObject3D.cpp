#include "OpenGLObject3D.h"

#include <filesystem>
#include <vector>

#include "OpenGLMaterial.h"
#include "OpenGLShader.h"
#include "Logger.h"

#include "../../Core/Asset.h"

#include <unordered_map>

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

    std::unordered_map<std::string, std::shared_ptr<OpenGLMaterial>> s_materialCache;
}

OpenGLObject3D::OpenGLObject3D(const std::shared_ptr<AssetData>& asset)
    : m_asset(asset)
{
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
    if (!path.empty())
    {
        auto it = s_materialCache.find(path);
        if (it != s_materialCache.end())
        {
            m_material = it->second;
            return true;
        }
    }

    const std::string vertexPath = ResolveShaderPath("vertex.glsl");
    const std::string fragmentPath = ResolveShaderPath("fragment.glsl");
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

    const auto& data = m_asset->getData();
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

    mat->setVertexData(vertices);
    mat->setIndexData(indices);

    // Default layout: positions (x,y,z) + texcoords (u,v)
    const GLsizei stride = static_cast<GLsizei>(5 * sizeof(float));
    std::vector<OpenGLMaterial::LayoutElement> layout;
    layout.push_back(OpenGLMaterial::LayoutElement{ 0, 3, GL_FLOAT, GL_FALSE, stride, 0 });
    layout.push_back(OpenGLMaterial::LayoutElement{ 2, 2, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(3 * sizeof(float)) });
    mat->setLayout(layout);

    if (!mat->build())
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: Failed to build OpenGL material.", Logger::LogLevel::ERROR);
        return false;
    }

    m_material = mat;
    if (!path.empty())
    {
        s_materialCache[path] = m_material;
    }
    return true;
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

void OpenGLObject3D::setTextures(const std::vector<std::shared_ptr<Texture>>& textures)
{
    if (m_material)
    {
        m_material->setTextures(textures);
    }
}
