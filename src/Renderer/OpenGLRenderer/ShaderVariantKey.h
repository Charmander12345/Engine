#pragma once

#include <cstdint>
#include <string>

/// Bitmask identifying which features a shader variant supports.
/// Used to select the correct pre-compiled shader permutation and
/// eliminate runtime branching on the GPU.
enum ShaderVariantFlag : uint32_t
{
    SVF_NONE                   = 0,
    SVF_HAS_DIFFUSE_MAP        = 1 << 0,
    SVF_HAS_SPECULAR_MAP       = 1 << 1,
    SVF_HAS_NORMAL_MAP         = 1 << 2,
    SVF_HAS_EMISSIVE_MAP       = 1 << 3,
    SVF_HAS_METALLIC_ROUGHNESS = 1 << 4,
    SVF_PBR_ENABLED            = 1 << 5,
    SVF_FOG_ENABLED            = 1 << 6,
    SVF_OIT_ENABLED            = 1 << 7,
};

using ShaderVariantKey = uint32_t;

/// Build the #define block that corresponds to a variant key.
/// The returned string is meant to be inserted right after the
/// ``#version`` line of a GLSL source file.
inline std::string buildVariantDefines(ShaderVariantKey key)
{
    std::string defs;
    if (key & SVF_HAS_DIFFUSE_MAP)        defs += "#define HAS_DIFFUSE_MAP\n";
    if (key & SVF_HAS_SPECULAR_MAP)       defs += "#define HAS_SPECULAR_MAP\n";
    if (key & SVF_HAS_NORMAL_MAP)         defs += "#define HAS_NORMAL_MAP\n";
    if (key & SVF_HAS_EMISSIVE_MAP)       defs += "#define HAS_EMISSIVE_MAP\n";
    if (key & SVF_HAS_METALLIC_ROUGHNESS) defs += "#define HAS_METALLIC_ROUGHNESS_MAP\n";
    if (key & SVF_PBR_ENABLED)            defs += "#define PBR_ENABLED\n";
    if (key & SVF_FOG_ENABLED)            defs += "#define FOG_ENABLED\n";
    if (key & SVF_OIT_ENABLED)            defs += "#define OIT_ENABLED\n";
    return defs;
}
