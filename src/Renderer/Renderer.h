#pragma once

#include <string>

class Renderer
{
public:
    virtual ~Renderer() = default;

    virtual void initialize() = 0;
    virtual void clear() = 0;
    virtual void present() = 0;
    virtual const std::string& name() const = 0;
};
