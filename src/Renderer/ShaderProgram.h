#pragma once

#include <memory>
#include <string>
#include "Shader.h"

class ShaderProgram
{
public:
    virtual ~ShaderProgram() = default;

    virtual bool attach(const std::shared_ptr<Shader>& shader) = 0;
    virtual bool link() = 0;

    virtual void bind() const = 0;
    virtual void unbind() const = 0;

    virtual bool isLinked() const = 0;
    virtual const std::string& linkLog() const = 0;
};
