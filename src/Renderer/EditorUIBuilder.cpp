#include "EditorUIBuilder.h"

#include <cstdio>
#include <cctype>

#include "UIWidgets/EntryBarWidget.h"
#include "UIWidgets/CheckBoxWidget.h"
#include "UIWidgets/SeparatorWidget.h"

// ── Text / labels ────────────────────────────────────────────────────────

WidgetElement EditorUIBuilder::makeLabel(const std::string& text, float minWidth)
{
    const auto& t = EditorTheme::Get();

    WidgetElement el{};
    el.type        = WidgetElementType::Text;
    el.text        = text;
    el.font        = t.fontDefault;
    el.fontSize    = t.fontSizeBody;
    el.textAlignH  = TextAlignH::Left;
    el.textAlignV  = TextAlignV::Center;
    el.style.textColor = t.textPrimary;
    el.fillX       = true;
    el.minSize     = Vec2{ minWidth, t.rowHeightSmall };
    el.runtimeOnly = true;
    return el;
}

WidgetElement EditorUIBuilder::makeSecondaryLabel(const std::string& text, float minWidth)
{
    const auto& t = EditorTheme::Get();

    WidgetElement el{};
    el.type        = WidgetElementType::Text;
    el.text        = text;
    el.font        = t.fontDefault;
    el.fontSize    = t.fontSizeSmall;
    el.textAlignH  = TextAlignH::Left;
    el.textAlignV  = TextAlignV::Center;
    el.style.textColor = t.textSecondary;
    el.fillX       = true;
    el.minSize     = Vec2{ minWidth, t.rowHeightSmall };
    el.runtimeOnly = true;
    return el;
}

WidgetElement EditorUIBuilder::makeHeading(const std::string& text)
{
    const auto& t = EditorTheme::Get();

    WidgetElement el{};
    el.type        = WidgetElementType::Text;
    el.text        = text;
    el.font        = t.fontDefault;
    el.fontSize    = t.fontSizeSubheading;
    el.textAlignH  = TextAlignH::Left;
    el.textAlignV  = TextAlignV::Center;
    el.style.textColor = t.textPrimary;
    el.fillX       = true;
    el.minSize     = Vec2{ 0.0f, t.sectionHeaderHeight };
    el.runtimeOnly = true;
    return el;
}

// ── Buttons ──────────────────────────────────────────────────────────────

static WidgetElement makeButtonBase(const std::string& id,
                                    const std::string& text,
                                    const Vec4& bg,
                                    const Vec4& hover,
                                    const Vec4& textColor,
                                    std::function<void()> onClick,
                                    Vec2 minSize)
{
    const auto& t = EditorTheme::Get();

    WidgetElement el{};
    el.id             = id;
    el.type           = WidgetElementType::Button;
    el.text           = text;
    el.font           = t.fontDefault;
    el.fontSize       = t.fontSizeBody;
    el.textAlignH     = TextAlignH::Center;
    el.textAlignV     = TextAlignV::Center;
    el.padding        = t.paddingNormal;
    el.minSize        = (minSize.x > 0.0f || minSize.y > 0.0f) ? minSize : Vec2{ 0.0f, t.rowHeight };
    el.style.color      = bg;
    el.style.hoverColor = hover;
    el.style.textColor  = textColor;
    el.shaderVertex   = "button_vertex.glsl";
    el.shaderFragment = "button_fragment.glsl";
    el.hitTestMode    = HitTestMode::Enabled;
    el.fillX          = true;
    el.runtimeOnly    = true;
    if (onClick) el.onClicked = std::move(onClick);
    return el;
}

WidgetElement EditorUIBuilder::makeButton(const std::string& id,
                                          const std::string& text,
                                          std::function<void()> onClick,
                                          Vec2 minSize)
{
    const auto& t = EditorTheme::Get();
    return makeButtonBase(id, text, t.buttonDefault, t.buttonHover, t.buttonText,
                          std::move(onClick), minSize);
}

WidgetElement EditorUIBuilder::makePrimaryButton(const std::string& id,
                                                 const std::string& text,
                                                 std::function<void()> onClick,
                                                 Vec2 minSize)
{
    const auto& t = EditorTheme::Get();
    return makeButtonBase(id, text, t.buttonPrimary, t.buttonPrimaryHover, t.buttonPrimaryText,
                          std::move(onClick), minSize);
}

