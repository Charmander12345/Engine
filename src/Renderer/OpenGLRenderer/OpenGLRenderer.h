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
#include <array>
#include <glm/glm.hpp>

#include "../../Core/ECS/Components.h"

class OpenGLObject2D;
class OpenGLObject3D;

struct WindowHitTestContext
{
    int titlebarHeight{ 50 };
    int resizeBorder{ 6 };
    int buttonStripWidth{ 120 };
    int buttonStripHeight{ 45 };
};

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

    double getLastGpuFrameMs() const { return m_lastGpuFrameMs; }
    double getLastCpuRenderWorldMs() const { return m_cpuRenderWorldMs; }
    double getLastCpuRenderUiMs() const { return m_cpuRenderUiMs; }
    double getLastCpuUiLayoutMs() const { return m_cpuUiLayoutMs; }
    double getLastCpuUiDrawMs() const { return m_cpuUiDrawMs; }
    double getLastCpuEcsMs() const { return m_cpuEcsMs; }

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
    void ensureUIShaderDefaults();
    const std::string& resolveUIShaderPath(const std::string& value, const std::string& fallback);
    bool ensureOcclusionResources();
    void releaseOcclusionResources();
    void updateOcclusionResults();
    bool shouldRenderOcclusion(const OpenGLObject3D* object) const;
    void issueOcclusionQuery(const OpenGLObject3D* object, const glm::vec3& center, float radius, const glm::mat4& viewProj);
    void drawUIPanel(float x0, float y0, float x1, float y1, const Vec4& color, const glm::mat4& projection, GLuint program,
        const Vec4& hoverColor = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f }, bool isHovered = false);
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
    std::vector<RenderEntry> m_meshEntries;
    RenderResourceManager m_resourceManager;
    EngineLevel* m_cachedLevel{ nullptr };

    UIManager m_uiManager;

    std::shared_ptr<OpenGLTextRenderer> m_textRenderer;
    std::vector<TextCommand> m_textQueue;


    GLuint m_uiQuadProgram{0};
    GLuint m_uiQuadVao{0};
    GLuint m_uiQuadVbo{0};
    std::unordered_map<std::string, GLuint> m_uiQuadPrograms;
    std::unordered_map<std::string, std::string> m_uiShaderPathCache;
    std::string m_uiShaderBaseDir;
    std::string m_defaultPanelVertex;
    std::string m_defaultPanelFragment;
    std::string m_defaultButtonVertex;
    std::string m_defaultButtonFragment;
    std::string m_defaultTextVertex;
    std::string m_defaultTextFragment;
    bool m_uiShaderDefaultsInitialized{false};
    ECS::Schema m_lightSchema{};
    bool m_lightSchemaInitialized{false};
    bool m_uiDebugEnabled{false};
    WindowHitTestContext m_hitTestContext{};

    struct OcclusionQueryData
    {
        GLuint queryId{0};
        bool hasResult{false};
        bool lastVisible{true};
        uint8_t occludedFrames{0};
    };

    std::unordered_map<const OpenGLObject3D*, OcclusionQueryData> m_occlusionQueries;
    GLuint m_occlusionVao{0};
    GLuint m_occlusionVbo{0};
    GLuint m_occlusionProgram{0};
    bool m_occlusionResourcesReady{false};
    bool m_occlusionEnabled{true};
    uint32_t m_lastVisibleCount{0};
    uint32_t m_lastHiddenCount{0};
    uint32_t m_lastTotalCount{0};

    static constexpr size_t kFrameQueryCount = 3;
    std::array<GLuint, kFrameQueryCount> m_gpuTimerQueries{};
    size_t m_gpuQueryIndex{0};
    bool m_gpuQueriesInitialized{false};
    double m_lastGpuFrameMs{0.0};
    double m_cpuRenderWorldMs{0.0};
    double m_cpuRenderUiMs{0.0};
    double m_cpuUiLayoutMs{0.0};
    double m_cpuUiDrawMs{0.0};
    double m_cpuEcsMs{0.0};

public:
    void toggleUIDebug() { m_uiDebugEnabled = !m_uiDebugEnabled; }
    bool isUIDebugEnabled() const { return m_uiDebugEnabled; }
    uint32_t getLastVisibleCount() const { return m_lastVisibleCount; }
    uint32_t getLastHiddenCount() const { return m_lastHiddenCount; }
    uint32_t getLastTotalCount() const { return m_lastTotalCount; }
    
};