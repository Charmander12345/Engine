#pragma once

class Material
{
public:
    virtual ~Material() = default;
    virtual bool build() = 0;
    virtual void bind() = 0;
    virtual void unbind() = 0;
    virtual void render() = 0;
};
