#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <algorithm>

/// Maximum number of bones influencing a single vertex.
static constexpr int kMaxBonesPerVertex = 4;
/// Maximum number of bones in a skeleton (matches shader array).
static constexpr int kMaxBones = 128;

// ── Quaternion helper (minimal, avoids GLM dependency in Core) ──

struct Quat { float x{0}, y{0}, z{0}, w{1}; };

inline Quat quatNormalize(const Quat& q)
{
    float len = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (len < 1e-8f) return {0,0,0,1};
    float inv = 1.0f / len;
    return { q.x*inv, q.y*inv, q.z*inv, q.w*inv };
}

inline Quat quatSlerp(const Quat& a, const Quat& b, float t)
{
    Quat bn = b;
    float dot = a.x*bn.x + a.y*bn.y + a.z*bn.z + a.w*bn.w;
    if (dot < 0.0f) { bn = {-bn.x,-bn.y,-bn.z,-bn.w}; dot = -dot; }
    if (dot > 0.9995f) {
        Quat r = { a.x + t*(bn.x-a.x), a.y + t*(bn.y-a.y), a.z + t*(bn.z-a.z), a.w + t*(bn.w-a.w) };
        return quatNormalize(r);
    }
    float theta0 = std::acos(std::min(dot, 1.0f));
    float theta  = theta0 * t;
    float sinT   = std::sin(theta);
    float sinT0  = std::sin(theta0);
    float s0 = std::cos(theta) - dot * sinT / sinT0;
    float s1 = sinT / sinT0;
    return quatNormalize({ s0*a.x+s1*bn.x, s0*a.y+s1*bn.y, s0*a.z+s1*bn.z, s0*a.w+s1*bn.w });
}

inline Quat quatMul(const Quat& a, const Quat& b)
{
    return {
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
    };
}

// ── 4×4 matrix helpers (row-major float[16]) ──

struct Mat4x4
{
    float m[16]{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

    static Mat4x4 identity()
    {
        Mat4x4 r{};
        return r;
    }

    static Mat4x4 fromTRS(const float pos[3], const Quat& rot, const float scl[3])
    {
        // quaternion to rotation matrix (column-major convention, stored row-major)
        Quat q = quatNormalize(rot);
        float xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z;
        float xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
        float wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;

        Mat4x4 r{};
        r.m[ 0] = (1 - 2*(yy+zz)) * scl[0];
        r.m[ 1] = (    2*(xy-wz)) * scl[0];
        r.m[ 2] = (    2*(xz+wy)) * scl[0];
        r.m[ 3] = 0;
        r.m[ 4] = (    2*(xy+wz)) * scl[1];
        r.m[ 5] = (1 - 2*(xx+zz)) * scl[1];
        r.m[ 6] = (    2*(yz-wx)) * scl[1];
        r.m[ 7] = 0;
        r.m[ 8] = (    2*(xz-wy)) * scl[2];
        r.m[ 9] = (    2*(yz+wx)) * scl[2];
        r.m[10] = (1 - 2*(xx+yy)) * scl[2];
        r.m[11] = 0;
        r.m[12] = pos[0];
        r.m[13] = pos[1];
        r.m[14] = pos[2];
        r.m[15] = 1;
        return r;
    }

    Mat4x4 operator*(const Mat4x4& b) const
    {
        Mat4x4 r{};
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col) {
                float s = 0;
                for (int k = 0; k < 4; ++k)
                    s += m[row*4+k] * b.m[k*4+col];
                r.m[row*4+col] = s;
            }
        return r;
    }

