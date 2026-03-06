#pragma once

#include <string>
#include <vector>
#include <memory>

#include "../../Core/MathTypes.h"
#include "../UIWidget.h"

/// Simple editor widget base class.
///
/// Editor widgets are statically defined in code and placed at fixed positions
/// by the UIManager.  They use EditorTheme for consistent styling.
///
/// Unlike GameplayWidget, EditorWidget does **not** support:
///   - JSON serialization / asset files
///   - Animations / WidgetAnimationPlayer
///   - EngineObject inheritance (not managed as an asset)
///
/// Both EditorWidget and GameplayWidget expose their element tree as
/// `std::vector<WidgetElement>`, so the renderer and layout engine work
/// with either type without changes.
class EditorWidget
{
public:
    EditorWidget() = default;
    ~EditorWidget() = default;

    // ── Identity ─────────────────────────────────────────────────────────
    void setName(const std::string& name) { m_name = name; }
    const std::string& getName() const { return m_name; }

    // ── Size / position ──────────────────────────────────────────────────
    void setSizePixels(const Vec2& size) { m_sizePixels = size; m_layoutDirty = true; }
    const Vec2& getSizePixels() const { return m_sizePixels; }

    void setPositionPixels(const Vec2& pos) { m_positionPixels = pos; m_layoutDirty = true; }
    const Vec2& getPositionPixels() const { return m_positionPixels; }

    void setFillX(bool fill) { m_fillX = fill; }
    bool getFillX() const { return m_fillX; }

    void setFillY(bool fill) { m_fillY = fill; }
    bool getFillY() const { return m_fillY; }

    void setAbsolutePosition(bool absolute) { m_absolutePosition = absolute; }
    bool isAbsolutePositioned() const { return m_absolutePosition; }

    // ── Anchor (used by UIManager layout to position editor panels) ──────
    void setAnchor(WidgetAnchor anchor) { m_anchor = anchor; }
    WidgetAnchor getAnchor() const { return m_anchor; }

    // ── Computed layout (written by UIManager::updateLayouts) ────────────
    const Vec2& getComputedSizePixels() const { return m_computedSizePixels; }
    bool hasComputedSize() const { return m_hasComputedSize; }
    void setComputedSizePixels(const Vec2& size, bool has) { m_computedSizePixels = size; m_hasComputedSize = has; }

    const Vec2& getComputedPositionPixels() const { return m_computedPositionPixels; }
    bool hasComputedPosition() const { return m_hasComputedPosition; }
    void setComputedPositionPixels(const Vec2& pos, bool has) { m_computedPositionPixels = pos; m_hasComputedPosition = has; }

    // ── Layout dirty flag ────────────────────────────────────────────────
    void markLayoutDirty() { m_layoutDirty = true; }
    bool isLayoutDirty() const { return m_layoutDirty; }
    void setLayoutDirty(bool dirty) { m_layoutDirty = dirty; }

    // ── Elements (same WidgetElement used by renderer/layout) ────────────
    void setElements(std::vector<WidgetElement> elements) { m_elements = std::move(elements); m_layoutDirty = true; }
    const std::vector<WidgetElement>& getElements() const { return m_elements; }
    std::vector<WidgetElement>& getElementsMutable() { return m_elements; }

    // ── Z-Order ──────────────────────────────────────────────────────────
    void setZOrder(int z) { m_zOrder = z; }
    int getZOrder() const { return m_zOrder; }

    // ── Factory: create from a loaded Widget (transition helper) ─────────
    /// Copies basic properties and element tree from a gameplay Widget.
    /// Used during the transition period where AssetManager still loads
    /// editor widget JSON files as Widget objects.
    static std::shared_ptr<EditorWidget> fromWidget(const std::shared_ptr<Widget>& source)
    {
        if (!source) return nullptr;
        auto ew = std::make_shared<EditorWidget>();
        ew->m_name              = source->getName();
        ew->m_sizePixels        = source->getSizePixels();
        ew->m_positionPixels    = source->getPositionPixels();
        ew->m_anchor            = source->getAnchor();
        ew->m_fillX             = source->getFillX();
        ew->m_fillY             = source->getFillY();
        ew->m_absolutePosition  = source->isAbsolutePositioned();
        ew->m_zOrder            = source->getZOrder();
        ew->m_elements          = source->getElements();
        return ew;
    }

private:
    std::string m_name;
    Vec2 m_sizePixels{};
    Vec2 m_positionPixels{};
    WidgetAnchor m_anchor{ WidgetAnchor::TopLeft };
    bool m_fillX{ false };
    bool m_fillY{ false };
    bool m_absolutePosition{ false };
    Vec2 m_computedSizePixels{};
    Vec2 m_computedPositionPixels{};
    bool m_hasComputedSize{ false };
    bool m_hasComputedPosition{ false };
    bool m_layoutDirty{ true };
    std::vector<WidgetElement> m_elements;
    int m_zOrder{ 0 };
};
