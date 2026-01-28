#include "OpenGLObject3D.h"

#include <filesystem>
#include <vector>

#include "OpenGLMaterial.h"
#include "OpenGLShader.h"
#include "Logger.h"

#include "../../Core/Object3D.h"
#include "../../AssetManager/AssetManager.h"

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
}

OpenGLObject3D::OpenGLObject3D(const std::shared_ptr<Object3D>& obj)
    : m_obj(obj)
{
}

bool OpenGLObject3D::prepare()
{
    auto& logger = Logger::Instance();
    if (!m_obj)
        return false;

    // If already prepared (material backend-specific), nothing to do.
    {
        auto existing = m_obj->getMaterial();
        if (std::dynamic_pointer_cast<OpenGLMaterial>(existing))
        {
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

    const auto vCount = m_obj->getVertices().size();
    const auto iCount = m_obj->getIndices().size();
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

    mat->setVertexData(m_obj->getVertices());
    mat->setIndexData(m_obj->getIndices());

    // Default layout: positions (x,y,z) + texcoords (u,v)
    const GLsizei stride = static_cast<GLsizei>(5 * sizeof(float));
    std::vector<OpenGLMaterial::LayoutElement> layout;
    layout.push_back(OpenGLMaterial::LayoutElement{ 0, 3, GL_FLOAT, GL_FALSE, stride, 0 });
    layout.push_back(OpenGLMaterial::LayoutElement{ 2, 2, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(3 * sizeof(float)) });
    mat->setLayout(layout);

    // Propagate textures from the runtime material (loaded by AssetManager) into the OpenGL material.
    if (auto cpuMat = m_obj->getMaterial())
    {
        mat->setTextures(cpuMat->getTextures());
    }

    if (!mat->build())
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject3D: Failed to build OpenGL material.", Logger::LogLevel::ERROR);
        return false;
    }

    m_obj->setMaterial(mat);
    return true;
}