    Mat4x4 inverse() const
    {
        // Gauss-Jordan full 4x4 inverse
        float inv[16];
        float tmp[16];
        for (int i = 0; i < 16; ++i) tmp[i] = m[i];
        for (int i = 0; i < 16; ++i) inv[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        for (int col = 0; col < 4; ++col) {
            int pivot = col;
            for (int row = col+1; row < 4; ++row)
                if (std::abs(tmp[row*4+col]) > std::abs(tmp[pivot*4+col])) pivot = row;
            if (pivot != col) {
                for (int j = 0; j < 4; ++j) { std::swap(tmp[col*4+j], tmp[pivot*4+j]); std::swap(inv[col*4+j], inv[pivot*4+j]); }
            }
            float diag = tmp[col*4+col];
            if (std::abs(diag) < 1e-12f) { return identity(); }
            float invDiag = 1.0f / diag;
            for (int j = 0; j < 4; ++j) { tmp[col*4+j] *= invDiag; inv[col*4+j] *= invDiag; }
            for (int row = 0; row < 4; ++row) {
                if (row == col) continue;
                float factor = tmp[row*4+col];
                for (int j = 0; j < 4; ++j) { tmp[row*4+j] -= factor*tmp[col*4+j]; inv[row*4+j] -= factor*inv[col*4+j]; }
            }
        }
        Mat4x4 r{};
        for (int i = 0; i < 16; ++i) r.m[i] = inv[i];
        return r;
    }
};

// ── Bone ──

struct BoneInfo
{
    std::string name;
    int parentIndex{-1};                ///< -1 = root bone
    Mat4x4 offsetMatrix;                ///< mesh-space → bone-space (inverse bind pose)
};

// ── Animation keyframes ──

struct VectorKey  { float time{0}; float value[3]{0,0,0}; };
struct QuatKey    { float time{0}; Quat value; };

struct BoneChannel
{
    std::string boneName;
    int boneIndex{-1};
    std::vector<VectorKey>  positionKeys;
    std::vector<QuatKey>    rotationKeys;
    std::vector<VectorKey>  scalingKeys;
};

struct AnimationClip
{
    std::string name;
    float duration{0};                  ///< in ticks
    float ticksPerSecond{25.0f};
    std::vector<BoneChannel> channels;
};

// ── Skeleton (embedded in mesh asset) ──

struct Skeleton
{
    std::vector<BoneInfo> bones;
    std::unordered_map<std::string, int> boneNameToIndex;
    std::vector<AnimationClip> animations;

    /// Node hierarchy for animation (may include non-bone nodes)
    struct Node
    {
        std::string name;
        int parentIndex{-1};
        Mat4x4 localTransform;
        int boneIndex{-1};              ///< -1 if not a bone
        std::vector<int> children;
    };
    std::vector<Node> nodes;
    int rootNodeIndex{0};

    bool hasBones() const { return !bones.empty(); }
};

// ── Per-vertex bone influence (used during import, not stored at runtime) ──

struct VertexBoneData
{
    int   ids[kMaxBonesPerVertex]{0,0,0,0};
    float weights[kMaxBonesPerVertex]{0,0,0,0};

    void addBoneWeight(int boneId, float weight)
    {
        // Insert into the slot with the lowest weight
        int minIdx = 0;
        for (int i = 1; i < kMaxBonesPerVertex; ++i)
            if (weights[i] < weights[minIdx]) minIdx = i;
        if (weight > weights[minIdx]) {
            ids[minIdx] = boneId;
            weights[minIdx] = weight;
        }
    }

    void normalize()
    {
        float total = 0;
        for (int i = 0; i < kMaxBonesPerVertex; ++i) total += weights[i];
        if (total > 0.0f) {
            float inv = 1.0f / total;
            for (int i = 0; i < kMaxBonesPerVertex; ++i) weights[i] *= inv;
        }
    }
};

// ── Skeletal Animator (runtime playback with blending & layers) ──

#include "AnimationBlending.h"

/// Per-node decomposed transform (position, rotation, scale) used during
/// pose blending so that quaternion slerp can be applied correctly.
struct NodePose
{
    float pos[3]{ 0, 0, 0 };
    Quat  rot{ 0, 0, 0, 1 };
    float scl[3]{ 1, 1, 1 };
};

class SkeletalAnimator
{
public:
    void setSkeleton(const Skeleton* skeleton)
    {
        m_skeleton = skeleton;
        if (m_layers.empty())
        {
            m_layers.resize(1);
            m_layers[0].name = "FullBody";
        }
    }
    const Skeleton* getSkeleton() const { return m_skeleton; }

    // ── Legacy single-clip API (operates on layer 0) ────────────────

    void playAnimation(int clipIndex, bool loop = true)
    {
        if (!m_skeleton || clipIndex < 0 || clipIndex >= static_cast<int>(m_skeleton->animations.size()))
            return;
        auto& layer = ensureLayer(0);
        layer.current.clipIndex = clipIndex;
        layer.current.time = 0.0f;
        layer.current.speed = m_speed;
        layer.current.weight = 1.0f;
        layer.current.loop = loop;
        layer.current.playing = true;
        layer.previous = BlendEntry{};
        layer.crossfade = CrossfadeState{};

        m_clipIndex = clipIndex;
        m_currentTime = 0.0f;
        m_playing = true;
        m_loop = loop;
    }

