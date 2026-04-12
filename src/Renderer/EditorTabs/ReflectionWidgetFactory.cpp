#include "ReflectionWidgetFactory.h"

#if ENGINE_EDITOR

#include "../EditorUIBuilder.h"
#include "../UIWidget.h"
#include "../../Core/MathTypes.h"
#include "../../Core/Reflection.h"
#include "../../Diagnostics/DiagnosticsManager.h"

#include <string>
#include <cstring>

// ── Helpers ──────────────────────────────────────────────────────────

/// Pretty-print a property name: "maxLinearVelocity" → "Max Linear Velocity"
static std::string prettyName(const char* raw)
{
    if (!raw || raw[0] == '\0') return {};
    std::string out;
    out += static_cast<char>(std::toupper(static_cast<unsigned char>(raw[0])));
    for (size_t i = 1; raw[i] != '\0'; ++i)
    {
        char c = raw[i];
        if (std::isupper(static_cast<unsigned char>(c)) &&
            i > 0 && std::islower(static_cast<unsigned char>(raw[i - 1])))
        {
            out += ' ';
        }
        out += c;
    }
    return out;
}

static void markUnsaved()
{
    if (auto* level = DiagnosticsManager::Instance().getActiveLevelSoft())
        level->setIsSaved(false);
}

// ── Widget builders per TypeID ───────────────────────────────────────

