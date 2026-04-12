#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

// ═══════════════════════════════════════════════════════════════════════
// TypeID — describes the kind of value a reflected property holds
// ═══════════════════════════════════════════════════════════════════════

enum class TypeID : uint8_t
{
    Float,          // single float
    Int,            // int (also used for enums stored as int)
    Bool,           // bool
    String,         // std::string
    Vec3,           // float[3] — position / rotation / scale / etc.
    Vec2,           // float[2]
    Color3,         // float[3] treated as RGB colour
    Color4,         // float[4] treated as RGBA colour
    Enum,           // int-backed enum with named entries
    AssetPath,      // std::string with an asset-type hint
    EntityRef,      // unsigned int entity id
    Custom          // opaque / not auto-editable
};

// ═══════════════════════════════════════════════════════════════════════
// PropertyFlags — bitwise flags controlling editor / serialisation behaviour
// ═══════════════════════════════════════════════════════════════════════

enum PropertyFlags : uint32_t
{
    PF_None          = 0,
    PF_EditAnywhere  = 1 << 0,   // Editable in any Details Panel
    PF_VisibleOnly   = 1 << 1,   // Read-only in the editor
    PF_Transient     = 1 << 2,   // Excluded from serialisation
    PF_Hidden        = 1 << 3    // Hidden in the editor
};

// ═══════════════════════════════════════════════════════════════════════
// EnumEntry — describes one named value of a reflected enum property
// ═══════════════════════════════════════════════════════════════════════

struct EnumEntry
{
    const char* name;
    int         value;
};

// ═══════════════════════════════════════════════════════════════════════
// PropertyInfo — metadata for one member variable
// ═══════════════════════════════════════════════════════════════════════

struct PropertyInfo
{
    const char*      name{ nullptr };
    TypeID           typeID{ TypeID::Custom };
    size_t           offset{ 0 };
    size_t           size{ 0 };
    uint32_t         flags{ PF_EditAnywhere };
    const char*      category{ "" };
    float            clampMin{ 0.0f };
    float            clampMax{ 0.0f };
    const EnumEntry* enumEntries{ nullptr };  // Points to a static array
    int              enumCount{ 0 };

    /// Get a typed pointer into a component instance.
    template<typename T>
    T* ptrIn(void* instance) const
    {
        return reinterpret_cast<T*>(static_cast<char*>(instance) + offset);
    }

    template<typename T>
    const T* ptrIn(const void* instance) const
    {
        return reinterpret_cast<const T*>(static_cast<const char*>(instance) + offset);
    }
};

// ═══════════════════════════════════════════════════════════════════════
// ClassInfo — metadata for a reflected class / struct
// ═══════════════════════════════════════════════════════════════════════

struct ClassInfo
{
    const char*              className{ nullptr };
    std::vector<PropertyInfo> properties;
    const ClassInfo*         superClass{ nullptr };
};

// ═══════════════════════════════════════════════════════════════════════
// TypeRegistry — singleton that maps std::type_index → ClassInfo
// ═══════════════════════════════════════════════════════════════════════

class TypeRegistry
{
public:
    static TypeRegistry& Instance();

    /// Register a ClassInfo for type T.
    template<typename T>
    void registerClass(const ClassInfo& info)
    {
        m_registry[std::type_index(typeid(T))] = &info;
    }

    /// Look up the ClassInfo for type T (returns nullptr if unregistered).
    template<typename T>
    const ClassInfo* getClassInfo() const
    {
        auto it = m_registry.find(std::type_index(typeid(T)));
        return it != m_registry.end() ? it->second : nullptr;
    }

    /// Look up the ClassInfo by type_index.
    const ClassInfo* getClassInfo(const std::type_index& ti) const;

    /// Look up the ClassInfo by class name string.
    const ClassInfo* getClassInfoByName(const char* className) const;

    /// Return all registered class infos (for iteration).
    const std::unordered_map<std::type_index, const ClassInfo*>& all() const
    {
        return m_registry;
    }

private:
    TypeRegistry() = default;
    std::unordered_map<std::type_index, const ClassInfo*> m_registry;
};