    void stop()
    {
        m_playing = false;
        m_currentTime = 0;
        if (!m_layers.empty())
        {
            m_layers[0].current.playing = false;
            m_layers[0].current.time = 0;
            m_layers[0].previous = BlendEntry{};
            m_layers[0].crossfade = CrossfadeState{};
        }
    }

    bool isPlaying() const { return m_playing; }
    int  getCurrentClipIndex() const { return m_clipIndex; }
    float getCurrentTime() const { return m_currentTime; }
    void setSpeed(float speed) { m_speed = speed; }

    // ── Crossfade API ───────────────────────────────────────────────

    /// Smoothly transition from the current clip to `toClip` over `duration`
    /// seconds on the specified layer.
    void crossfade(int toClip, float duration, bool loop = true, int layerIndex = 0)
    {
        if (!m_skeleton || toClip < 0 || toClip >= static_cast<int>(m_skeleton->animations.size()))
            return;
        auto& layer = ensureLayer(layerIndex);
        layer.previous = layer.current;
        layer.current.clipIndex = toClip;
        layer.current.time = 0.0f;
        layer.current.speed = m_speed;
        layer.current.weight = 0.0f;
        layer.current.loop = loop;
        layer.current.playing = true;
        layer.crossfade.active = true;
        layer.crossfade.duration = (std::max)(duration, 0.001f);
        layer.crossfade.elapsed = 0.0f;
    }

    bool isCrossfading(int layerIndex = 0) const
    {
        if (layerIndex < 0 || layerIndex >= static_cast<int>(m_layers.size())) return false;
        return m_layers[layerIndex].crossfade.active;
    }

    // ── Layer API ───────────────────────────────────────────────────

    int  getLayerCount() const { return static_cast<int>(m_layers.size()); }
    void setLayerCount(int count)
    {
        if (count < 1) count = 1;
        if (count > kMaxAnimLayers) count = kMaxAnimLayers;
        m_layers.resize(static_cast<size_t>(count));
    }

    AnimationLayer&       getLayer(int index)       { return m_layers[static_cast<size_t>(index)]; }
    const AnimationLayer& getLayer(int index) const { return m_layers[static_cast<size_t>(index)]; }

    void setLayerWeight(int index, float weight)
    {
        if (index >= 0 && index < static_cast<int>(m_layers.size()))
            m_layers[index].layerWeight = std::clamp(weight, 0.0f, 1.0f);
    }

    void setLayerBoneMask(int index, const BoneMask& mask)
    {
        if (index >= 0 && index < static_cast<int>(m_layers.size()))
            m_layers[index].boneMask = mask;
    }

    void setLayerBlendMode(int index, AnimBlendMode mode)
    {
        if (index >= 0 && index < static_cast<int>(m_layers.size()))
            m_layers[index].blendMode = mode;
    }

    /// Play a clip on a specific layer (with optional crossfade).
    void playOnLayer(int layerIndex, int clipIndex, bool loop = true, float crossfadeDuration = 0.0f)
    {
        if (!m_skeleton || clipIndex < 0 || clipIndex >= static_cast<int>(m_skeleton->animations.size()))
            return;
        if (crossfadeDuration > 0.0f)
        {
            crossfade(clipIndex, crossfadeDuration, loop, layerIndex);
        }
        else
        {
            auto& layer = ensureLayer(layerIndex);
            layer.current.clipIndex = clipIndex;
            layer.current.time = 0.0f;
            layer.current.speed = m_speed;
            layer.current.weight = 1.0f;
            layer.current.loop = loop;
            layer.current.playing = true;
            layer.previous = BlendEntry{};
            layer.crossfade = CrossfadeState{};
        }
        if (layerIndex == 0)
        {
            m_clipIndex = clipIndex;
            m_currentTime = 0.0f;
            m_playing = true;
            m_loop = loop;
        }
    }

    void stopLayer(int layerIndex)
    {
        if (layerIndex < 0 || layerIndex >= static_cast<int>(m_layers.size())) return;
        m_layers[layerIndex].current.playing = false;
        m_layers[layerIndex].current.time = 0;
        m_layers[layerIndex].previous = BlendEntry{};
        m_layers[layerIndex].crossfade = CrossfadeState{};
        if (layerIndex == 0)
        {
            m_playing = false;
            m_currentTime = 0;
        }
    }

