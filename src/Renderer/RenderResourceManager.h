#pragma once

#include <memory>

class EngineLevel;
class Object2D;

class RenderResourceManager
{
public:
    RenderResourceManager() = default;

    bool prepareActiveLevel();

private:
    bool prepareOpenGL(EngineLevel& level);
    bool prepareOpenGLObject2D(const std::shared_ptr<Object2D>& obj2d);
};
