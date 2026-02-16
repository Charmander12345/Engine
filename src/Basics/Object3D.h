#pragma once

#include "RenderableObject.h"

class Object3D : public RenderableObject
{
public:
    Object3D() = default;
    ~Object3D() override = default;

    bool is3D() const override { return true; }
};