    // ── Tick ─────────────────────────────────────────────────────────

    /// Advance time and compute bone matrices (call once per frame).
    void tick(float deltaTime)
    {
        if (!m_skeleton) return;

        bool anyPlaying = false;
        for (auto& layer : m_layers)
        {
            advanceBlendEntry(layer.current, deltaTime);
            advanceBlendEntry(layer.previous, deltaTime);

            if (layer.crossfade.active)
            {
                layer.crossfade.elapsed += deltaTime;
                float t = std::clamp(layer.crossfade.elapsed / layer.crossfade.duration, 0.0f, 1.0f);
                // smooth-step easing
                t = t * t * (3.0f - 2.0f * t);
                layer.current.weight = t;
                layer.previous.weight = 1.0f - t;
                if (layer.crossfade.elapsed >= layer.crossfade.duration)
                {
                    layer.crossfade.active = false;
                    layer.current.weight = 1.0f;
                    layer.previous = BlendEntry{};
                }
            }

            if (layer.current.playing) anyPlaying = true;
        }

        if (!m_layers.empty())
        {
            m_clipIndex = m_layers[0].current.clipIndex;
            m_currentTime = m_layers[0].current.time;
            m_playing = m_layers[0].current.playing;
        }

        computeBlendedBoneMatrices();
    }

    /// Final bone matrices ready for upload to GPU (count = bones.size()).
    const std::vector<Mat4x4>& getBoneMatrices() const { return m_boneMatrices; }

private:
    AnimationLayer& ensureLayer(int index)
    {
        if (index >= static_cast<int>(m_layers.size()))
            m_layers.resize(static_cast<size_t>(index) + 1);
        return m_layers[index];
    }

    void advanceBlendEntry(BlendEntry& entry, float dt)
    {
        if (!entry.playing || !m_skeleton || entry.clipIndex < 0) return;
        const auto& clip = m_skeleton->animations[entry.clipIndex];
        float tps = clip.ticksPerSecond > 0.0f ? clip.ticksPerSecond : 25.0f;
        entry.time += dt * tps * entry.speed;
        if (entry.time >= clip.duration)
        {
            if (entry.loop)
                entry.time = std::fmod(entry.time, clip.duration);
            else
            {
                entry.time = clip.duration;
                entry.playing = false;
            }
        }
    }

    /// Evaluate a single BlendEntry to produce per-node local poses.
    void evaluateEntryPoses(const BlendEntry& entry,
                            std::vector<NodePose>& outPoses,
                            std::vector<bool>& outAnimated) const
    {
        if (!m_skeleton || entry.clipIndex < 0) return;
        const auto& clip = m_skeleton->animations[entry.clipIndex];
        const auto& nodes = m_skeleton->nodes;
        const size_t nodeCount = nodes.size();
        outPoses.resize(nodeCount);
        outAnimated.resize(nodeCount, false);

        for (size_t i = 0; i < nodeCount; ++i)
        {
            outPoses[i] = decomposeNodeTransform(nodes[i].localTransform);
            outAnimated[i] = false;
        }

        for (const auto& ch : clip.channels)
        {
            int nodeIdx = findNodeForChannel(ch);
            if (nodeIdx < 0) continue;
            interpolatePosition(ch, entry.time, outPoses[nodeIdx].pos);
            interpolateRotation(ch, entry.time, outPoses[nodeIdx].rot);
            interpolateScaling(ch, entry.time, outPoses[nodeIdx].scl);
            outAnimated[nodeIdx] = true;
        }
    }

