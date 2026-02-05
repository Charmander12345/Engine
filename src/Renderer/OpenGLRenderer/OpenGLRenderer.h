#pragma once

#include "../Renderer.h"
#include "../Camera.h"
#include "../RenderResourceManager.h"
#include "glad/include/gl.h"
#include <memory>
#include <vector>
#include <glm/glm.hpp>

#include "../../Core/ECS/Components.h"

class OpenGLObject2D;
class OpenGLObject3D;

class OpenGLRenderer : public Renderer
{
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    bool initialize() override;
    void shutdown() override;
    void clear() override;
    void render() override;
    void present() override;
    const std::string& name() const override;

    SDL_Window* window() const override;

    void moveCamera(float forward, float right, float up) override;
    void rotateCamera(float yawDeltaDegrees, float pitchDeltaDegrees) override;

private:
    struct RenderEntry
    {
        ECS::TransformComponent transform{};
        std::shared_ptr<OpenGLObject2D> object2D;
        std::shared_ptr<OpenGLObject3D> object3D;
    };

    bool isRenderEntryRelevant(const RenderEntry& entry) const;

    bool m_initialized;
    std::string m_name;
    SDL_Window* m_window;
    SDL_GLContext m_glContext;

    std::unique_ptr<Camera> m_camera;
    glm::mat4 m_projectionMatrix;
    std::vector<RenderEntry> m_renderEntries;
    RenderResourceManager m_resourceManager;
    EngineLevel* m_cachedLevel{ nullptr };
};