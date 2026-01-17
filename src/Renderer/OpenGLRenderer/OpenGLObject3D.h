#pragma once

#include <memory>

class Object3D;

class OpenGLObject3D
{
public:
    explicit OpenGLObject3D(const std::shared_ptr<Object3D>& obj);

    bool prepare();

private:
    std::shared_ptr<Object3D> m_obj;
};