// ═══════════════════════════════════════════════════════════════════════
// Registration macros — non-intrusive, for POD structs and classes
// ═══════════════════════════════════════════════════════════════════════
//
// Usage:
//   REFLECT_BEGIN(TransformComponent, "Transform")
//       REFLECT_VEC3  (position, "Geometry", PF_EditAnywhere)
//       REFLECT_VEC3  (rotation, "Geometry", PF_EditAnywhere)
//       REFLECT_VEC3  (scale,    "Geometry", PF_EditAnywhere)
//       REFLECT_BOOL  (dirty,    "",         PF_Transient | PF_Hidden)
//   REFLECT_END(TransformComponent)

#define REFLECT_BEGIN(ClassName, DisplayName) \
    inline const ClassInfo& ClassName##_Reflect() { \
        using T = ClassName; \
        static ClassInfo info{ DisplayName, {

#define REFLECT_FLOAT(member, category, flags) \
    { #member, TypeID::Float, offsetof(T, member), sizeof(T::member), flags, category },

#define REFLECT_FLOAT_CLAMPED(member, category, flags, cmin, cmax) \
    { #member, TypeID::Float, offsetof(T, member), sizeof(T::member), flags, category, cmin, cmax },

#define REFLECT_INT(member, category, flags) \
    { #member, TypeID::Int, offsetof(T, member), sizeof(T::member), flags, category },

#define REFLECT_BOOL(member, category, flags) \
    { #member, TypeID::Bool, offsetof(T, member), sizeof(T::member), flags, category },

#define REFLECT_STRING(member, category, flags) \
    { #member, TypeID::String, offsetof(T, member), sizeof(T::member), flags, category },

#define REFLECT_VEC3(member, category, flags) \
    { #member, TypeID::Vec3, offsetof(T, member), sizeof(T::member), flags, category },

#define REFLECT_VEC2(member, category, flags) \
    { #member, TypeID::Vec2, offsetof(T, member), sizeof(T::member), flags, category },

#define REFLECT_COLOR3(member, category, flags) \
    { #member, TypeID::Color3, offsetof(T, member), sizeof(T::member), flags, category },

#define REFLECT_COLOR4(member, category, flags) \
    { #member, TypeID::Color4, offsetof(T, member), sizeof(T::member), flags, category },

#define REFLECT_ENUM(member, category, flags, entries, count) \
    { #member, TypeID::Enum, offsetof(T, member), sizeof(T::member), flags, category, 0.0f, 0.0f, entries, count },

#define REFLECT_ASSET_PATH(member, category, flags) \
    { #member, TypeID::AssetPath, offsetof(T, member), sizeof(T::member), flags, category },

#define REFLECT_ENTITY_REF(member, category, flags) \
    { #member, TypeID::EntityRef, offsetof(T, member), sizeof(T::member), flags, category },

#define REFLECT_END(ClassName) \
        }}; \
        return info; \
    } \
    static const bool s_##ClassName##_registered = \
        (TypeRegistry::Instance().registerClass<ClassName>(ClassName##_Reflect()), true);

/// Variant for nested types (e.g. LodComponent::LodLevel) where :: in
/// the token-paste would produce invalid identifiers.  The Alias is a
/// flat name used only for the generated symbol names.
#define REFLECT_BEGIN_NESTED(QualifiedType, Alias, DisplayName) \
    inline const ClassInfo& Alias##_Reflect() { \
        using T = QualifiedType; \
        static ClassInfo info{ DisplayName, {

#define REFLECT_END_NESTED(QualifiedType, Alias) \
        }}; \
        return info; \
    } \
    static const bool s_##Alias##_registered = \
        (TypeRegistry::Instance().registerClass<QualifiedType>(Alias##_Reflect()), true);

// ═══════════════════════════════════════════════════════════════════════
// Reflection utilities — property-level comparison and copying
// ═══════════════════════════════════════════════════════════════════════

/// Compare a single reflected property between two instances.
/// Returns true if the values are identical.
bool reflectPropertyEquals(const PropertyInfo& prop, const void* a, const void* b);

/// Copy all reflected properties from src to dst.
void reflectCopyProperties(const ClassInfo& info, const void* src, void* dst);

/// Find all properties whose values differ between two instances.
std::vector<const PropertyInfo*> reflectDiffProperties(
    const ClassInfo& info, const void* a, const void* b);
