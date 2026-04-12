#pragma once

#include <cstdint>
#include <bitset>
#include <string>

/// Maximum animation layers (e.g. full body, upper body, facial).
static constexpr int kMaxAnimLayers = 4;

// ── Blend entry (one active clip contributing to the final pose) ────

/// Represents a single animation clip that is contributing to the final
/// blended pose.  Multiple BlendEntries can be active simultaneously
/// (e.g. during a crossfade or additive layer).
struct BlendEntry
{
    int   clipIndex{ -1 };   ///< Index into Skeleton::animations (-1 = none)
    float time{ 0.0f };      ///< Current playback time (in ticks)
    float speed{ 1.0f };     ///< Playback speed multiplier
    float weight{ 0.0f };    ///< Blend weight [0..1]
    bool  loop{ true };      ///< Whether the clip loops
    bool  playing{ false };  ///< Whether the clip is actively advancing
};

// ── Crossfade state ─────────────────────────────────────────────────

/// Tracks an in-progress crossfade between two BlendEntries within the
/// same layer.
struct CrossfadeState
{
    bool  active{ false };
    float duration{ 0.0f };  ///< Total crossfade time (seconds)
    float elapsed{ 0.0f };   ///< Time elapsed since crossfade start (seconds)
};

// ── Bone mask (which bones a layer controls) ────────────────────────

/// A bitset over kMaxBones (128) – bit N set means the layer drives bone N.
/// An empty mask (all zero) means "all bones" (full body).
struct BoneMask
{
    static constexpr int kCapacity = 128;
    std::bitset<kCapacity> bits;

    bool empty() const { return bits.none(); }
    void setAll()      { bits.set(); }
    void clearAll()    { bits.reset(); }
    bool test(int boneIndex) const
    {
        if (boneIndex < 0 || boneIndex >= kCapacity) return false;
        return bits.test(static_cast<size_t>(boneIndex));
    }
    void set(int boneIndex, bool value = true)
    {
        if (boneIndex >= 0 && boneIndex < kCapacity)
            bits.set(static_cast<size_t>(boneIndex), value);
    }
};

// ── Blend mode ──────────────────────────────────────────────────────

enum class AnimBlendMode : uint8_t
{
    Override,   ///< Layer overrides lower layers (weighted)
    Additive    ///< Layer adds deltas on top of lower layers
};

// ── Animation layer ─────────────────────────────────────────────────

/// One animation layer.  Layer 0 is typically "full body", additional
/// layers allow partial-body overrides (e.g. upper body aiming while
/// lower body runs).
struct AnimationLayer
{
    BlendEntry    current;                       ///< The clip currently playing
    BlendEntry    previous;                      ///< The clip being faded out (during crossfade)
    CrossfadeState crossfade;                    ///< Active crossfade between previous→current
    BoneMask      boneMask;                      ///< Which bones this layer affects (empty = all)
    AnimBlendMode blendMode{ AnimBlendMode::Override }; ///< How this layer combines with lower layers
    float         layerWeight{ 1.0f };           ///< Overall layer weight [0..1]
    std::string   name;                          ///< Optional name ("FullBody", "UpperBody", …)
};