WidgetElement EditorUIBuilder::makeDangerButton(const std::string& id,
                                                const std::string& text,
                                                std::function<void()> onClick,
                                                Vec2 minSize)
{
    const auto& t = EditorTheme::Get();
    return makeButtonBase(id, text, t.buttonDanger, t.buttonDangerHover, t.buttonDangerText,
                          std::move(onClick), minSize);
}

WidgetElement EditorUIBuilder::makeSubtleButton(const std::string& id,
                                                const std::string& text,
                                                std::function<void()> onClick,
                                                Vec2 minSize)
{
    const auto& t = EditorTheme::Get();
    return makeButtonBase(id, text, t.buttonSubtle, t.buttonSubtleHover, t.textPrimary,
                          std::move(onClick), minSize);
}

// ── Input controls ───────────────────────────────────────────────────────

WidgetElement EditorUIBuilder::makeEntryBar(const std::string& id,
                                            const std::string& value,
                                            std::function<void(const std::string&)> onValueChanged,
                                            float minWidth)
{
    const auto& t = EditorTheme::Get();

    EntryBarWidget entry;
    entry.setValue(value);
    entry.setFont(t.fontDefault);
    entry.setFontSize(t.fontSizeSmall);
    entry.setMinSize(Vec2{ minWidth, t.rowHeightSmall + 2.0f });
    entry.setPadding(t.paddingSmall);
    entry.setBackgroundColor(t.inputBackground);
    entry.setTextColor(t.inputText);
    if (onValueChanged) entry.setOnValueChanged(std::move(onValueChanged));

    WidgetElement el = entry.toElement();
    el.id          = id;
    el.fillX       = true;
    el.runtimeOnly = true;
    return el;
}

WidgetElement EditorUIBuilder::makeCheckBox(const std::string& id,
                                            const std::string& label,
                                            bool checked,
                                            std::function<void(bool)> onCheckedChanged)
{
    const auto& t = EditorTheme::Get();

    CheckBoxWidget cb;
    cb.setChecked(checked);
    cb.setLabel(label);
    cb.setFont(t.fontDefault);
    cb.setFontSize(t.fontSizeSmall);
    cb.setMinSize(Vec2{ 0.0f, t.rowHeightSmall });
    cb.setPadding(t.paddingSmall);
    if (onCheckedChanged) cb.setOnCheckedChanged(std::move(onCheckedChanged));

    WidgetElement el = cb.toElement();
    el.id          = id;
    el.fillX       = true;
    el.runtimeOnly = true;
    return el;
}

WidgetElement EditorUIBuilder::makeDropDown(const std::string& id,
                                            const std::vector<std::string>& items,
                                            int selectedIndex,
                                            std::function<void(int)> onSelectionChanged)
{
    const auto& t = EditorTheme::Get();

    WidgetElement el{};
    el.id            = id;
    el.type          = WidgetElementType::DropDown;
    el.font          = t.fontDefault;
    el.fontSize      = t.fontSizeSmall;
    el.minSize       = Vec2{ 0.0f, t.rowHeightSmall };
    el.padding       = t.paddingSmall;
    el.style.color      = t.dropdownBackground;
    el.style.hoverColor = t.dropdownHover;
    el.style.textColor  = t.dropdownText;
    el.items         = items;
    el.selectedIndex = selectedIndex;
    el.hitTestMode   = HitTestMode::Enabled;
    el.fillX         = true;
    el.runtimeOnly   = true;
    if (onSelectionChanged) el.onSelectionChanged = std::move(onSelectionChanged);
    return el;
}

// ── Property rows ────────────────────────────────────────────────────────

WidgetElement EditorUIBuilder::makeFloatRow(const std::string& id,
                                            const std::string& label,
                                            float value,
                                            std::function<void(float)> onChange)
{
    const auto& t = EditorTheme::Get();

    WidgetElement row = makeHorizontalRow(id, Vec2{ 0.0f, 1.0f });

    WidgetElement lbl = makeLabel(label, 100.0f);
    lbl.fillX = false;
    row.children.push_back(std::move(lbl));

    WidgetElement entry = makeEntryBar(id + ".Value", fmtFloat(value),
        [onChange = std::move(onChange)](const std::string& val) {
            try { onChange(std::stof(val)); } catch (...) {}
        });
    row.children.push_back(std::move(entry));

    return row;
}

