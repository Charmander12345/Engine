#pragma once

#include <functional>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"
#include "EntryBarWidget.h"

class ColorPickerWidget
{
public:
    void setColor(const Vec4& color) { m_color = color; }
    const Vec4& getColor() const { return m_color; }

    void setMinSize(const Vec2& size) { m_minSize = size; }
    const Vec2& getMinSize() const { return m_minSize; }

    void setHitTestable(bool hitTestable) { m_hitTestable = hitTestable; }
    bool isHitTestable() const { return m_hitTestable; }

    void setCompact(bool compact) { m_compact = compact; }
    bool isCompact() const { return m_compact; }

    void setOnColorChanged(std::function<void(const Vec4&)> callback) { m_onColorChanged = std::move(callback); }
    const std::function<void(const Vec4&)>& getOnColorChanged() const { return m_onColorChanged; }

    WidgetElement toElement() const;

private:
    Vec4 m_color{ 1.0f, 1.0f, 1.0f, 1.0f };
    Vec2 m_minSize{};
    bool m_hitTestable{ true };
    bool m_compact{ false };
    std::function<void(const Vec4&)> m_onColorChanged;
};
