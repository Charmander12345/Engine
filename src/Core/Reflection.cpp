#include "Reflection.h"
#include <cstring>

// ── TypeRegistry singleton ──────────────────────────────────────────

TypeRegistry& TypeRegistry::Instance()
{
    static TypeRegistry instance;
    return instance;
}

const ClassInfo* TypeRegistry::getClassInfo(const std::type_index& ti) const
{
    auto it = m_registry.find(ti);
    return it != m_registry.end() ? it->second : nullptr;
}

const ClassInfo* TypeRegistry::getClassInfoByName(const char* className) const
{
    if (!className) return nullptr;
    for (const auto& [ti, info] : m_registry)
    {
        if (info && info->className && std::strcmp(info->className, className) == 0)
            return info;
    }
    return nullptr;
}

// ── Reflection utilities ────────────────────────────────────────────

bool reflectPropertyEquals(const PropertyInfo& prop, const void* a, const void* b)
{
    const char* ptrA = static_cast<const char*>(a) + prop.offset;
    const char* ptrB = static_cast<const char*>(b) + prop.offset;

    switch (prop.typeID)
    {
    case TypeID::Float:
        return *reinterpret_cast<const float*>(ptrA) == *reinterpret_cast<const float*>(ptrB);
    case TypeID::Int:
    case TypeID::Enum:
        return *reinterpret_cast<const int*>(ptrA) == *reinterpret_cast<const int*>(ptrB);
    case TypeID::Bool:
        return *reinterpret_cast<const bool*>(ptrA) == *reinterpret_cast<const bool*>(ptrB);
    case TypeID::String:
    case TypeID::AssetPath:
        return *reinterpret_cast<const std::string*>(ptrA) == *reinterpret_cast<const std::string*>(ptrB);
    case TypeID::Vec3:
    case TypeID::Color3:
        return std::memcmp(ptrA, ptrB, sizeof(float) * 3) == 0;
    case TypeID::Vec2:
        return std::memcmp(ptrA, ptrB, sizeof(float) * 2) == 0;
    case TypeID::Color4:
        return std::memcmp(ptrA, ptrB, sizeof(float) * 4) == 0;
    case TypeID::EntityRef:
        return *reinterpret_cast<const unsigned int*>(ptrA) == *reinterpret_cast<const unsigned int*>(ptrB);
    case TypeID::Custom:
    default:
        return std::memcmp(ptrA, ptrB, prop.size) == 0;
    }
}

void reflectCopyProperties(const ClassInfo& info, const void* src, void* dst)
{
    const char* srcBase = static_cast<const char*>(src);
    char* dstBase = static_cast<char*>(dst);

    for (const auto& prop : info.properties)
    {
        const char* srcPtr = srcBase + prop.offset;
        char* dstPtr = dstBase + prop.offset;

        switch (prop.typeID)
        {
        case TypeID::String:
        case TypeID::AssetPath:
            *reinterpret_cast<std::string*>(dstPtr) = *reinterpret_cast<const std::string*>(srcPtr);
            break;
        default:
            std::memcpy(dstPtr, srcPtr, prop.size);
            break;
        }
    }
}

std::vector<const PropertyInfo*> reflectDiffProperties(
    const ClassInfo& info, const void* a, const void* b)
{
    std::vector<const PropertyInfo*> result;
    for (const auto& prop : info.properties)
    {
        if (!reflectPropertyEquals(prop, a, b))
            result.push_back(&prop);
    }
    return result;
}