WidgetElement EditorUIBuilder::makeVec3Row(const std::string& idPrefix,
                                           const std::string& label,
                                           const float values[3],
                                           std::function<void(int axis, float value)> onChange)
{
    const auto& t = EditorTheme::Get();

    WidgetElement row = makeHorizontalRow(idPrefix, Vec2{ 0.0f, 1.0f });

    WidgetElement lbl = makeLabel(label, 100.0f);
    lbl.fillX = false;
    row.children.push_back(std::move(lbl));

    const char* axes[] = { "X", "Y", "Z" };
    const Vec4 axisColors[] = {
        { 0.22f, 0.10f, 0.10f, 0.9f },
        { 0.10f, 0.20f, 0.10f, 0.9f },
        { 0.10f, 0.10f, 0.22f, 0.9f },
    };

    for (int i = 0; i < 3; ++i)
    {
        EntryBarWidget entry;
        entry.setValue(fmtFloat(values[i]));
        entry.setFont(t.fontDefault);
        entry.setFontSize(t.fontSizeSmall);
        entry.setMinSize(Vec2{ 0.0f, t.rowHeightSmall });
        entry.setPadding(t.paddingSmall);
        entry.setBackgroundColor(axisColors[i]);
        entry.setTextColor(t.inputText);

        int axis = i;
        entry.setOnValueChanged([onChange, axis](const std::string& val) {
            try { onChange(axis, std::stof(val)); } catch (...) {}
        });

        WidgetElement entryEl = entry.toElement();
        entryEl.id          = idPrefix + "." + axes[i];
        entryEl.fillX       = true;
        entryEl.runtimeOnly = true;
        row.children.push_back(std::move(entryEl));
    }

    return row;
}

// ── Layout helpers ───────────────────────────────────────────────────────

WidgetElement EditorUIBuilder::makeHorizontalRow(const std::string& id, Vec2 padding)
{
    WidgetElement el{};
    el.id            = id;
    el.type          = WidgetElementType::StackPanel;
    el.orientation   = StackOrientation::Horizontal;
    el.fillX         = true;
    el.sizeToContent = true;
    el.style.color   = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    el.padding       = padding;
    el.runtimeOnly   = true;
    return el;
}

WidgetElement EditorUIBuilder::makeVerticalStack(const std::string& id, Vec2 padding)
{
    WidgetElement el{};
    el.id            = id;
    el.type          = WidgetElementType::StackPanel;
    el.orientation   = StackOrientation::Vertical;
    el.fillX         = true;
    el.sizeToContent = true;
    el.style.color   = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
    el.padding       = padding;
    el.runtimeOnly   = true;
    return el;
}

WidgetElement EditorUIBuilder::makeDivider()
{
    const auto& t = EditorTheme::Get();

    WidgetElement el{};
    el.type          = WidgetElementType::Panel;
    el.fillX         = true;
    el.minSize       = Vec2{ 0.0f, t.separatorThickness };
    el.style.color   = t.panelBorder;
    el.runtimeOnly   = true;
    return el;
}

// ── Collapsible section ──────────────────────────────────────────────────

WidgetElement EditorUIBuilder::makeSection(const std::string& id,
                                           const std::string& title,
                                           const std::vector<WidgetElement>& children)
{
    const auto& t = EditorTheme::Get();

    SeparatorWidget sep;
    sep.setId(id.empty() ? sanitizeId(title) : id);
    sep.setTitle(title);
    sep.setFont(t.fontDefault);
    sep.setFontSize(t.fontSizeBody);
    sep.setHeaderColors(t.panelHeader, t.buttonHover);
    sep.setTitleColor(t.textPrimary);
    sep.setContentPadding(Vec2{ 14.0f, t.paddingNormal.y });
    sep.setChildren(children);

    return sep.toElement();
}

// ── Utility ──────────────────────────────────────────────────────────────

std::string EditorUIBuilder::fmtFloat(float v)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return std::string(buf);
}

std::string EditorUIBuilder::sanitizeId(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    for (unsigned char c : text)
    {
        result.push_back(std::isalnum(c) ? static_cast<char>(c) : '_');
    }
    if (result.empty()) result = "Section";
    return result;
}
