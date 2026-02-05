#pragma once

#include "../Renderer.h"
#include "../Camera.h"
#include "../RenderResourceManager.h"
#include "../UIManager.h"

#include "../../Core/MathTypes.h"
#include "glad/include/gl.h"
#include <memory>
#include <unordered_map>
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

    void queueText(const std::string& text, const Vec2& screenPos, float scale, const Vec4& color);
    Vec2 getViewportSize() const;
    UIManager& getUIManager();
    const UIManager& getUIManager() const;
    std::shared_ptr<Widget> createWidgetFromAsset(const std::shared_ptr<AssetData>& asset);

private:
    void renderWorld();
    void renderUI();
    bool ensureUIQuadRenderer();
    GLuint getUIQuadProgram(const std::string& vertexShaderPath, const std::string& fragmentShaderPath);
    void drawUIPanel(float x0, float y0, float x1, float y1, const Vec4& color, const glm::mat4& projection, GLuint program);
    void drawUIOutline(float x0, float y0, float x1, float y1, const Vec4& color, const glm::mat4& projection, GLuint program);

    struct RenderEntry
    {
        ECS::TransformComponent transform{};
        std::shared_ptr<OpenGLObject2D> object2D;
        std::shared_ptr<OpenGLObject3D> object3D;
    };

    bool isRenderEntryRelevant(const RenderEntry& entry) const;

    struct TextCommand
    {
        std::string text;
        Vec2 screenPos;
        float scale{1.0f};
        Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    bool m_initialized;
    std::string m_name;
    SDL_Window* m_window;
    SDL_GLContext m_glContext;

    std::unique_ptr<Camera> m_camera;
    glm::mat4 m_projectionMatrix;
    std::vector<RenderEntry> m_renderEntries;
    RenderResourceManager m_resourceManager;
    EngineLevel* m_cachedLevel{ nullptr };

    UIManager m_uiManager;

    std::shared_ptr<OpenGLTextRenderer> m_textRenderer;
    std::vector<TextCommand> m_textQueue;

    GLuint m_uiQuadProgram{0};
    GLuint m_uiQuadVao{0};
    GLuint m_uiQuadVbo{0};
    std::unordered_map<std::string, GLuint> m_uiQuadPrograms;
    bool m_uiDebugEnabled{false};

public:
    void toggleUIDebug() { m_uiDebugEnabled = !m_uiDebugEnabled; }
    bool isUIDebugEnabled() const { return m_uiDebugEnabled; }
};