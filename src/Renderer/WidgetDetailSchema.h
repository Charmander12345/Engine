#pragma once

#include <string>
#include <functional>
#include <vector>

#include "UIWidget.h"

/// Schema-driven detail panel builder for the Widget Editor and UI Designer.
///
/// Provides shared section builders (Identity, Transform, Layout, Style, etc.)
/// and per-element-type sections (Text, Slider, Image, etc.) so both editors
/// use the same rich property editing UI with ColorPickers, Sliders, DropDowns,
/// CheckBoxes, collapsible sections and Vec2 rows instead of raw text entry.
///
/// Usage:
///   WidgetDetailSchema::buildDetailPanel(
///       "WE.Det",              // unique id prefix (avoids collisions)
///       selected,              // WidgetElement* to edit
///       applyChangeFn,         // lambda called after every change
///       rootPanel,             // WidgetElement* container to append to
///       options);              // optional flags
class WidgetDetailSchema
{
public:
    struct Options
    {
        /// If true, an editable ID field is shown with a rename callback.
        bool showEditableId{ true };

        /// Called after an ID rename (e.g. to update editor selection state).
        std::function<void(const std::string& newId)> onIdRenamed;

        /// If true, a Delete button is appended at the bottom.
        bool showDeleteButton{ false };
        std::function<void()> onDelete;

        /// Called after the hierarchy tree needs a refresh (e.g. after ID rename).
        std::function<void()> onRefreshHierarchy;
    };

    /// Build the full detail panel for a selected WidgetElement.
    static void buildDetailPanel(const std::string& prefix,
                                 WidgetElement* selected,
                                 std::function<void()> applyChange,
                                 WidgetElement* rootPanel,
                                 const Options& options = {});

private:
    // ── Shared sections (applicable to all or most element types) ────────

    static void addIdentitySection(const std::string& prefix,
                                   WidgetElement* sel,
                                   std::function<void()> applyChange,
                                   WidgetElement* root,
                                   const Options& options);

    static void addTransformSection(const std::string& prefix,
                                    WidgetElement* sel,
                                    std::function<void()> applyChange,
                                    WidgetElement* root);

    static void addAnchorSection(const std::string& prefix,
                                 WidgetElement* sel,
                                 std::function<void()> applyChange,
                                 WidgetElement* root);

    static void addHitTestSection(const std::string& prefix,
                                  WidgetElement* sel,
                                  std::function<void()> applyChange,
                                  WidgetElement* root);

    static void addLayoutSection(const std::string& prefix,
                                 WidgetElement* sel,
                                 std::function<void()> applyChange,
                                 WidgetElement* root);

    static void addStyleColorsSection(const std::string& prefix,
                                      WidgetElement* sel,
                                      std::function<void()> applyChange,
                                      WidgetElement* root);

    static void addBrushSection(const std::string& prefix,
                                WidgetElement* sel,
                                std::function<void()> applyChange,
                                WidgetElement* root);

    static void addRenderTransformSection(const std::string& prefix,
                                          WidgetElement* sel,
                                          std::function<void()> applyChange,
                                          WidgetElement* root);

    static void addShadowSection(const std::string& prefix,
                                 WidgetElement* sel,
                                 std::function<void()> applyChange,
                                 WidgetElement* root);

    // ── Per-type sections ────────────────────────────────────────────────

    static void addTextSection(const std::string& prefix,
                               WidgetElement* sel,
                               std::function<void()> applyChange,
                               WidgetElement* root);

    static void addImageSection(const std::string& prefix,
                                WidgetElement* sel,
                                std::function<void()> applyChange,
                                WidgetElement* root);

    static void addValueSection(const std::string& prefix,
                                WidgetElement* sel,
                                std::function<void()> applyChange,
                                WidgetElement* root);

    static void addEntryBarSection(const std::string& prefix,
                                   WidgetElement* sel,
                                   std::function<void()> applyChange,
                                   WidgetElement* root);

    static void addContainerSection(const std::string& prefix,
                                    WidgetElement* sel,
                                    std::function<void()> applyChange,
                                    WidgetElement* root);

    static void addBorderWidgetSection(const std::string& prefix,
                                       WidgetElement* sel,
                                       std::function<void()> applyChange,
                                       WidgetElement* root);

    static void addSpinnerSection(const std::string& prefix,
                                  WidgetElement* sel,
                                  std::function<void()> applyChange,
                                  WidgetElement* root);

    static void addRichTextSection(const std::string& prefix,
                                   WidgetElement* sel,
                                   std::function<void()> applyChange,
                                   WidgetElement* root);

    static void addListViewSection(const std::string& prefix,
                                   WidgetElement* sel,
                                   std::function<void()> applyChange,
                                   WidgetElement* root);

    static void addTileViewSection(const std::string& prefix,
                                   WidgetElement* sel,
                                   std::function<void()> applyChange,
                                   WidgetElement* root);

    static void addFocusSection(const std::string& prefix,
                                WidgetElement* sel,
                                std::function<void()> applyChange,
                                WidgetElement* root);

    static void addDragDropSection(const std::string& prefix,
                                   WidgetElement* sel,
                                   std::function<void()> applyChange,
                                   WidgetElement* root);

    // ── Utilities ────────────────────────────────────────────────────────

    static std::string getTypeName(WidgetElementType type);

    static bool hasTextProperties(WidgetElementType type);
};