    /// Compute final bone matrices by blending all layers.
    void computeBlendedBoneMatrices()
    {
        if (!m_skeleton) return;
        const auto& nodes = m_skeleton->nodes;
        const auto& bones = m_skeleton->bones;
        const size_t nodeCount = nodes.size();
        const size_t boneCount = bones.size();

        // Start with bind-pose local transforms decomposed
        std::vector<NodePose> basePoses(nodeCount);
        for (size_t i = 0; i < nodeCount; ++i)
            basePoses[i] = decomposeNodeTransform(nodes[i].localTransform);

        // Per-node accumulated pose (starts as bind pose)
        std::vector<NodePose> finalPoses = basePoses;

        // Track which nodes have been written to by at least one layer
        std::vector<bool> nodeWritten(nodeCount, false);

        for (size_t li = 0; li < m_layers.size(); ++li)
        {
            const auto& layer = m_layers[li];
            if (layer.layerWeight <= 0.0f) continue;

            // Evaluate the layer's current and previous entries
            std::vector<NodePose> curPoses, prevPoses;
            std::vector<bool> curAnimated, prevAnimated;

            bool hasCurrent = layer.current.playing && layer.current.clipIndex >= 0;
            bool hasPrevious = layer.crossfade.active && layer.previous.playing && layer.previous.clipIndex >= 0;

            if (hasCurrent)
                evaluateEntryPoses(layer.current, curPoses, curAnimated);
            if (hasPrevious)
                evaluateEntryPoses(layer.previous, prevPoses, prevAnimated);

            if (!hasCurrent && !hasPrevious) continue;

            // Blend current and previous within this layer
            std::vector<NodePose> layerPoses(nodeCount);
            std::vector<bool> layerAnimated(nodeCount, false);

            for (size_t ni = 0; ni < nodeCount; ++ni)
            {
                bool cA = hasCurrent && ni < curAnimated.size() && curAnimated[ni];
                bool pA = hasPrevious && ni < prevAnimated.size() && prevAnimated[ni];

                if (cA && pA)
                {
                    float cw = layer.current.weight;
                    float pw = layer.previous.weight;
                    float total = cw + pw;
                    if (total > 0.0f)
                    {
                        float t = cw / total;
                        blendPose(prevPoses[ni], curPoses[ni], t, layerPoses[ni]);
                    }
                    else
                    {
                        layerPoses[ni] = basePoses[ni];
                    }
                    layerAnimated[ni] = true;
                }
                else if (cA)
                {
                    layerPoses[ni] = curPoses[ni];
                    layerAnimated[ni] = true;
                }
                else if (pA)
                {
                    layerPoses[ni] = prevPoses[ni];
                    layerAnimated[ni] = true;
                }
                else
                {
                    layerPoses[ni] = basePoses[ni];
                }
            }

            // Apply this layer to finalPoses with bone mask and layer weight
            float lw = layer.layerWeight;
            for (size_t ni = 0; ni < nodeCount; ++ni)
            {
                if (!layerAnimated[ni]) continue;

                // Check bone mask (empty = all bones)
                int boneIdx = (ni < nodes.size()) ? nodes[ni].boneIndex : -1;
                if (!layer.boneMask.empty() && boneIdx >= 0 && !layer.boneMask.test(boneIdx))
                    continue;

                if (layer.blendMode == AnimBlendMode::Override)
                {
                    if (nodeWritten[ni])
                        blendPose(finalPoses[ni], layerPoses[ni], lw, finalPoses[ni]);
                    else
                    {
                        blendPose(basePoses[ni], layerPoses[ni], lw, finalPoses[ni]);
                        nodeWritten[ni] = true;
                    }
                }
                else // Additive
                {
                    // Additive: delta = layerPose - bindPose, apply weighted
                    NodePose& fp = finalPoses[ni];
                    const NodePose& lp = layerPoses[ni];
                    const NodePose& bp = basePoses[ni];
                    fp.pos[0] += (lp.pos[0] - bp.pos[0]) * lw;
                    fp.pos[1] += (lp.pos[1] - bp.pos[1]) * lw;
                    fp.pos[2] += (lp.pos[2] - bp.pos[2]) * lw;
                    fp.scl[0] += (lp.scl[0] - bp.scl[0]) * lw;
                    fp.scl[1] += (lp.scl[1] - bp.scl[1]) * lw;
                    fp.scl[2] += (lp.scl[2] - bp.scl[2]) * lw;
                    // Additive rotation: multiply delta quaternion scaled by weight
                    // delta = lp.rot * inverse(bp.rot)
                    Quat bpInv = { -bp.rot.x, -bp.rot.y, -bp.rot.z, bp.rot.w };
                    Quat delta = quatMul(lp.rot, bpInv);
                    Quat scaled = quatSlerp(Quat{0,0,0,1}, delta, lw);
                    fp.rot = quatNormalize(quatMul(scaled, fp.rot));
                    nodeWritten[ni] = true;
                }
            }
        }

        // Recompose local transforms from poses
        std::vector<Mat4x4> localTransforms(nodeCount);
        for (size_t i = 0; i < nodeCount; ++i)
            localTransforms[i] = Mat4x4::fromTRS(finalPoses[i].pos, finalPoses[i].rot, finalPoses[i].scl);

        // Compute global transforms by traversal
        std::vector<Mat4x4> globalTransforms(nodeCount);
        for (size_t i = 0; i < nodeCount; ++i)
        {
            if (nodes[i].parentIndex < 0)
                globalTransforms[i] = localTransforms[i];
            else
                globalTransforms[i] = globalTransforms[nodes[i].parentIndex] * localTransforms[i];
        }

        // Compute final bone matrices: globalTransform * offsetMatrix
        m_boneMatrices.resize(boneCount);
        for (size_t b = 0; b < boneCount; ++b)
        {
            int nodeIdx = findNodeForBone(static_cast<int>(b));
            if (nodeIdx >= 0)
                m_boneMatrices[b] = globalTransforms[nodeIdx] * bones[b].offsetMatrix;
            else
                m_boneMatrices[b] = Mat4x4::identity();
        }
    }

