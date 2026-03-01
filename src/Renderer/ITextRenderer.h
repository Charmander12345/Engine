#pragma once

#include <string>

#include "../Core/MathTypes.h"

class ITextRenderer
{
public:
    virtual ~ITextRenderer() = default;

    virtual bool initialize(const std::string& fontPath, const std::string& vertexShaderPath, const std::string& fragmentShaderPath) = 0;
    virtual void shutdown() = 0;
    virtual void setScreenSize(int width, int height) = 0;
    virtual void drawText(const std::string& text, const Vec2& screenPos, float scale, const Vec4& color) = 0;
    virtual Vec2 measureText(const std::string& text, float scale) const = 0;
    virtual float getLineHeight(float scale) const = 0;
};
