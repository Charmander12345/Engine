#pragma once

#include <memory>

class EngineLevel;
class Object2D;
class Object3D;

class RenderResourceManager
{
public:
    RenderResourceManager() = default;

    bool prepareActiveLevel();

private:
    bool prepareOpenGL(EngineLevel& level);
    bool prepareOpenGLObject2D(const std::shared_ptr<Object2D>& obj2d);
	bool prepareOpenGLObject3D(const std::shared_ptr<Object3D>& obj3d);
};
