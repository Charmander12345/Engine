#pragma once

#include <string>

class IShaderProgram
{
public:
    virtual ~IShaderProgram() = default;

    virtual bool link() = 0;
    virtual void bind() const = 0;
    virtual void unbind() const = 0;
    virtual bool isLinked() const = 0;
    virtual const std::string& linkLog() const = 0;

    virtual void setUniform(const std::string& name, float value) = 0;
    virtual void setUniform(const std::string& name, int32_t value) = 0;
};
