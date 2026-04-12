#pragma once
#if ENGINE_EDITOR

#include <string>
#include <vector>
#include <functional>
#include "../../Core/Reflection.h"

struct WidgetElement;

/// Generates themed editor WidgetElements from reflected PropertyInfo
/// metadata.  Each TypeID maps to the appropriate EditorUIBuilder
/// helper (Float→FloatRow, Bool→CheckBox, Vec3→Vec3Row, etc.).
///
/// This factory is used to auto-generate Details Panel property rows
/// from a ClassInfo, eliminating the need for hand-coded UI per
/// component type.
class ReflectionWidgetFactory
{
public:
    /// Callback invoked when any property changes.  Parameters:
    /// (propertyName, pointerToComponentInstance).
    /// The caller is responsible for undo/redo and ECS writeback.
    using OnPropertyChanged = std::function<void(const PropertyInfo& prop, void* instance)>;

    /// Build a list of WidgetElements for all editable properties of
    /// the given ClassInfo.  The instance pointer points to the live
    /// component data in the ECS (or a copy).
    ///
    /// @param classInfo    The reflected class metadata.
    /// @param instance     Pointer to the component data.
    /// @param idPrefix     Unique prefix for widget IDs (e.g. "Details.Physics").
    /// @param entity       The owning entity id (used for undo/redo scoping).
    /// @param onChange      Called after any property value changes.
    static std::vector<WidgetElement> buildPropertyWidgets(
        const ClassInfo&    classInfo,
        void*               instance,
        const std::string&  idPrefix,
        unsigned int        entity,
        OnPropertyChanged   onChange = {});

    /// Build a single WidgetElement for one property.
    static WidgetElement buildWidgetForProperty(
        const PropertyInfo& prop,
        void*               instance,
        const std::string&  idPrefix,
        unsigned int        entity,
        OnPropertyChanged   onChange = {});
};

#endif // ENGINE_EDITOR
