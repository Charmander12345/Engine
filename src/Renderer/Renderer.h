#pragma once

#include <string>
#include <SDL3/SDL.h>

class Renderer
{
public:
    virtual ~Renderer() = default;

    virtual bool initialize() = 0;
    virtual void clear() = 0;
    virtual void render() = 0;
    virtual void present() = 0;
    virtual const std::string& name() const = 0;

    // Camera controls
    virtual void moveCamera(float dx, float dy, float dz) = 0;
    virtual void rotateCamera(float yawDeltaDegrees, float pitchDeltaDegrees) = 0;
};
