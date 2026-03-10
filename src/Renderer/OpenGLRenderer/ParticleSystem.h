#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include "glad/include/gl.h"
#include "../../Core/ECS/Components.h"

/// CPU-simulated billboard particle system.
/// Each entity with a ParticleEmitterComponent gets its own pool of particles.
/// The system updates all pools each frame and uploads a single interleaved
/// VBO for the renderer to draw as instanced point-sprite quads.
class ParticleSystem
{
public:
    struct Particle
    {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec4 color{1.0f};
        glm::vec4 colorStart{1.0f};
        glm::vec4 colorEnd{0.0f};
        float sizeStart{0.2f};
        float sizeEnd{0.0f};
        float life{0.0f};      ///< Remaining life in seconds
        float maxLife{1.0f};
        bool alive{false};
    };

    struct EmitterState
    {
        unsigned int entity{0};
        std::vector<Particle> particles;
    };

    /// Per-particle GPU data (uploaded each frame).
    struct ParticleVertex
    {
        float px, py, pz;      ///< World position
        float r, g, b, a;      ///< RGBA color
        float size;             ///< Billboard half-size
    };

    ParticleSystem() = default;
    ~ParticleSystem();

    /// Initialise GPU resources (VAO, VBO, shader program).
    bool initialize(const std::string& shaderDir);

    /// Destroy GPU resources.
    void shutdown();

    /// Simulate all emitters for one frame.
    void update(float dt);

    /// Upload particle data and draw all particles.
    /// @param view       Current view matrix.
    /// @param projection Current projection matrix.
    /// @param cameraPos  Camera world position (for billboard orientation).
    void render(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos);

    /// Clear all emitter states (e.g. on level unload).
    void clear();

    /// Number of active (alive) particles across all emitters.
    int getActiveParticleCount() const { return m_activeCount; }

private:
    void emitParticles(EmitterState& state, const ECS::ParticleEmitterComponent& cfg, const glm::vec3& emitterPos, float dt);
    void spawnParticle(Particle& p, const ECS::ParticleEmitterComponent& cfg, const glm::vec3& emitterPos);

    std::vector<EmitterState> m_emitters;
    std::vector<ParticleVertex> m_gpuData;
    int m_activeCount{0};

    // GPU resources
    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLuint m_program{0};
    GLint m_locView{-1};
    GLint m_locProjection{-1};
    GLint m_locCameraRight{-1};
    GLint m_locCameraUp{-1};
    bool m_initialized{false};

    // Simple LCG for fast pseudo-random in hot loop
    uint32_t m_rngState{12345};
    float randFloat01();
    float randFloat(float lo, float hi);
};
