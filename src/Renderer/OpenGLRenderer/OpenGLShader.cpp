#include "OpenGLShader.h"

#include <vector>
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>
#include "Logger.h"
#include "../../AssetManager/HPKArchive.h"

OpenGLShader::OpenGLShader() = default;

OpenGLShader::~OpenGLShader()
{
    if (m_shader)
    {
        glDeleteShader(m_shader);
        m_shader = 0;
    }
}

bool OpenGLShader::loadFromSource(Shader::Type type, const std::string& source)
{
    m_type = type;
    m_source = source;
    return compile();
}

bool OpenGLShader::loadFromFile(Shader::Type type, const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::in | std::ios::binary);
    if (!file)
    {
        // HPK fallback
        auto* hpk = HPKReader::GetMounted();
        if (hpk)
        {
            std::string vpath = hpk->makeVirtualPath(filePath);
            Logger::Instance().log("HPK shader fallback: file=" + filePath
                + " vpath=" + (vpath.empty() ? "(empty)" : vpath)
                + " baseDir=" + hpk->getBaseDir(), Logger::LogLevel::INFO);
            if (!vpath.empty())
            {
                auto buf = hpk->readFile(vpath);
                if (buf)
                {
                    Logger::Instance().log("HPK shader loaded: " + vpath
                        + " (" + std::to_string(buf->size()) + " bytes)", Logger::LogLevel::INFO);
                    m_source.assign(buf->data(), buf->size());
                    m_type = type;
                    return compile();
                }
                Logger::Instance().log("HPK shader read failed: " + vpath, Logger::LogLevel::ERROR);
            }
        }
        else
        {
            Logger::Instance().log("HPK not mounted when loading shader: " + filePath, Logger::LogLevel::WARNING);
        }
        Logger::Instance().log("Shader-Datei konnte nicht geoeffnet werden: " + filePath, Logger::LogLevel::ERROR);
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    m_source = buffer.str();
    m_type = type;
    return compile();
}

bool OpenGLShader::loadFromFileWithDefines(Shader::Type type, const std::string& filePath, const std::string& defines)
{
    std::string src;
    {
        std::ifstream file(filePath, std::ios::in | std::ios::binary);
        if (!file)
        {
            // HPK fallback
            auto* hpk = HPKReader::GetMounted();
            if (hpk)
            {
                std::string vpath = hpk->makeVirtualPath(filePath);
                Logger::Instance().log("HPK shader(defines) fallback: file=" + filePath
                    + " vpath=" + (vpath.empty() ? "(empty)" : vpath), Logger::LogLevel::INFO);
                if (!vpath.empty())
                {
                    auto buf = hpk->readFile(vpath);
                    if (buf)
                    {
                        src.assign(buf->data(), buf->size());
                        Logger::Instance().log("HPK shader(defines) loaded: " + vpath
                            + " (" + std::to_string(buf->size()) + " bytes)", Logger::LogLevel::INFO);
                    }
                }
            }
            else
            {
                Logger::Instance().log("HPK not mounted when loading shader(defines): " + filePath, Logger::LogLevel::WARNING);
            }
            if (src.empty())
            {
                Logger::Instance().log("Shader-Datei konnte nicht geoeffnet werden: " + filePath, Logger::LogLevel::ERROR);
                return false;
            }
        }
        else
        {
            std::stringstream buffer;
            buffer << file.rdbuf();
            src = buffer.str();
        }
    }
    m_type = type;

    // Insert defines right after the #version line
    if (!defines.empty())
    {
        auto pos = src.find('\n');
        if (pos != std::string::npos)
            src.insert(pos + 1, defines);
    }
    m_source = std::move(src);
    return compile();
}

static GLenum ToGLType(Shader::Type type)
{
    switch (type)
    {
    case Shader::Type::Vertex: return GL_VERTEX_SHADER;
    case Shader::Type::Fragment: return GL_FRAGMENT_SHADER;
    case Shader::Type::Geometry: return GL_GEOMETRY_SHADER;
    case Shader::Type::Compute: return GL_COMPUTE_SHADER;
    case Shader::Type::Hull: return GL_TESS_CONTROL_SHADER;
    case Shader::Type::Domain: return GL_TESS_EVALUATION_SHADER;
    default: return 0;
    }
}

bool OpenGLShader::compile()
{
    if (m_shader)
    {
        glDeleteShader(m_shader);
        m_shader = 0;
    }
    m_compileLog.clear();
    m_compiled = false;

    GLenum glType = ToGLType(m_type);
    if (glType == 0)
    {
        Logger::Instance().log("Unsupported shader type for OpenGL", Logger::LogLevel::ERROR);
        return false;
    }

    GLuint shader = glCreateShader(glType);
    const char* src = m_source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    GLint logLen = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
    if (logLen > 1)
    {
        std::vector<char> log(logLen);
        glGetShaderInfoLog(shader, logLen, nullptr, log.data());
        m_compileLog.assign(log.data(), log.size());
    }

    if (!success)
    {
        Logger::Instance().log(m_compileLog.empty() ? "Shader-Compile fehlgeschlagen" : m_compileLog, Logger::LogLevel::ERROR);
        glDeleteShader(shader);
        return false;
    }

    m_shader = shader;
    m_compiled = true;
    return true;
}