    // ── Helpers ──────────────────────────────────────────────────────

    int findNodeForChannel(const BoneChannel& ch) const
    {
        const auto& nodes = m_skeleton->nodes;
        for (int ni = 0; ni < static_cast<int>(nodes.size()); ++ni)
            if (nodes[ni].boneIndex == ch.boneIndex || nodes[ni].name == ch.boneName)
                return ni;
        return -1;
    }

    int findNodeForBone(int boneIndex) const
    {
        const auto& nodes = m_skeleton->nodes;
        for (int ni = 0; ni < static_cast<int>(nodes.size()); ++ni)
            if (nodes[ni].boneIndex == boneIndex)
                return ni;
        return -1;
    }

    static NodePose decomposeNodeTransform(const Mat4x4& m)
    {
        NodePose p;
        p.pos[0] = m.m[12]; p.pos[1] = m.m[13]; p.pos[2] = m.m[14];
        // Extract scale from column lengths (row-major: rows are axes)
        float sx = std::sqrt(m.m[0]*m.m[0] + m.m[1]*m.m[1] + m.m[2]*m.m[2]);
        float sy = std::sqrt(m.m[4]*m.m[4] + m.m[5]*m.m[5] + m.m[6]*m.m[6]);
        float sz = std::sqrt(m.m[8]*m.m[8] + m.m[9]*m.m[9] + m.m[10]*m.m[10]);
        p.scl[0] = sx > 1e-6f ? sx : 1.0f;
        p.scl[1] = sy > 1e-6f ? sy : 1.0f;
        p.scl[2] = sz > 1e-6f ? sz : 1.0f;
        // Extract rotation matrix (normalize rows)
        float r00 = m.m[0]/p.scl[0], r01 = m.m[1]/p.scl[0], r02 = m.m[2]/p.scl[0];
        float r10 = m.m[4]/p.scl[1], r11 = m.m[5]/p.scl[1], r12 = m.m[6]/p.scl[1];
        float r20 = m.m[8]/p.scl[2], r21 = m.m[9]/p.scl[2], r22 = m.m[10]/p.scl[2];
        // Rotation matrix → quaternion (Shepperd's method)
        float trace = r00 + r11 + r22;
        if (trace > 0.0f)
        {
            float s = std::sqrt(trace + 1.0f) * 2.0f;
            p.rot.w = 0.25f * s;
            p.rot.x = (r12 - r21) / s;
            p.rot.y = (r20 - r02) / s;
            p.rot.z = (r01 - r10) / s;
        }
        else if (r00 > r11 && r00 > r22)
        {
            float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
            p.rot.w = (r12 - r21) / s;
            p.rot.x = 0.25f * s;
            p.rot.y = (r01 + r10) / s;
            p.rot.z = (r02 + r20) / s;
        }
        else if (r11 > r22)
        {
            float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
            p.rot.w = (r20 - r02) / s;
            p.rot.x = (r01 + r10) / s;
            p.rot.y = 0.25f * s;
            p.rot.z = (r12 + r21) / s;
        }
        else
        {
            float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
            p.rot.w = (r01 - r10) / s;
            p.rot.x = (r02 + r20) / s;
            p.rot.y = (r12 + r21) / s;
            p.rot.z = 0.25f * s;
        }
        p.rot = quatNormalize(p.rot);
        return p;
    }

