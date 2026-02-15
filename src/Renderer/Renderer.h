#pragma once

#include <string>
#include <SDL3/SDL.h>
#include "../Core/MathTypes.h"

class Renderer
{
public:
    virtual ~Renderer() = default;

    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void clear() = 0;
    virtual void render() = 0;
    virtual void present() = 0;
    virtual const std::string& name() const = 0;

    virtual SDL_Window* window() const = 0;

    // Camera controls
    virtual void moveCamera(float forward, float right, float up) = 0;
    virtual void rotateCamera(float yawDeltaDegrees, float pitchDeltaDegrees) = 0;
    virtual Vec3 getCameraPosition() const = 0;
    virtual void setCameraPosition(const Vec3& position) = 0;
    virtual Vec2 getCameraRotationDegrees() const = 0;
    virtual void setCameraRotationDegrees(float yawDegrees, float pitchDegrees) = 0;
};