WidgetElement ReflectionWidgetFactory::buildWidgetForProperty(
    const PropertyInfo& prop,
    void*               instance,
    const std::string&  idPrefix,
    unsigned int        entity,
    OnPropertyChanged   onChange)
{
    const std::string id    = idPrefix + "." + prop.name;
    const std::string label = prettyName(prop.name);
    const bool readOnly     = (prop.flags & PF_VisibleOnly) != 0;

    switch (prop.typeID)
    {
    // ── Float ────────────────────────────────────────────────────────
    case TypeID::Float:
    {
        float value = *prop.ptrIn<float>(instance);
        if (prop.clampMin != 0.0f || prop.clampMax != 0.0f)
        {
            return EditorUIBuilder::makeSliderRow(id, label, value,
                prop.clampMin, prop.clampMax,
                [&prop, instance, onChange](float v) {
                    *const_cast<float*>(prop.ptrIn<float>(instance)) = v;
                    markUnsaved();
                    if (onChange) onChange(prop, instance);
                });
        }
        return EditorUIBuilder::makeFloatRow(id, label, value,
            [&prop, instance, onChange](float v) {
                *const_cast<float*>(prop.ptrIn<float>(instance)) = v;
                markUnsaved();
                if (onChange) onChange(prop, instance);
            });
    }

    // ── Int ──────────────────────────────────────────────────────────
    case TypeID::Int:
    {
        int value = *prop.ptrIn<int>(instance);
        return EditorUIBuilder::makeIntRow(id, label, value,
            [&prop, instance, onChange](int v) {
                *const_cast<int*>(prop.ptrIn<int>(instance)) = v;
                markUnsaved();
                if (onChange) onChange(prop, instance);
            });
    }

    // ── Bool ─────────────────────────────────────────────────────────
    case TypeID::Bool:
    {
        bool value = *prop.ptrIn<bool>(instance);
        return EditorUIBuilder::makeCheckBox(id, label, value,
            [&prop, instance, onChange](bool v) {
                *const_cast<bool*>(prop.ptrIn<bool>(instance)) = v;
                markUnsaved();
                if (onChange) onChange(prop, instance);
            });
    }

    // ── String / AssetPath ───────────────────────────────────────────
    case TypeID::String:
    case TypeID::AssetPath:
    {
        const std::string& value = *prop.ptrIn<std::string>(instance);
        if (readOnly)
            return EditorUIBuilder::makeLabel(label + ": " + value);

        return EditorUIBuilder::makeStringRow(id, label, value,
            [&prop, instance, onChange](const std::string& v) {
                *const_cast<std::string*>(prop.ptrIn<std::string>(instance)) = v;
                markUnsaved();
                if (onChange) onChange(prop, instance);
            });
    }

    // ── Vec3 ─────────────────────────────────────────────────────────
    case TypeID::Vec3:
    {
        const float* values = prop.ptrIn<float>(instance);
        return EditorUIBuilder::makeVec3Row(id, label, values,
            [&prop, instance, onChange](int axis, float v) {
                float* arr = const_cast<float*>(prop.ptrIn<float>(instance));
                arr[axis] = v;
                markUnsaved();
                if (onChange) onChange(prop, instance);
            });
    }

    // ── Vec2 ─────────────────────────────────────────────────────────
    case TypeID::Vec2:
    {
        const float* values = prop.ptrIn<float>(instance);
        Vec2 v2{ values[0], values[1] };
        return EditorUIBuilder::makeVec2Row(id, label, v2,
            [&prop, instance, onChange](int axis, float v) {
                float* arr = const_cast<float*>(prop.ptrIn<float>(instance));
                arr[axis] = v;
                markUnsaved();
                if (onChange) onChange(prop, instance);
            });
    }

    // ── Color3 (RGB) ─────────────────────────────────────────────────
    case TypeID::Color3:
    {
        const float* rgb = prop.ptrIn<float>(instance);
        Vec4 color{ rgb[0], rgb[1], rgb[2], 1.0f };
        return EditorUIBuilder::makeColorPickerRow(id, label, color,
            [&prop, instance, onChange](const Vec4& c) {
                float* arr = const_cast<float*>(prop.ptrIn<float>(instance));
                arr[0] = c.x; arr[1] = c.y; arr[2] = c.z;
                markUnsaved();
                if (onChange) onChange(prop, instance);
            });
    }

    // ── Color4 (RGBA) ────────────────────────────────────────────────
    case TypeID::Color4:
    {
        const float* rgba = prop.ptrIn<float>(instance);
        Vec4 color{ rgba[0], rgba[1], rgba[2], rgba[3] };
        return EditorUIBuilder::makeColorPickerRow(id, label, color,
            [&prop, instance, onChange](const Vec4& c) {
                float* arr = const_cast<float*>(prop.ptrIn<float>(instance));
                arr[0] = c.x; arr[1] = c.y; arr[2] = c.z; arr[3] = c.w;
                markUnsaved();
                if (onChange) onChange(prop, instance);
            });
    }

    // ── Enum ─────────────────────────────────────────────────────────
    case TypeID::Enum:
    {
        int currentValue = *prop.ptrIn<int>(instance);
        std::vector<std::string> names;
        int selectedIdx = 0;
        for (int i = 0; i < prop.enumCount; ++i)
        {
            names.push_back(prop.enumEntries[i].name);
            if (prop.enumEntries[i].value == currentValue)
                selectedIdx = i;
        }
        return EditorUIBuilder::makeDropDownRow(id, label, names, selectedIdx,
            [&prop, instance, onChange](int idx) {
                if (idx >= 0 && idx < prop.enumCount)
                {
                    *const_cast<int*>(prop.ptrIn<int>(instance)) = prop.enumEntries[idx].value;
                    markUnsaved();
                    if (onChange) onChange(prop, instance);
                }
            });
    }

    // ── EntityRef ────────────────────────────────────────────────────
    case TypeID::EntityRef:
    {
        unsigned int value = *prop.ptrIn<unsigned int>(instance);
        return EditorUIBuilder::makeIntRow(id, label, static_cast<int>(value),
            [&prop, instance, onChange](int v) {
                *const_cast<unsigned int*>(prop.ptrIn<unsigned int>(instance)) =
                    static_cast<unsigned int>(v);
                markUnsaved();
                if (onChange) onChange(prop, instance);
            });
    }

    // ── Custom / fallback ────────────────────────────────────────────
    case TypeID::Custom:
    default:
        return EditorUIBuilder::makeLabel(label + ": (custom)");
    }
}

// ── Build all property widgets for a ClassInfo ──────────────────────

std::vector<WidgetElement> ReflectionWidgetFactory::buildPropertyWidgets(
    const ClassInfo&    classInfo,
    void*               instance,
    const std::string&  idPrefix,
    unsigned int        entity,
    OnPropertyChanged   onChange)
{
    std::vector<WidgetElement> widgets;
    widgets.reserve(classInfo.properties.size());

    for (const auto& prop : classInfo.properties)
    {
        // Skip hidden and transient-hidden properties
        if ((prop.flags & PF_Hidden) != 0)
            continue;

        WidgetElement w = buildWidgetForProperty(prop, instance, idPrefix, entity, onChange);
        w.runtimeOnly = true;
        widgets.push_back(std::move(w));
    }

    return widgets;
}

#endif // ENGINE_EDITOR
