#include "OpenGLObject2D.h"

#include <filesystem>
#include <vector>

#include "OpenGLMaterial.h"
#include "OpenGLShader.h"
#include "Logger.h"

#include "../../Basics/Object2D.h"
#include "../../Basics/Texture.h"
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

OpenGLObject2D::OpenGLObject2D(std::shared_ptr<Object2D> cpuObject)
    : m_cpuObject(std::move(cpuObject))
{
}

bool OpenGLObject2D::prepare()
{
    auto& logger = Logger::Instance();
    if (!m_cpuObject)
        return false;

    if (m_material)
        return true;

    const std::string vertexPath = ResolveShaderPath("vertex.glsl");
    const std::string fragmentPath = ResolveShaderPath("fragment.glsl");

    if (vertexPath.empty() || fragmentPath.empty())
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject2D: Couldn't locate vertex.glsl and/or fragment.glsl.", Logger::LogLevel::ERROR);
        return false;
    }

    auto vertexShader = std::make_shared<OpenGLShader>();
    auto fragmentShader = std::make_shared<OpenGLShader>();

    if (!vertexShader->loadFromFile(Shader::Type::Vertex, vertexPath))
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject2D: Failed to load/compile vertex shader.", Logger::LogLevel::ERROR);
        return false;
    }

    if (!fragmentShader->loadFromFile(Shader::Type::Fragment, fragmentPath))
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject2D: Failed to load/compile fragment shader.", Logger::LogLevel::ERROR);
        return false;
    }

    auto mat = std::make_shared<OpenGLMaterial>();
    mat->addShader(vertexShader);
    mat->addShader(fragmentShader);

    logger.log(Logger::Category::Rendering,
        "OpenGLObject2D: vertexFloats=" + std::to_string(m_cpuObject->getVertices().size()) +
        ", indexCount=" + std::to_string(m_cpuObject->getIndices().size()),
        Logger::LogLevel::INFO);
    if (m_cpuObject->getVertices().empty())
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject2D: WARNING: vertices are empty.", Logger::LogLevel::WARNING);
    }

    mat->setVertexData(m_cpuObject->getVertices());
    mat->setIndexData(m_cpuObject->getIndices());

    // Default layout: positions (x,y,z) + texcoords (u,v)
    const GLsizei stride = static_cast<GLsizei>(5 * sizeof(float));
    std::vector<OpenGLMaterial::LayoutElement> layout;
    layout.push_back(OpenGLMaterial::LayoutElement{ 0, 3, GL_FLOAT, GL_FALSE, stride, 0 });
    layout.push_back(OpenGLMaterial::LayoutElement{ 2, 2, GL_FLOAT, GL_FALSE, stride, static_cast<size_t>(3 * sizeof(float)) });
    mat->setLayout(layout);

    // Propagate textures from the runtime material (loaded by AssetManager) into the OpenGL material.
    if (auto cpuMat = m_cpuObject->getMaterial())
    {
        mat->setTextures(cpuMat->getTextures());
    }

    if (!mat->build())
    {
        logger.log(Logger::Category::Rendering, "OpenGLObject2D: Failed to build OpenGL material.", Logger::LogLevel::ERROR);
        return false;
    }

    m_material = mat;
    m_cpuObject->setMaterial(m_material);
    return true;
}

void OpenGLObject2D::setMatrices(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection)
{
    if (!m_material)
        return;

    m_material->setModelMatrix(model);
    m_material->setViewMatrix(view);
    m_material->setProjectionMatrix(projection);
}

void OpenGLObject2D::render()
{
    if (m_cpuObject)
    {
        m_cpuObject->render();
    }
}