    static void blendPose(const NodePose& a, const NodePose& b, float t, NodePose& out)
    {
        out.pos[0] = a.pos[0] + t * (b.pos[0] - a.pos[0]);
        out.pos[1] = a.pos[1] + t * (b.pos[1] - a.pos[1]);
        out.pos[2] = a.pos[2] + t * (b.pos[2] - a.pos[2]);
        out.scl[0] = a.scl[0] + t * (b.scl[0] - a.scl[0]);
        out.scl[1] = a.scl[1] + t * (b.scl[1] - a.scl[1]);
        out.scl[2] = a.scl[2] + t * (b.scl[2] - a.scl[2]);
        out.rot = quatSlerp(a.rot, b.rot, t);
    }

    static void interpolatePosition(const BoneChannel& ch, float time, float out[3])
    {
        if (ch.positionKeys.empty()) return;
        if (ch.positionKeys.size() == 1) {
            out[0] = ch.positionKeys[0].value[0]; out[1] = ch.positionKeys[0].value[1]; out[2] = ch.positionKeys[0].value[2];
            return;
        }
        int idx = 0;
        for (int i = 0; i < static_cast<int>(ch.positionKeys.size()) - 1; ++i)
            if (time < ch.positionKeys[i+1].time) { idx = i; break; }
            else idx = i;
        const auto& k0 = ch.positionKeys[idx];
        const auto& k1 = ch.positionKeys[std::min(idx+1, static_cast<int>(ch.positionKeys.size())-1)];
        float dt = k1.time - k0.time;
        float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        out[0] = k0.value[0] + t*(k1.value[0]-k0.value[0]);
        out[1] = k0.value[1] + t*(k1.value[1]-k0.value[1]);
        out[2] = k0.value[2] + t*(k1.value[2]-k0.value[2]);
    }

    static void interpolateRotation(const BoneChannel& ch, float time, Quat& out)
    {
        if (ch.rotationKeys.empty()) return;
        if (ch.rotationKeys.size() == 1) { out = ch.rotationKeys[0].value; return; }
        int idx = 0;
        for (int i = 0; i < static_cast<int>(ch.rotationKeys.size()) - 1; ++i)
            if (time < ch.rotationKeys[i+1].time) { idx = i; break; }
            else idx = i;
        const auto& k0 = ch.rotationKeys[idx];
        const auto& k1 = ch.rotationKeys[std::min(idx+1, static_cast<int>(ch.rotationKeys.size())-1)];
        float dt = k1.time - k0.time;
        float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        out = quatSlerp(k0.value, k1.value, t);
    }

    static void interpolateScaling(const BoneChannel& ch, float time, float out[3])
    {
        if (ch.scalingKeys.empty()) { out[0]=out[1]=out[2]=1; return; }
        if (ch.scalingKeys.size() == 1) {
            out[0] = ch.scalingKeys[0].value[0]; out[1] = ch.scalingKeys[0].value[1]; out[2] = ch.scalingKeys[0].value[2];
            return;
        }
        int idx = 0;
        for (int i = 0; i < static_cast<int>(ch.scalingKeys.size()) - 1; ++i)
            if (time < ch.scalingKeys[i+1].time) { idx = i; break; }
            else idx = i;
        const auto& k0 = ch.scalingKeys[idx];
        const auto& k1 = ch.scalingKeys[std::min(idx+1, static_cast<int>(ch.scalingKeys.size())-1)];
        float dt = k1.time - k0.time;
        float t = (dt > 0.0f) ? (time - k0.time) / dt : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        out[0] = k0.value[0] + t*(k1.value[0]-k0.value[0]);
        out[1] = k0.value[1] + t*(k1.value[1]-k0.value[1]);
        out[2] = k0.value[2] + t*(k1.value[2]-k0.value[2]);
    }

    const Skeleton* m_skeleton{ nullptr };
    // Legacy compatibility fields (mirror layer 0)
    int   m_clipIndex{ -1 };
    float m_currentTime{ 0 };
    float m_speed{ 1.0f };
    bool  m_playing{ false };
    bool  m_loop{ true };

    std::vector<AnimationLayer> m_layers;
    std::vector<Mat4x4>         m_boneMatrices;
};
