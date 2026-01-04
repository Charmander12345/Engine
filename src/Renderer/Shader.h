#pragma once

#include <string>

class Shader
{
public:
    enum class Type
    {
        Vertex,
        Fragment,
        Geometry,
        Compute,
        Hull,
        Domain
    };

    virtual ~Shader() = default;

    virtual bool loadFromSource(Type type, const std::string& source) = 0;
    virtual bool loadFromFile(Type type, const std::string& filePath) = 0;
    virtual bool compile() = 0;

    virtual Type type() const = 0;
    virtual const std::string& source() const = 0;
    virtual const std::string& compileLog() const = 0;
};
