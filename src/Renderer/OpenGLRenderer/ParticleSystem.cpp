#include "ParticleSystem.h"

#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>
#include "../../Core/ECS/ECS.h"
#include "Logger.h"
#include "../../AssetManager/HPKArchive.h"

// ---------------------------------------------------------------------------
// Random helpers
// ---------------------------------------------------------------------------

float ParticleSystem::randFloat01()
{
    m_rngState = m_rngState * 1664525u + 1013904223u;
    return static_cast<float>(m_rngState & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

float ParticleSystem::randFloat(float lo, float hi)
{
    return lo + randFloat01() * (hi - lo);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

ParticleSystem::~ParticleSystem()
{
    shutdown();
}

static GLuint compileShaderSource(GLenum type, const std::string& source)
{
    GLuint shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char buf[512]{};
        glGetShaderInfoLog(shader, sizeof(buf), nullptr, buf);
        Logger::Instance().log(std::string("ParticleSystem shader compile error: ") + buf, Logger::LogLevel::ERROR);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static std::string readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f)
    {
        // HPK fallback
        auto* hpk = HPKReader::GetMounted();
        if (hpk)
        {
            std::string vpath = hpk->makeVirtualPath(path);
            Logger::Instance().log("HPK particle shader fallback: file=" + path
                + " vpath=" + (vpath.empty() ? "(empty)" : vpath), Logger::LogLevel::INFO);
            if (!vpath.empty())
            {
                auto buf = hpk->readFile(vpath);
                if (buf)
                {
                    Logger::Instance().log("HPK particle shader loaded: " + vpath
                        + " (" + std::to_string(buf->size()) + " bytes)", Logger::LogLevel::INFO);
                    return std::string(buf->data(), buf->size());
                }
            }
        }
        else
        {
            Logger::Instance().log("HPK not mounted when loading particle shader: " + path, Logger::LogLevel::WARNING);
        }
        return {};
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool ParticleSystem::initialize(const std::string& shaderDir)
{
    if (m_initialized) return true;

    // Load shaders
    std::string vtxSrc = readFile(shaderDir + "/particle_vertex.glsl");
    std::string frgSrc = readFile(shaderDir + "/particle_fragment.glsl");
    if (vtxSrc.empty() || frgSrc.empty())
    {
        Logger::Instance().log("ParticleSystem: failed to load particle shaders from " + shaderDir, Logger::LogLevel::ERROR);
        return false;
    }

    GLuint vs = compileShaderSource(GL_VERTEX_SHADER, vtxSrc);
    GLuint fs = compileShaderSource(GL_FRAGMENT_SHADER, frgSrc);
    if (!vs || !fs)
    {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDetachShader(m_program, vs);
    glDetachShader(m_program, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char buf[512]{};
        glGetProgramInfoLog(m_program, sizeof(buf), nullptr, buf);
        Logger::Instance().log(std::string("ParticleSystem program link error: ") + buf, Logger::LogLevel::ERROR);
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    m_locView = glGetUniformLocation(m_program, "uView");
    m_locProjection = glGetUniformLocation(m_program, "uProjection");
    m_locCameraRight = glGetUniformLocation(m_program, "uCameraRight");
    m_locCameraUp = glGetUniformLocation(m_program, "uCameraUp");

    // VAO / VBO for per-particle instance data
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Layout: px,py,pz, r,g,b,a, size  = 8 floats
    const GLsizei stride = static_cast<GLsizei>(sizeof(ParticleVertex));
    // location 0: position (vec3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    // location 1: color (vec4)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    // location 2: size (float)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(7 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_initialized = true;
    return true;
}

void ParticleSystem::shutdown()
{
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    m_emitters.clear();
    m_gpuData.clear();
    m_initialized = false;
}

void ParticleSystem::clear()
{
    m_emitters.clear();
    m_gpuData.clear();
    m_activeCount = 0;
}

// ---------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------

void ParticleSystem::spawnParticle(Particle& p, const ECS::ParticleEmitterComponent& cfg, const glm::vec3& emitterPos)
{
    p.alive = true;
    p.life = cfg.lifetime;
    p.maxLife = cfg.lifetime;
    p.position = emitterPos;

    // Emit in a cone around +Y (up)
    const float halfAngleRad = cfg.coneAngle * 3.14159265f / 180.0f;
    const float theta = randFloat(0.0f, 2.0f * 3.14159265f);
    const float phi = randFloat(0.0f, halfAngleRad);
    const float sp = std::sin(phi);
    glm::vec3 dir(sp * std::cos(theta), std::cos(phi), sp * std::sin(theta));

    const float spd = cfg.speed + randFloat(-cfg.speedVariance, cfg.speedVariance);
    p.velocity = dir * spd;

    p.colorStart = glm::vec4(cfg.colorR, cfg.colorG, cfg.colorB, cfg.colorA);
    p.colorEnd = glm::vec4(cfg.colorEndR, cfg.colorEndG, cfg.colorEndB, cfg.colorEndA);
    p.color = p.colorStart;
    p.sizeStart = cfg.size;
    p.sizeEnd = cfg.sizeEnd;
}

void ParticleSystem::emitParticles(EmitterState& state, const ECS::ParticleEmitterComponent& cfg, const glm::vec3& emitterPos, float dt)
{
    // Accumulate fractional particles
    float toEmit = cfg.emissionRate * dt;

    for (auto& p : state.particles)
    {
        if (!p.alive && toEmit >= 1.0f)
        {
            spawnParticle(p, cfg, emitterPos);
            toEmit -= 1.0f;
        }
    }
}

void ParticleSystem::update(float dt)
{
    if (dt <= 0.0f) return;

    // Gather all entities with ParticleEmitterComponent + TransformComponent
    auto& ecs = ECS::Manager();
    ECS::Schema schema;
    schema.require<ECS::ParticleEmitterComponent>().require<ECS::TransformComponent>();
    auto entities = ecs.getEntitiesMatchingSchema(schema);

    // Build a quick lookup of existing emitter states by entity
    // (keep emitter pools stable across frames)
    for (auto entity : entities)
    {
        auto* cfg = ecs.getComponent<ECS::ParticleEmitterComponent>(entity);
        auto* transform = ecs.getComponent<ECS::TransformComponent>(entity);
        if (!cfg || !transform || !cfg->enabled) continue;

        // Find or create emitter state
        EmitterState* state = nullptr;
        for (auto& es : m_emitters)
        {
            if (es.entity == entity) { state = &es; break; }
        }
        if (!state)
        {
            m_emitters.push_back({});
            state = &m_emitters.back();
            state->entity = entity;
            state->particles.resize(static_cast<size_t>(std::max(1, cfg->maxParticles)));
        }
        // Resize if maxParticles changed
        if (static_cast<int>(state->particles.size()) != cfg->maxParticles)
        {
            state->particles.resize(static_cast<size_t>(std::max(1, cfg->maxParticles)));
        }

        const glm::vec3 emitterPos(transform->position[0], transform->position[1], transform->position[2]);

        // Emit new particles
        emitParticles(*state, *cfg, emitterPos, dt);

        // Update existing particles
        for (auto& p : state->particles)
        {
            if (!p.alive) continue;
            p.life -= dt;
            if (p.life <= 0.0f)
            {
                p.alive = false;
                continue;
            }
            // Gravity
            p.velocity.y += cfg->gravity * dt;
            p.position += p.velocity * dt;

            // Interpolate color and size over lifetime
            const float t = 1.0f - (p.life / p.maxLife); // 0 at birth, 1 at death
            p.color = glm::mix(p.colorStart, p.colorEnd, t);
        }
    }

    // Remove emitter states for entities that no longer exist
    m_emitters.erase(
        std::remove_if(m_emitters.begin(), m_emitters.end(), [&](const EmitterState& es) {
            return !ecs.hasComponent<ECS::ParticleEmitterComponent>(es.entity);
        }),
        m_emitters.end());
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void ParticleSystem::render(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos)
{
    if (!m_initialized || m_emitters.empty()) return;

    // Build GPU data
    m_gpuData.clear();
    for (const auto& state : m_emitters)
    {
        auto* cfg = ECS::Manager().getComponent<ECS::ParticleEmitterComponent>(state.entity);
        for (const auto& p : state.particles)
        {
            if (!p.alive) continue;
            const float t = 1.0f - (p.life / p.maxLife);
            const float sz = cfg ? (cfg->size + t * (cfg->sizeEnd - cfg->size)) : p.sizeStart;
            m_gpuData.push_back({
                p.position.x, p.position.y, p.position.z,
                p.color.r, p.color.g, p.color.b, p.color.a,
                sz
            });
        }
    }
    m_activeCount = static_cast<int>(m_gpuData.size());
    if (m_activeCount == 0) return;

    // Sort back-to-front for correct alpha blending
    const glm::vec3 camPos = cameraPos;
    std::sort(m_gpuData.begin(), m_gpuData.end(), [&](const ParticleVertex& a, const ParticleVertex& b) {
        const float da = (a.px - camPos.x) * (a.px - camPos.x) + (a.py - camPos.y) * (a.py - camPos.y) + (a.pz - camPos.z) * (a.pz - camPos.z);
        const float db = (b.px - camPos.x) * (b.px - camPos.x) + (b.py - camPos.y) * (b.py - camPos.y) + (b.pz - camPos.z) * (b.pz - camPos.z);
        return da > db; // back-to-front
    });

    // Upload
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    const auto byteSize = static_cast<GLsizeiptr>(m_gpuData.size() * sizeof(ParticleVertex));
    glBufferData(GL_ARRAY_BUFFER, byteSize, m_gpuData.data(), GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Extract camera right/up from view matrix for billboarding
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);

    // Draw
    glUseProgram(m_program);
    if (m_locView >= 0) glUniformMatrix4fv(m_locView, 1, GL_FALSE, glm::value_ptr(view));
    if (m_locProjection >= 0) glUniformMatrix4fv(m_locProjection, 1, GL_FALSE, glm::value_ptr(projection));
    if (m_locCameraRight >= 0) glUniform3fv(m_locCameraRight, 1, glm::value_ptr(camRight));
    if (m_locCameraUp >= 0) glUniform3fv(m_locCameraUp, 1, glm::value_ptr(camUp));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE); // Don't write depth for particles

    glBindVertexArray(m_vao);
    glDrawArrays(GL_POINTS, 0, m_activeCount);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glUseProgram(0);
}
