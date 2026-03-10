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

// ── Skeletal Animator (runtime playback) ──

class SkeletalAnimator
{
public:
    void setSkeleton(const Skeleton* skeleton) { m_skeleton = skeleton; }
    const Skeleton* getSkeleton() const { return m_skeleton; }

    void playAnimation(int clipIndex, bool loop = true)
    {
        if (!m_skeleton || clipIndex < 0 || clipIndex >= static_cast<int>(m_skeleton->animations.size()))
            return;
        m_clipIndex = clipIndex;
        m_currentTime = 0.0f;
        m_playing = true;
        m_loop = loop;
    }

    void stop() { m_playing = false; m_currentTime = 0; }
    bool isPlaying() const { return m_playing; }
    int  getCurrentClipIndex() const { return m_clipIndex; }
    float getCurrentTime() const { return m_currentTime; }
    void setSpeed(float speed) { m_speed = speed; }

    /// Advance time and compute bone matrices (call once per frame).
    void tick(float deltaTime)
    {
        if (!m_playing || !m_skeleton || m_clipIndex < 0) return;
        const auto& clip = m_skeleton->animations[m_clipIndex];
        float tps = clip.ticksPerSecond > 0.0f ? clip.ticksPerSecond : 25.0f;
        m_currentTime += deltaTime * tps * m_speed;
        if (m_currentTime >= clip.duration) {
            if (m_loop) {
                m_currentTime = std::fmod(m_currentTime, clip.duration);
            } else {
                m_currentTime = clip.duration;
                m_playing = false;
            }
        }
        computeBoneMatrices(clip);
    }

    /// Final bone matrices ready for upload to GPU (column-major, count = bones.size()).
    const std::vector<Mat4x4>& getBoneMatrices() const { return m_boneMatrices; }

private:
    void computeBoneMatrices(const AnimationClip& clip)
    {
        if (!m_skeleton) return;
        const auto& nodes = m_skeleton->nodes;
        const auto& bones = m_skeleton->bones;
        const size_t nodeCount = nodes.size();
        const size_t boneCount = bones.size();

        // Build per-node local transforms (animated where a channel exists)
        std::vector<Mat4x4> localTransforms(nodeCount);
        for (size_t i = 0; i < nodeCount; ++i)
            localTransforms[i] = nodes[i].localTransform;

        for (const auto& ch : clip.channels) {
            int boneIdx = ch.boneIndex;
            // Find the node that corresponds to this bone
            int nodeIdx = -1;
            for (int ni = 0; ni < static_cast<int>(nodeCount); ++ni) {
                if (nodes[ni].boneIndex == boneIdx || nodes[ni].name == ch.boneName) {
                    nodeIdx = ni;
                    break;
                }
            }
            if (nodeIdx < 0) continue;

            float pos[3]{0,0,0};
            Quat  rot{0,0,0,1};
            float scl[3]{1,1,1};

            interpolatePosition(ch, m_currentTime, pos);
            interpolateRotation(ch, m_currentTime, rot);
            interpolateScaling(ch, m_currentTime, scl);

            localTransforms[nodeIdx] = Mat4x4::fromTRS(pos, rot, scl);
        }

        // Compute global transforms by traversal
        std::vector<Mat4x4> globalTransforms(nodeCount);
        for (size_t i = 0; i < nodeCount; ++i) {
            if (nodes[i].parentIndex < 0)
                globalTransforms[i] = localTransforms[i];
            else
                globalTransforms[i] = globalTransforms[nodes[i].parentIndex] * localTransforms[i];
        }

        // Compute final bone matrices: globalTransform * offsetMatrix
        m_boneMatrices.resize(boneCount);
        for (size_t b = 0; b < boneCount; ++b) {
            // Find the node for this bone
            int nodeIdx = -1;
            for (int ni = 0; ni < static_cast<int>(nodeCount); ++ni) {
                if (nodes[ni].boneIndex == static_cast<int>(b)) {
                    nodeIdx = ni;
                    break;
                }
            }
            if (nodeIdx >= 0)
                m_boneMatrices[b] = globalTransforms[nodeIdx] * bones[b].offsetMatrix;
            else
                m_boneMatrices[b] = Mat4x4::identity();
        }
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

    const Skeleton* m_skeleton{nullptr};
    int   m_clipIndex{-1};
    float m_currentTime{0};
    float m_speed{1.0f};
    bool  m_playing{false};
    bool  m_loop{true};
    std::vector<Mat4x4> m_boneMatrices;
};
