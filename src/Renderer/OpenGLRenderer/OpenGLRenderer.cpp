#include "OpenGLRenderer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <cmath>
#include <cctype>
#include <cstring>
#include <unordered_set>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Logger.h"
#include "OpenGLMaterial.h"
#include "OpenGLCamera.h"
#include "OpenGLObject2D.h"
#include "OpenGLObject3D.h"
#include "OpenGLTextRenderer.h"
#include "OpenGLShader.h"
#include "OpenGLRenderContext.h"

#include "../../Diagnostics/DiagnosticsManager.h"
#include "../../Core/EngineLevel.h"
#include "../../AssetManager/AssetManager.h"

#include "../RenderResourceManager.h"

#include "../../Core/Asset.h"
#include "../../Core/MathTypes.h"
#include "../EditorTheme.h"
#if ENGINE_EDITOR
#include "../EditorUIBuilder.h"
#include "../../AssetManager/AssetManager.h"
#endif
#include "../../Core/UndoRedoManager.h"
#include "../Texture.h"

namespace
{
	Vec4 HsvToRgb(float hue, float saturation, float value)
	{
		hue = std::fmod(std::max(0.0f, hue), 1.0f) * 6.0f;
		const float c = value * saturation;
		const float x = c * (1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f));
		const float m = value - c;

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;

		if (hue < 1.0f)
		{
			r = c;
			g = x;
		}
		else if (hue < 2.0f)
		{
			r = x;
			g = c;
		}
		else if (hue < 3.0f)
		{
			g = c;
			b = x;
		}
		else if (hue < 4.0f)
		{
			g = x;
			b = c;
		}
		else if (hue < 5.0f)
		{
			r = x;
			b = c;
		}
		else
		{
			r = c;
			b = x;
		}

		return Vec4{ r + m, g + m, b + m, 1.0f };
	}

	struct FrustumPlane
	{
		glm::vec3 normal{0.0f};
		float d{0.0f};
	};

	std::array<FrustumPlane, 6> ExtractFrustumPlanes(const glm::mat4& matrix)
	{
		std::array<FrustumPlane, 6> planes{};

		auto makePlane = [&](float a, float b, float c, float d)
		{
			FrustumPlane plane;
			plane.normal = glm::vec3(a, b, c);
			const float length = glm::length(plane.normal);
			if (length > 0.0f)
			{
				plane.normal /= length;
				plane.d = d / length;
			}
			else
			{
				plane.d = d;
			}
			return plane;
		};

		planes[0] = makePlane(matrix[0][3] + matrix[0][0], matrix[1][3] + matrix[1][0], matrix[2][3] + matrix[2][0], matrix[3][3] + matrix[3][0]); // Left
		planes[1] = makePlane(matrix[0][3] - matrix[0][0], matrix[1][3] - matrix[1][0], matrix[2][3] - matrix[2][0], matrix[3][3] - matrix[3][0]); // Right
		planes[2] = makePlane(matrix[0][3] + matrix[0][1], matrix[1][3] + matrix[1][1], matrix[2][3] + matrix[2][1], matrix[3][3] + matrix[3][1]); // Bottom
		planes[3] = makePlane(matrix[0][3] - matrix[0][1], matrix[1][3] - matrix[1][1], matrix[2][3] - matrix[2][1], matrix[3][3] - matrix[3][1]); // Top
		planes[4] = makePlane(matrix[0][3] + matrix[0][2], matrix[1][3] + matrix[1][2], matrix[2][3] + matrix[2][2], matrix[3][3] + matrix[3][2]); // Near
		planes[5] = makePlane(matrix[0][3] - matrix[0][2], matrix[1][3] - matrix[1][2], matrix[2][3] - matrix[2][2], matrix[3][3] - matrix[3][2]); // Far

		return planes;
	}

	bool IsSphereInsideFrustum(const std::array<FrustumPlane, 6>& planes, const glm::vec3& center, float radius)
	{
		for (const auto& plane : planes)
		{
			const float distance = glm::dot(plane.normal, center) + plane.d;
			if (distance < -radius)
			{
				return false;
			}
		}
		return true;
	}

	float MaxScale(float sx, float sy, float sz)
	{
		return std::max(sx, std::max(sy, sz));
	}

	void ComputeWorldAabb(const glm::vec3& localMin, const glm::vec3& localMax, const glm::mat4& model, glm::vec3& outMin, glm::vec3& outMax)
	{
		const glm::vec3 corners[8] = {
			glm::vec3(localMin.x, localMin.y, localMin.z),
			glm::vec3(localMax.x, localMin.y, localMin.z),
			glm::vec3(localMax.x, localMax.y, localMin.z),
			glm::vec3(localMin.x, localMax.y, localMin.z),
			glm::vec3(localMin.x, localMin.y, localMax.z),
			glm::vec3(localMax.x, localMin.y, localMax.z),
			glm::vec3(localMax.x, localMax.y, localMax.z),
			glm::vec3(localMin.x, localMax.y, localMax.z)
		};

		glm::vec3 minPos(std::numeric_limits<float>::max());
		glm::vec3 maxPos(std::numeric_limits<float>::lowest());
		for (const auto& corner : corners)
		{
			const glm::vec4 world = model * glm::vec4(corner, 1.0f);
			const glm::vec3 pos = glm::vec3(world);
			minPos = glm::min(minPos, pos);
			maxPos = glm::max(maxPos, pos);
		}
		outMin = minPos;
		outMax = maxPos;
	}

	bool IsAabbInsideFrustum(const std::array<FrustumPlane, 6>& planes, const glm::vec3& minBounds, const glm::vec3& maxBounds)
	{
		for (const auto& plane : planes)
		{
			glm::vec3 positive = minBounds;
			if (plane.normal.x >= 0.0f) positive.x = maxBounds.x;
			if (plane.normal.y >= 0.0f) positive.y = maxBounds.y;
			if (plane.normal.z >= 0.0f) positive.z = maxBounds.z;
			if (glm::dot(plane.normal, positive) + plane.d < 0.0f)
			{
				return false;
			}
		}
		return true;
	}

	static SDL_HitTestResult SDLCALL WindowHitTestCallback(SDL_Window* window, const SDL_Point* area, void* data)
	{
		if (!window || !area || !data)
		{
			return SDL_HITTEST_NORMAL;
		}

		const auto* ctx = static_cast<WindowHitTestContext*>(data);
		int width = 0;
		int height = 0;
		SDL_GetWindowSizeInPixels(window, &width, &height);

		const int x = area->x;
		const int y = area->y;

		// Exclude button strip area from draggable region
		if (y < ctx->buttonStripHeight)
		{
			if (ctx->buttonsOnLeft && x < ctx->buttonStripWidth)
			{
				return SDL_HITTEST_NORMAL;
			}
			if (!ctx->buttonsOnLeft && x >= width - ctx->buttonStripWidth)
			{
				return SDL_HITTEST_NORMAL;
			}
		}

		const bool left = x < ctx->resizeBorder;
		const bool right = x >= width - ctx->resizeBorder;
		const bool top = y < ctx->resizeBorder;
		const bool bottom = y >= height - ctx->resizeBorder;

		if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
		if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
		if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
		if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
		if (left) return SDL_HITTEST_RESIZE_LEFT;
		if (right) return SDL_HITTEST_RESIZE_RIGHT;
		if (top) return SDL_HITTEST_RESIZE_TOP;
		if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;

		if (y < ctx->titlebarHeight)
		{
			return SDL_HITTEST_DRAGGABLE;
		}

		return SDL_HITTEST_NORMAL;
	}

	// ── RenderTransform helper ───────────────────────────────────────────
	// Computes a 2D transformation matrix around the element's pivot point.
	// The returned matrix is meant to be multiplied onto the ortho projection
	// so that all subsequent draw calls are visually transformed.
	glm::mat4 ComputeRenderTransformMatrix(const RenderTransform& rt,
										   float x0, float y0,
										   float w, float h)
	{
		const float pivotX = x0 + rt.pivot.x * w;
		const float pivotY = y0 + rt.pivot.y * h;

		// T(pivot) * Translate * Rotate * Scale * Shear * T(-pivot)
		glm::mat4 m(1.0f);
		m = glm::translate(m, glm::vec3(pivotX, pivotY, 0.0f));
		m = glm::translate(m, glm::vec3(rt.translation.x, rt.translation.y, 0.0f));
		m = glm::rotate(m, glm::radians(rt.rotation), glm::vec3(0.0f, 0.0f, 1.0f));
		m = glm::scale(m, glm::vec3(rt.scale.x, rt.scale.y, 1.0f));
		// Shear as a manual matrix multiply
		if (rt.shear.x != 0.0f || rt.shear.y != 0.0f)
		{
			glm::mat4 shearMat(1.0f);
			shearMat[1][0] = rt.shear.x; // x sheared by y
			shearMat[0][1] = rt.shear.y; // y sheared by x
			m = m * shearMat;
		}
		m = glm::translate(m, glm::vec3(-pivotX, -pivotY, 0.0f));
		return m;
	}

}

OpenGLRenderer::OpenGLRenderer()
{
	m_initialized = false;
	m_name = "OpenGL Renderer";
	m_window = nullptr;
	m_glContext = nullptr;

	m_camera = std::make_unique<OpenGLCamera>();

	// Initialize projection matrix (will be updated with proper aspect ratio)
	m_projectionMatrix = glm::perspective(
		glm::radians(45.0f),  // FOV
		800.0f / 600.0f,      // Aspect ratio (will be updated)
		0.1f,                 // Near plane
		100.0f                // Far plane
	);

	// Note: any initial camera offset should be expressed as initial camera position.
}

void OpenGLRenderer::shutdown()
{
	if (m_gpuQueriesInitialized)
	{
		glDeleteQueries(static_cast<GLsizei>(m_gpuTimerQueries.size()), m_gpuTimerQueries.data());
		m_gpuQueriesInitialized = false;
		m_gpuTimerQueries.fill(0);
	}
	m_postProcessStack.shutdown();
	releaseHzbResources();
	releaseBoundsDebugResources();
	releaseHeightFieldDebugResources();
	releaseDisplacementResources();
	releaseSkyboxResources();
	releaseShadowResources();
	releasePointShadowResources();
	releaseCsmResources();
#if ENGINE_EDITOR
	releasePickFbo();
	releaseOutlineResources();
#endif
	releaseInstanceResources();
	releaseOitResources();
	releaseUiFbo();
#if ENGINE_EDITOR
	releaseAllTabFbos();
#endif
	m_particleSystem.shutdown();
	m_textureStreaming.shutdown();

#if ENGINE_EDITOR
	// Release mesh viewer state before destroying GL resources.
	m_meshViewers.clear();

	// Destroy all popup windows before releasing main GL resources.
	m_popupWindows.clear();
#endif

	auto& logger = Logger::Instance();
	logger.log(Logger::Category::Rendering, "OpenGLRenderer shutdown: releasing GPU resources...", Logger::LogLevel::INFO);

	// Ensure GL context is current so destructors can safely delete GL objects.
	if (m_window && m_glContext)
	{
		SDL_GL_MakeCurrent(m_window, m_glContext);
	}

	OpenGLObject2D::ClearCache();
	OpenGLObject3D::ClearCache();

#if ENGINE_EDITOR
	if (m_popupUiVao)
	{
		glDeleteVertexArrays(1, &m_popupUiVao);
		m_popupUiVao = 0;
	}
#endif
	if (m_uiQuadVbo)
	{
		glDeleteBuffers(1, &m_uiQuadVbo);
		m_uiQuadVbo = 0;
	}
	if (m_uiQuadVao)
	{
		glDeleteVertexArrays(1, &m_uiQuadVao);
		m_uiQuadVao = 0;
	}
	if (m_uiQuadProgram)
	{
		glDeleteProgram(m_uiQuadProgram);
		m_uiQuadProgram = 0;
	}
	if (m_uiImageProgram)
	{
		glDeleteProgram(m_uiImageProgram);
		m_uiImageProgram = 0;
	}
	for (auto& entry : m_uiQuadPrograms)
	{
		if (entry.second)
		{
			glDeleteProgram(entry.second);
		}
	}
	m_uiQuadPrograms.clear();

	for (auto& [path, tex] : m_uiTextureCache)
	{
		if (tex != 0)
		{
			glDeleteTextures(1, &tex);
		}
	}
	m_uiTextureCache.clear();

	if (m_textRenderer)
	{
		m_textRenderer->shutdown();
		m_textRenderer.reset();
	}

	// Any additional renderer-owned GPU resources can be released here.
	m_camera.reset();

	logger.log(Logger::Category::Rendering, "OpenGLRenderer shutdown: done.", Logger::LogLevel::INFO);
}

OpenGLRenderer::~OpenGLRenderer()
{
	SDL_GL_DestroyContext(m_glContext);
	SDL_DestroyWindow(m_window);
}

bool OpenGLRenderer::initialize()
{
	auto& logger = Logger::Instance();
	logger.log(Logger::Category::Rendering, "OpenGLRenderer initialize started", Logger::LogLevel::INFO);

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

	logger.log(Logger::Category::Rendering, "Creating Window...", Logger::LogLevel::INFO);

	auto& diagnostics = DiagnosticsManager::Instance();
	Vec2 windowSize = diagnostics.getWindowSize();
	int width = static_cast<int>(windowSize.x);
	int height = static_cast<int>(windowSize.y);
	DiagnosticsManager::WindowState windowState = diagnostics.getWindowState();

	m_window = SDL_CreateWindow("HorizonEngine", width, height,
		SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
	if (!m_window) {
		logger.log(Logger::Category::Rendering, std::string("Failed to create window: ") + SDL_GetError(), Logger::LogLevel::ERROR);
		SDL_Quit();
		return false;
	}
	logger.log(Logger::Category::Rendering, "Window created successfully.", Logger::LogLevel::INFO);
	logger.log(Logger::Category::Rendering, "Creating OpenGL context...", Logger::LogLevel::INFO);

	m_glContext = SDL_GL_CreateContext(m_window);
	if (!m_glContext) {
		logger.log(Logger::Category::Rendering, std::string("Failed to create OpenGL context: ") + SDL_GetError(), Logger::LogLevel::ERROR);
		SDL_DestroyWindow(m_window);
		SDL_Quit();
		return false;
	}

	if (!SDL_GL_MakeCurrent(m_window, m_glContext)) {
		logger.log(Logger::Category::Rendering, std::string("Failed to make created context current: ") + SDL_GetError(), Logger::LogLevel::ERROR);
		SDL_GL_DestroyContext(m_glContext);
		SDL_DestroyWindow(m_window);
		SDL_Quit();
		return false;
	}

	logger.log(Logger::Category::Rendering, "OpenGL context created successfully.", Logger::LogLevel::INFO);

	if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress))
	{
		logger.log(Logger::Category::Rendering, "Failed to initialize GLAD", Logger::LogLevel::ERROR);
		return false;
	}

	logger.log(Logger::Category::Rendering, "GLAD initialised.", Logger::LogLevel::INFO);

	// Populate GPU / VRAM info for hardware diagnostics
	{
		GpuInfo gpuInfo;
		const char* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
		const char* v = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
		const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
		if (r) gpuInfo.renderer      = r;
		if (v) gpuInfo.vendor        = v;
		if (ver) gpuInfo.driverVersion = ver;

		// NVIDIA: GL_NVX_gpu_memory_info
		GLint totalKB = 0;
		glGetIntegerv(0x9048, &totalKB); // GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
		if (totalKB > 0)
		{
			gpuInfo.vramTotalMB = totalKB / 1024;
			GLint freeKB = 0;
			glGetIntegerv(0x9049, &freeKB); // GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
			gpuInfo.vramFreeMB = freeKB / 1024;
		}
		else
		{
			// AMD: GL_ATI_meminfo  (GL_VBO_FREE_MEMORY_ATI = 0x87FB)
			GLint atiInfo[4] = {};
			glGetIntegerv(0x87FB, atiInfo);
			glGetError(); // clear potential GL_INVALID_ENUM
			if (atiInfo[0] > 0)
				gpuInfo.vramFreeMB = atiInfo[0] / 1024;
		}

		DiagnosticsManager::Instance().setGpuInfo(gpuInfo);
	}

	int nrAttributes;
	glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &nrAttributes);
	logger.log(Logger::Category::Rendering, "Max number of vertex attributes: " + std::to_string(nrAttributes), Logger::LogLevel::INFO);

	glEnable(GL_DEPTH_TEST);

	m_initialized = true;
	m_uiManager.setRenderer(this);
	logger.log(Logger::Category::Rendering, "Initialisation of the OpenGL renderer complete.", Logger::LogLevel::INFO);

	glGenQueries(static_cast<GLsizei>(m_gpuTimerQueries.size()), m_gpuTimerQueries.data());
	m_gpuQueriesInitialized = true;

	// Initialize particle system (soft-fail: game runs without particles).
	{
		const auto shaderDir = (std::filesystem::current_path() / "shaders").string();
		if (!m_particleSystem.initialize(shaderDir))
		{
			logger.log(Logger::Category::Rendering, "ParticleSystem: init failed (particles disabled).", Logger::LogLevel::WARNING);
		}
	}

	// Initialize texture streaming manager (soft-fail: streaming disabled on failure).
	if (!m_textureStreaming.initialize())
	{
		logger.log(Logger::Category::Rendering, "TextureStreamingManager: init failed (streaming disabled).", Logger::LogLevel::WARNING);
	}

	// Prime render resources as soon as the renderer is ready.
	{
		if (m_resourceManager.prepareActiveLevel())
		{
			DiagnosticsManager::Instance().setScenePrepared(true);
		}
	}
	{
		auto& ecs = ECS::ECSManager::Instance();
		m_renderEntries.clear();
		const auto renderables = m_resourceManager.buildRenderablesForSchema(ecs.getRenderSchema());
		m_renderEntries.reserve(renderables.size());
		for (const auto& renderable : renderables)
		{
			RenderEntry entry;
			entry.entity = renderable.entity;
			entry.transform = renderable.transform;
			entry.object3D = std::static_pointer_cast<OpenGLObject3D>(renderable.object3D);
			entry.object2D = std::static_pointer_cast<OpenGLObject2D>(renderable.object2D);
			if (!entry.object3D && !entry.object2D)
			{
				continue;
			}
			m_renderEntries.push_back(std::move(entry));
		}
		m_cachedLevel = DiagnosticsManager::Instance().getActiveLevelSoft();

		// Create skeletal animators for skinned meshes
		m_entityAnimators.clear();
		for (const auto& entry : m_renderEntries)
		{
			if (entry.object3D && entry.object3D->isSkinned() && entry.object3D->getSkeleton())
			{
				auto animator = std::make_shared<SkeletalAnimator>();
				animator->setSkeleton(entry.object3D->getSkeleton());
				// Auto-play the first animation if available
				if (!entry.object3D->getSkeleton()->animations.empty())
				{
					animator->playAnimation(0, true);
				}
				m_entityAnimators[static_cast<unsigned int>(entry.entity)] = animator;
			}
		}

		// Restore editor camera from the level if available
		if (m_cachedLevel && m_cachedLevel->hasEditorCamera() && m_camera)
		{
			m_camera->setPosition(m_cachedLevel->getEditorCameraPosition());
			const Vec2& rot = m_cachedLevel->getEditorCameraRotation();
			m_camera->setRotationDegrees(rot.x, rot.y);
		}
	}

	// Window stays hidden until main.cpp shows it after freeing the console.
	switch (windowState)
	{
	case DiagnosticsManager::WindowState::Fullscreen:
		SDL_SetWindowFullscreen(m_window, SDL_WINDOW_FULLSCREEN);
		break;
	case DiagnosticsManager::WindowState::Normal:
		SDL_RestoreWindow(m_window);
		break;
	case DiagnosticsManager::WindowState::Maximized:
		SDL_MaximizeWindow(m_window);
		break;
	}

	SDL_ClearError();
	if (SDL_SetWindowHitTest(m_window, WindowHitTestCallback, &m_hitTestContext) != 0)
	{
		const char* err = SDL_GetError();
		std::string msg = "Failed to set window hit test";
		if (err && *err)
		{
			msg += ": ";
			msg += err;
		}
		else
		{
			msg += ": (no SDL error string)";
		}
		logger.log(Logger::Category::Rendering, msg, Logger::LogLevel::WARNING);
	}
	else
	{
		logger.log(Logger::Category::Rendering, "Window hit test enabled.", Logger::LogLevel::INFO);
	}

	// Initialise shader hot-reload watcher on the shaders directory.
	{
		const std::string shadersDir = (std::filesystem::current_path() / "shaders").string();
		m_shaderHotReload.init(shadersDir);
	}

	return true;
}

void OpenGLRenderer::clear()
{
	glClearColor(m_clearColor.x, m_clearColor.y, m_clearColor.z, m_clearColor.w);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderer::setClearColor(const Vec4& color)
{
	m_clearColor = color;
}

Vec4 OpenGLRenderer::getClearColor() const
{
	return m_clearColor;
}

void OpenGLRenderer::present()
{
	if (m_window)
	{
		SDL_GL_SwapWindow(m_window);
	}
}

// ---------------------------------------------------------------------------
// Shader Hot-Reload
// ---------------------------------------------------------------------------
void OpenGLRenderer::handleShaderHotReload()
{
	auto changed = m_shaderHotReload.poll();
	if (changed.empty())
		return;

	auto& logger = Logger::Instance();
	for (const auto& path : changed)
	{
		logger.log(Logger::Category::Rendering,
			"ShaderHotReload: changed -> " + path,
			Logger::LogLevel::INFO);
	}

	// 1. Invalidate material cache (3D scene shaders loaded from files).
	//    Existing render entries keep their old materials alive via shared_ptr
	//    until we rebuild the entries below.
	OpenGLObject3D::ClearCache();
	m_resourceManager.clearCaches();

	// 2. Delete and clear cached UI shader programs.
	for (auto& [key, prog] : m_uiQuadPrograms)
	{
		if (prog)
			glDeleteProgram(prog);
	}
	m_uiQuadPrograms.clear();
	m_uiPanelUniformCache.clear();
	m_uiQuadProgram = 0;

	// 3. Reload PostProcessStack programs (resolve, bloom, SSAO).
	m_postProcessStack.reloadPrograms();

	// 4. Rebuild all render entries from the ECS so new materials are created.
	{
		auto& ecs = ECS::ECSManager::Instance();
		m_renderEntries.clear();
		const auto renderables = m_resourceManager.buildRenderablesForSchema(ecs.getRenderSchema());
		m_renderEntries.reserve(renderables.size());
		for (const auto& renderable : renderables)
		{
			RenderEntry entry;
			entry.entity = renderable.entity;
			entry.transform = renderable.transform;
			entry.object3D = std::static_pointer_cast<OpenGLObject3D>(renderable.object3D);
			entry.object2D = std::static_pointer_cast<OpenGLObject2D>(renderable.object2D);
			if (!entry.object3D && !entry.object2D)
				continue;
			m_renderEntries.push_back(std::move(entry));
		}
	}

	// 5. Mark UI cache as dirty so it redraws with the new programs.
	m_uiFboCacheValid = false;

	logger.log(Logger::Category::Rendering,
		"ShaderHotReload: reloaded " + std::to_string(changed.size()) + " shader(s)",
		Logger::LogLevel::INFO);
}

// ---------------------------------------------------------------------------
// requestShaderReload  – force a full shader hot-reload (called from UI)
// ---------------------------------------------------------------------------
void OpenGLRenderer::requestShaderReload()
{
	auto& logger = Logger::Instance();
	logger.log(Logger::Category::Rendering, "requestShaderReload: forced reload", Logger::LogLevel::INFO);

	OpenGLObject3D::ClearCache();
	m_resourceManager.clearCaches();

	for (auto& [key, prog] : m_uiQuadPrograms)
	{
		if (prog)
			glDeleteProgram(prog);
	}
	m_uiQuadPrograms.clear();
	m_uiPanelUniformCache.clear();
	m_uiQuadProgram = 0;

	m_postProcessStack.reloadPrograms();

	{
		auto& ecs = ECS::ECSManager::Instance();
		m_renderEntries.clear();
		const auto renderables = m_resourceManager.buildRenderablesForSchema(ecs.getRenderSchema());
		m_renderEntries.reserve(renderables.size());
		for (const auto& renderable : renderables)
		{
			RenderEntry entry;
			entry.entity = renderable.entity;
			entry.transform = renderable.transform;
			entry.object3D = std::static_pointer_cast<OpenGLObject3D>(renderable.object3D);
			entry.object2D = std::static_pointer_cast<OpenGLObject2D>(renderable.object2D);
			if (!entry.object3D && !entry.object2D)
				continue;
			m_renderEntries.push_back(std::move(entry));
		}
	}

	m_uiFboCacheValid = false;
}

// ---------------------------------------------------------------------------
// getRenderPassInfo  – returns a description of every render pass for the
//                      Render-Pass-Debugger tab
// ---------------------------------------------------------------------------
std::vector<Renderer::RenderPassInfo> OpenGLRenderer::getRenderPassInfo() const
{
	std::vector<RenderPassInfo> passes;

	// 1. Shadow Map Pass (Directional – CSM)
	{
		RenderPassInfo p;
		p.name     = "Shadow Map (Directional CSM)";
		p.category = "Shadow";
		p.enabled  = m_shadow.enabled;
		p.fboWidth = kShadowMapSize;
		p.fboHeight = kShadowMapSize;
		p.fboFormat = "Depth24 Array";
		p.details  = std::to_string(m_shadow.count) + " cascade(s), "
				   + std::to_string(m_shadowCasterList.size()) + " caster(s)";
		passes.push_back(std::move(p));
	}

	// 2. Point Light Shadow Pass (Cube Maps)
	{
		RenderPassInfo p;
		p.name     = "Shadow Map (Point Lights)";
		p.category = "Shadow";
		p.enabled  = m_shadow.enabled && (m_pointShadow.count > 0);
		p.fboWidth = kPointShadowMapSize;
		p.fboHeight = kPointShadowMapSize;
		p.fboFormat = "DepthCube Array";
		p.details  = std::to_string(m_pointShadow.count) + " point shadow(s)";
		passes.push_back(std::move(p));
	}

	// 3. Skybox Pass
	{
		RenderPassInfo p;
		p.name     = "Skybox";
		p.category = "Geometry";
		p.enabled  = (m_skybox.cubemap != 0);
		p.fboFormat = "RGBA16F (HDR)";
		p.details  = m_skybox.loadedPath.empty() ? "no skybox" : m_skybox.loadedPath;
		passes.push_back(std::move(p));
	}

	// 4. Geometry Pass (Opaque)
	{
		const auto& vp = getViewportSize();
		RenderPassInfo p;
		p.name     = "Geometry (Opaque)";
		p.category = "Geometry";
		p.enabled  = true;
		p.fboWidth = static_cast<int>(vp.x);
		p.fboHeight = static_cast<int>(vp.y);
		p.fboFormat = m_postProcessEnabled ? "RGBA16F (HDR)" : "RGBA8 (Tab FBO)";
		p.details  = std::to_string(m_lastVisibleCount) + " visible, "
				   + std::to_string(m_lastHiddenCount) + " culled, "
				   + std::to_string(m_renderEntries.size() + m_meshEntries.size()) + " entries";
		passes.push_back(std::move(p));
	}

	// 5. Particle Pass
	{
		RenderPassInfo p;
		p.name     = "Particles";
		p.category = "Geometry";
		p.enabled  = DiagnosticsManager::Instance().isPIEActive();
		p.fboFormat = "RGBA16F (HDR)";
		p.details  = "GL_PROGRAM_POINT_SIZE";
		passes.push_back(std::move(p));
	}

	// 6. OIT Transparent Pass
	{
		RenderPassInfo p;
		p.name     = "OIT Transparent";
		p.category = "Geometry";
		p.enabled  = m_oit.enabled;
		p.fboFormat = "RGBA16F (Accum) + R8 (Revealage)";
		p.details  = std::to_string(m_transparentDrawList.size()) + " transparent object(s)";
		passes.push_back(std::move(p));
	}

	// 7. HeightField Debug
	{
		RenderPassInfo p;
		p.name     = "HeightField Debug";
		p.category = "Overlay";
		p.enabled  = m_hfDebug.enabled;
		p.fboFormat = "—";
		p.details  = "wireframe overlay";
		passes.push_back(std::move(p));
	}

	// 8. HZB Build
	{
		RenderPassInfo p;
		p.name     = "HZB (Hi-Z Buffer)";
		p.category = "Post-Process";
		p.enabled  = m_occlusionEnabled && (m_lastTotalCount >= 20);
		p.fboWidth = m_hzbWidth;
		p.fboHeight = m_hzbHeight;
		p.fboFormat = "R32F Mipchain";
		p.details  = std::to_string(m_hzbMipLevels) + " mip level(s)";
		passes.push_back(std::move(p));
	}

	#if ENGINE_EDITOR
	// 9. Pick Pass
	{
		RenderPassInfo p;
		p.name     = "Pick Buffer";
		p.category = "Utility";
		p.enabled  = (m_pick.fbo != 0);
		p.fboFormat = "RGBA8 (Entity ID)";
		p.details  = "entity selection";
		passes.push_back(std::move(p));
	}
#endif // ENGINE_EDITOR — pick pass

	// 10. Post-Process Resolve (Bloom + SSAO + Tone Mapping + Gamma)
	{
		RenderPassInfo p;
		p.name     = "Post-Process Resolve";
		p.category = "Post-Process";
		p.enabled  = m_postProcessEnabled && m_postProcessStack.isInitialized();
		p.fboFormat = "RGBA8 (Tab FBO)";
		std::string det;
		if (m_bloomEnabled) det += "Bloom ";
		if (m_ssaoEnabled)  det += "SSAO ";
		if (m_gammaEnabled) det += "Gamma ";
		if (m_toneMappingEnabled) det += "ToneMap ";
		if (det.empty()) det = "passthrough";
		p.details = det;
		passes.push_back(std::move(p));
	}

	// 11. Bloom Pass
	{
		RenderPassInfo p;
		p.name     = "Bloom";
		p.category = "Post-Process";
		p.enabled  = m_bloomEnabled && m_postProcessEnabled;
		p.fboFormat = "RGBA16F Mipchain";
		p.details  = "5 mip downsample + blur, threshold=" + std::to_string(m_bloomThreshold).substr(0, 4)
				   + ", intensity=" + std::to_string(m_bloomIntensity).substr(0, 4);
		passes.push_back(std::move(p));
	}

	// 12. SSAO Pass
	{
		RenderPassInfo p;
		p.name     = "SSAO";
		p.category = "Post-Process";
		p.enabled  = m_ssaoEnabled && m_postProcessEnabled;
		p.fboFormat = "R8 (half-res)";
		p.details  = "32-sample kernel + blur";
		passes.push_back(std::move(p));
	}

#if ENGINE_EDITOR
	// 13. Grid Overlay
	{
		RenderPassInfo p;
		p.name     = "Viewport Grid";
		p.category = "Overlay";
		p.enabled  = m_gridVisible && !DiagnosticsManager::Instance().isPIEActive();
		p.fboFormat = "—";
		p.details  = "infinite XZ grid, size=" + std::to_string(m_gridSize).substr(0, 4);
		passes.push_back(std::move(p));
	}

	// 14. Collider Debug
	{
		RenderPassInfo p;
		p.name     = "Collider Debug";
		p.category = "Overlay";
		p.enabled  = m_collidersVisible;
		p.fboFormat = "—";
		p.details  = "wireframe collider shapes";
		passes.push_back(std::move(p));
	}

	// 15. Bone Debug
	{
		RenderPassInfo p;
		p.name     = "Bone Debug";
		p.category = "Overlay";
		p.enabled  = m_bonesVisible;
		p.fboFormat = "—";
		p.details  = "skeleton lines + joint markers";
		passes.push_back(std::move(p));
	}

	// 16. Selection Outline
	{
		RenderPassInfo p;
		p.name     = "Selection Outline";
		p.category = "Overlay";
		p.enabled  = !m_selectedEntities.empty();
		p.fboFormat = "—";
		p.details  = std::to_string(m_selectedEntities.size()) + " selected";
		passes.push_back(std::move(p));
	}

	// 17. Gizmo
	{
		RenderPassInfo p;
		p.name     = "Gizmo";
		p.category = "Overlay";
		p.enabled  = !m_selectedEntities.empty() && !DiagnosticsManager::Instance().isPIEActive();
		p.fboFormat = "—";
		p.details  = "translate/rotate/scale";
		passes.push_back(std::move(p));
	}
#endif // ENGINE_EDITOR

	// 18. FXAA Pass (deferred)
	{
		RenderPassInfo p;
		p.name     = "FXAA (Deferred)";
		p.category = "Post-Process";
		p.enabled  = m_postProcessEnabled && (m_aaMode == AntiAliasingMode::FXAA);
		p.fboFormat = "RGBA8 (Tab FBO)";
		p.details  = "applied after overlays";
		passes.push_back(std::move(p));
	}

	// 19. UI Pass
	{
		RenderPassInfo p;
		p.name     = "UI Rendering";
		p.category = "UI";
		p.enabled  = true;
		p.fboFormat = "RGBA8 (Tab FBO)";
		p.details  = "editor widgets + viewport UI";
		passes.push_back(std::move(p));
	}

	return passes;
}

void OpenGLRenderer::render()
{
	if (!m_initialized)
	{
		return;
	}

	// Poll for shader file changes and hot-reload if needed.
	handleShaderHotReload();

	// Process pending texture streaming uploads (max 4 per frame).
	if (m_textureStreamingEnabled && m_textureStreaming.isInitialized())
	{
		m_textureStreaming.processUploads(4);
	}

	const uint64_t freq = SDL_GetPerformanceFrequency();

	if (m_gpuQueriesInitialized)
	{
		glBeginQuery(GL_TIME_ELAPSED, m_gpuTimerQueries[m_gpuQueryIndex]);
	}

	int width = 0, height = 0;
	SDL_GetWindowSizeInPixels(m_window, &width, &height);
	m_cachedWindowWidth = width;
	m_cachedWindowHeight = height;

	#if ENGINE_EDITOR
	// Ensure active tab has a valid FBO (cache for renderWorld reuse)
	m_cachedActiveTab = nullptr;
	for (auto& tab : m_editorTabs)
	{
		if (tab.active)
		{
			if (!tab.renderTarget)
				tab.renderTarget = std::make_unique<OpenGLRenderTarget>();
			tab.renderTarget->resize(width, height);
			m_cachedActiveTab = &tab;
			break;
		}
	}
	EditorTab* activeTab = m_cachedActiveTab;
#endif

	// Render content for the active tab (world or mesh viewer)
	const uint64_t worldStart = SDL_GetPerformanceCounter();
	{
		// Tick camera transition (smooth interpolation)
		if (m_cameraTransition.active && m_camera && freq > 0)
		{
			const uint64_t now = SDL_GetPerformanceCounter();
			float dt = 0.0f;
			if (m_lastTransitionTick > 0)
				dt = static_cast<float>(static_cast<double>(now - m_lastTransitionTick) / static_cast<double>(freq));
			m_lastTransitionTick = now;
			if (dt > 0.0f && dt < 1.0f)
			{
				m_cameraTransition.elapsed += dt;
				float t = m_cameraTransition.elapsed / m_cameraTransition.duration;
				if (t >= 1.0f)
				{
					t = 1.0f;
					m_cameraTransition.active = false;
				}
				// Smooth-step easing: 3t² - 2t³
				const float st = t * t * (3.0f - 2.0f * t);
				const Vec3 pos{
					m_cameraTransition.startPos.x + st * (m_cameraTransition.endPos.x - m_cameraTransition.startPos.x),
					m_cameraTransition.startPos.y + st * (m_cameraTransition.endPos.y - m_cameraTransition.startPos.y),
					m_cameraTransition.startPos.z + st * (m_cameraTransition.endPos.z - m_cameraTransition.startPos.z)
				};
				const float yaw = m_cameraTransition.startYaw + st * (m_cameraTransition.endYaw - m_cameraTransition.startYaw);
				const float pitch = m_cameraTransition.startPitch + st * (m_cameraTransition.endPitch - m_cameraTransition.startPitch);
				m_camera->setPosition(pos);
				m_camera->setRotationDegrees(yaw, pitch);
			}
		}
		else
		{
			m_lastTransitionTick = 0;
		}

		// Tick camera path (Catmull-Rom spline playback)
		if (m_cameraPathActive && !m_cameraPathPaused && m_camera && freq > 0 && m_cameraPath.isValid())
		{
			const uint64_t now = SDL_GetPerformanceCounter();
			float dt = 0.0f;
			if (m_lastPathTick > 0)
				dt = static_cast<float>(static_cast<double>(now - m_lastPathTick) / static_cast<double>(freq));
			m_lastPathTick = now;
			if (dt > 0.0f && dt < 1.0f)
			{
				m_cameraPathElapsed += dt;
				if (!m_cameraPath.loop && m_cameraPathElapsed >= m_cameraPath.duration)
				{
					m_cameraPathElapsed = m_cameraPath.duration;
					m_cameraPathActive = false;
				}
				const float t = m_cameraPath.duration > 0.0f
					? m_cameraPathElapsed / m_cameraPath.duration
					: 1.0f;
				const CameraPathPoint pt = m_cameraPath.evaluate(t);
				m_camera->setPosition(pt.position);
				m_camera->setRotationDegrees(pt.yaw, pt.pitch);
			}
		}
		else if (!m_cameraPathActive)
		{
			m_lastPathTick = 0;
		}

		#if ENGINE_EDITOR
		if (activeTab)
		{
			// Texture viewer tabs: render texture preview into the tab FBO
			auto texViewerIt = m_textureViewers.find(activeTab->id);
			if (texViewerIt != m_textureViewers.end() && texViewerIt->second)
			{
				auto* viewer = texViewerIt->second.get();
				GLuint texId = viewer->getGLTextureId();
				if (texId != 0 && activeTab->renderTarget && activeTab->renderTarget->isValid())
				{
					auto* glRT = static_cast<OpenGLRenderTarget*>(activeTab->renderTarget.get());
					glBindFramebuffer(GL_FRAMEBUFFER, glRT->getGLFramebuffer());
					glViewport(0, 0, width, height);

					// Clear with dark background
					glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
					glClear(GL_COLOR_BUFFER_BIT);

					// Ensure channel-isolation shader
					if (m_texViewerChannelProgram == 0)
					{
						const char* vsSource = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vTexCoord;
uniform mat4 uProjection;
uniform vec4 uRect;
void main() {
	gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
	vec2 normalised = (aPos - uRect.xy) / max(uRect.zw - uRect.xy, vec2(1.0));
	vTexCoord = vec2(normalised.x, 1.0 - normalised.y);
}
)";
						const char* fsSource = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform ivec4 uChannelMask;
uniform int uCheckerboard;
void main() {
	// Checkerboard background
	vec3 bg = vec3(0.12, 0.12, 0.15);
	if (uCheckerboard != 0) {
		float checker = mod(floor(vTexCoord.x * 32.0) + floor(vTexCoord.y * 32.0), 2.0);
		bg = mix(vec3(0.3), vec3(0.5), checker);
	}

	vec4 texel = texture(uTexture, vTexCoord);

	// Apply channel mask
	float r = (uChannelMask.x != 0) ? texel.r : 0.0;
	float g = (uChannelMask.y != 0) ? texel.g : 0.0;
	float b = (uChannelMask.z != 0) ? texel.b : 0.0;
	float a = (uChannelMask.w != 0) ? texel.a : 1.0;

	// If only one channel is on, show it as grayscale
	int count = uChannelMask.x + uChannelMask.y + uChannelMask.z;
	if (count == 1) {
		float v = r + g + b;
		FragColor = vec4(mix(bg, vec3(v), a), 1.0);
	} else {
		FragColor = vec4(mix(bg, vec3(r, g, b), a), 1.0);
	}
}
)";
						GLuint vs = glCreateShader(GL_VERTEX_SHADER);
						glShaderSource(vs, 1, &vsSource, nullptr);
						glCompileShader(vs);
						GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
						glShaderSource(fs, 1, &fsSource, nullptr);
						glCompileShader(fs);
						m_texViewerChannelProgram = glCreateProgram();
						glAttachShader(m_texViewerChannelProgram, vs);
						glAttachShader(m_texViewerChannelProgram, fs);
						glLinkProgram(m_texViewerChannelProgram);
						glDeleteShader(vs);
						glDeleteShader(fs);

						m_texViewerChannelUniforms.projection = glGetUniformLocation(m_texViewerChannelProgram, "uProjection");
						m_texViewerChannelUniforms.rect = glGetUniformLocation(m_texViewerChannelProgram, "uRect");
						m_texViewerChannelUniforms.texture = glGetUniformLocation(m_texViewerChannelProgram, "uTexture");
						m_texViewerChannelUniforms.channelMask = glGetUniformLocation(m_texViewerChannelProgram, "uChannelMask");
						m_texViewerChannelUniforms.checkerboard = glGetUniformLocation(m_texViewerChannelProgram, "uCheckerboard");
					}

					ensureUIQuadRenderer();

					// Compute image rect with zoom and pan, centered in the viewport content area
					const Vec4 vpRect = m_cachedViewportContentRect;
					float vpX = vpRect.x;
					float vpY = vpRect.y;
					float vpW = vpRect.z > 0.0f ? vpRect.z : static_cast<float>(width);
					float vpH = vpRect.w > 0.0f ? vpRect.w : static_cast<float>(height);

					// Always compute fit-to-window scale as the base (no upscale beyond 1:1)
					float fitScale = std::min(vpW / std::max(1.0f, static_cast<float>(viewer->getWidth())),
											  vpH / std::max(1.0f, static_cast<float>(viewer->getHeight())));
					fitScale = std::min(fitScale, 1.0f);

					// Zoom is relative to fitScale: zoom 1.0 = fit-to-window
					float imgW = static_cast<float>(viewer->getWidth()) * fitScale * viewer->getZoom();
					float imgH = static_cast<float>(viewer->getHeight()) * fitScale * viewer->getZoom();

					float cx = vpX + vpW * 0.5f + viewer->getPanX();
					float cy = vpY + vpH * 0.5f + viewer->getPanY();

					float x0 = cx - imgW * 0.5f;
					float y0 = cy - imgH * 0.5f;
					float x1 = cx + imgW * 0.5f;
					float y1 = cy + imgH * 0.5f;

					glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);

					glEnable(GL_BLEND);
					glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
					glDisable(GL_DEPTH_TEST);

					glUseProgram(m_texViewerChannelProgram);
					glUniformMatrix4fv(m_texViewerChannelUniforms.projection, 1, GL_FALSE, &ortho[0][0]);
					glUniform4f(m_texViewerChannelUniforms.rect, x0, y0, x1, y1);
					glUniform4i(m_texViewerChannelUniforms.channelMask,
						viewer->isChannelR() ? 1 : 0,
						viewer->isChannelG() ? 1 : 0,
						viewer->isChannelB() ? 1 : 0,
						viewer->isChannelA() ? 1 : 0);
					glUniform1i(m_texViewerChannelUniforms.checkerboard, viewer->isCheckerboard() ? 1 : 0);

					glActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, texId);
					glUniform1i(m_texViewerChannelUniforms.texture, 0);

					float vertices[6][2] = {
						{ x0, y1 }, { x0, y0 }, { x1, y0 },
						{ x0, y1 }, { x1, y0 }, { x1, y1 }
					};

					glBindVertexArray(m_uiQuadVao);
					glBindBuffer(GL_ARRAY_BUFFER, m_uiQuadVbo);
					glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
					glBindBuffer(GL_ARRAY_BUFFER, 0);
					glDrawArrays(GL_TRIANGLES, 0, 6);
					glBindVertexArray(0);
					glBindTexture(GL_TEXTURE_2D, 0);

					glBindFramebuffer(GL_FRAMEBUFFER, 0);
				}
			}
			else
			{
				// Multi-viewport: render the world once per sub-viewport
				const int subCount = getSubViewportCount();
				if (subCount > 1 && activeTab->id == "Viewport")
				{
					ensureSubViewportCameras();
					// Sync sub-viewport 0 (Perspective) from the main editor camera
					if (m_camera)
					{
						m_subViewportCameras[0].position = m_camera->getPosition();
						const Vec2 rot = m_camera->getRotationDegrees();
						m_subViewportCameras[0].yawDeg = rot.x;
						m_subViewportCameras[0].pitchDeg = rot.y;
					}

					// Compute sub-viewport pixel rects
					const Vec4 vp = m_cachedViewportContentRect;
					const bool hasVp = (vp.z > 0.0f && vp.w > 0.0f);
					const int vpX = hasVp ? static_cast<int>(vp.x) : 0;
					const int vpY = hasVp ? static_cast<int>(vp.y) : 0;
					const int vpW = hasVp ? static_cast<int>(vp.z) : width;
					const int vpH = hasVp ? static_cast<int>(vp.w) : height;

					SubViewportRect rects[kMaxSubViewports];
					computeSubViewportRects(vpX, vpY, vpW, vpH, rects, subCount);

					for (int sv = 0; sv < subCount; ++sv)
					{
						m_currentSubViewportIndex = sv;
						m_currentSubViewportRect = rects[sv];
						renderWorld();
					}
					m_currentSubViewportIndex = -1;
					renderViewportUI();
				}
				else
				{
					m_currentSubViewportIndex = -1;
					renderWorld();
					if (activeTab->id == "Viewport")
					{
						renderViewportUI();
					}
				}
			}
		}
#else
		// Runtime: render world directly to default framebuffer
		renderWorld();
#endif
	}
	const uint64_t worldEnd = SDL_GetPerformanceCounter();
	m_cpuRenderWorldMs = (freq > 0) ? (static_cast<double>(worldEnd - worldStart) * 1000.0 / static_cast<double>(freq)) : 0.0;

#if ENGINE_EDITOR
	// Composite: blit world content to default framebuffer.
	// Keep source/destination restricted to the viewport content rect so the
	// scene is shown only in the available viewport area (excluding UI docks).
	// First, clear the default framebuffer so non-Viewport tabs (e.g. Widget
	// Editor) that only partially blit don't show undefined back-buffer content.
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, width, height);
	glClearColor(m_clearColor.x, m_clearColor.y, m_clearColor.z, m_clearColor.w);
	glClear(GL_COLOR_BUFFER_BIT);

	if (activeTab && activeTab->renderTarget && activeTab->renderTarget->isValid())
	{
		auto* glRT = static_cast<OpenGLRenderTarget*>(activeTab->renderTarget.get());
		glBindFramebuffer(GL_READ_FRAMEBUFFER, glRT->getGLFramebuffer());
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

		const Vec4 vpRect = m_cachedViewportContentRect;
		const bool hasContentRect = (vpRect.z > 0.0f && vpRect.w > 0.0f);
		if (hasContentRect)
		{
			const int srcX0 = static_cast<int>(vpRect.x);
			const int srcY0 = height - static_cast<int>(vpRect.y) - static_cast<int>(vpRect.w);
			const int srcX1 = srcX0 + static_cast<int>(vpRect.z);
			const int srcY1 = srcY0 + static_cast<int>(vpRect.w);
			glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
							  srcX0, srcY0, srcX1, srcY1,
							  GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		else
		{
			glBlitFramebuffer(0, 0, width, height,
							  0, 0, width, height,
							  GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, width, height);
#endif

	renderUI();
#if ENGINE_EDITOR
	renderPopupWindows();
#endif

	if (m_gpuQueriesInitialized)
	{
		glEndQuery(GL_TIME_ELAPSED);
		const size_t readIndex = (m_gpuQueryIndex + m_gpuTimerQueries.size() - 1) % m_gpuTimerQueries.size();
		GLuint available = 0;
		glGetQueryObjectuiv(m_gpuTimerQueries[readIndex], GL_QUERY_RESULT_AVAILABLE, &available);
		if (available != 0)
		{
			GLuint64 elapsedNs = 0;
			glGetQueryObjectui64v(m_gpuTimerQueries[readIndex], GL_QUERY_RESULT, &elapsedNs);
			m_lastGpuFrameMs = static_cast<double>(elapsedNs) / 1'000'000.0;
		}
		m_gpuQueryIndex = (m_gpuQueryIndex + 1) % m_gpuTimerQueries.size();
	}
}

void OpenGLRenderer::renderWorld()
{
	if (!m_initialized)
	{
		return;
	}

	// When rendering is frozen (e.g. during level load), skip the world pass
	// entirely so the last frame stays on screen while the UI remains interactive.
	if (m_renderFrozen)
	{
		return;
	}

	m_lastVisibleCount = 0;
	m_lastHiddenCount = 0;
	m_lastTotalCount = 0;

	const Vec2 viewportSize = getViewportSize();
	const int fullWidth = static_cast<int>(viewportSize.x);
	const int fullHeight = static_cast<int>(viewportSize.y);

	// Use the viewport content rect (area not covered by editor panels) for
	// projection and glViewport so resizing the window acts like zoom (showing
	// more or less of the scene) instead of stretching the image.
	// In multi-viewport mode, restrict to the sub-viewport rect instead.
	const Vec4 vpRect = m_cachedViewportContentRect;
	const bool hasContentRect = (vpRect.z > 0.0f && vpRect.w > 0.0f);

	int vpX, vpY, width, height;
	if (m_currentSubViewportIndex >= 0)
	{
		vpX    = m_currentSubViewportRect.x;
		vpY    = m_currentSubViewportRect.y;
		width  = m_currentSubViewportRect.w;
		height = m_currentSubViewportRect.h;
	}
	else
	{
		vpX    = hasContentRect ? static_cast<int>(vpRect.x) : 0;
		vpY    = hasContentRect ? static_cast<int>(vpRect.y) : 0;
		width  = hasContentRect ? static_cast<int>(vpRect.z) : fullWidth;
		height = hasContentRect ? static_cast<int>(vpRect.w) : fullHeight;
	}
	const int viewportY = fullHeight - vpY - height;

	// Render into the active tab's FBO (or HDR FBO when post-processing)
	const bool usePostProcess = m_postProcessEnabled && ensurePostProcessResources();
	{
		// In multi-viewport mode, only bind the FBO for the first sub-viewport;
		// subsequent sub-viewports reuse the already-bound FBO.
		const bool isFirstSubViewport = (m_currentSubViewportIndex <= 0);
#if ENGINE_EDITOR
		EditorTab* activeTab = m_cachedActiveTab;
		if (isFirstSubViewport && activeTab && activeTab->renderTarget && activeTab->renderTarget->isValid())
		{
			if (usePostProcess)
			{
				// Scene renders into the HDR FBO; resolved to tab FBO at end
				m_postProcessStack.resize(fullWidth, fullHeight);

				// When MSAA is selected, ensure the multisampled FBO exists
				const int aaModeInt = static_cast<int>(m_aaMode);
				int msaaSamples = 0;
				if (m_aaMode == AntiAliasingMode::MSAA_2x) msaaSamples = 2;
				else if (m_aaMode == AntiAliasingMode::MSAA_4x) msaaSamples = 4;
				m_postProcessStack.setAntiAliasingMode(aaModeInt);
				if (msaaSamples > 0)
					m_postProcessStack.ensureMsaaFbo(fullWidth, fullHeight, msaaSamples);
				else
					m_postProcessStack.ensureMsaaFbo(fullWidth, fullHeight, 0);

				m_postProcessStack.bindHdrTarget();
			}
			else
			{
				activeTab->renderTarget->bind();
			}
		}
#else
		if (isFirstSubViewport && usePostProcess)
		{
			m_postProcessStack.resize(fullWidth, fullHeight);
			const int aaModeInt = static_cast<int>(m_aaMode);
			int msaaSamples = 0;
			if (m_aaMode == AntiAliasingMode::MSAA_2x) msaaSamples = 2;
			else if (m_aaMode == AntiAliasingMode::MSAA_4x) msaaSamples = 4;
			m_postProcessStack.setAntiAliasingMode(aaModeInt);
			if (msaaSamples > 0)
				m_postProcessStack.ensureMsaaFbo(fullWidth, fullHeight, msaaSamples);
			else
				m_postProcessStack.ensureMsaaFbo(fullWidth, fullHeight, 0);
			m_postProcessStack.bindHdrTarget();
		}
#endif
	}

	// Clear & viewport setup
#if ENGINE_EDITOR
	const bool isActorEditorTab = (m_activeTabId == "ActorEditor");
	const Vec4 clearCol = isActorEditorTab ? Vec4{ 0.12f, 0.12f, 0.14f, 1.0f } : m_clearColor;
#else
	const Vec4& clearCol = m_clearColor;
#endif
	if (m_currentSubViewportIndex <= 0)
	{
		// First sub-viewport (or single mode): clear the full FBO
		glViewport(0, 0, fullWidth, fullHeight);
		glClearColor(clearCol.x, clearCol.y, clearCol.z, clearCol.w);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	// In multi-viewport mode, use glScissor to restrict drawing to this sub-viewport
	if (m_currentSubViewportIndex >= 0)
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(vpX, viewportY, width, height);
		if (m_currentSubViewportIndex > 0)
		{
			// Clear only this sub-viewport area
			glViewport(vpX, viewportY, width, height);
			glClearColor(clearCol.x, clearCol.y, clearCol.z, clearCol.w);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		}
	}
	glViewport(vpX, viewportY, width, height);

	// Update projection matrix with current aspect ratio (default, may be overridden by entity camera)
	float activeFov = 45.0f;
	float activeNear = 0.1f;
	float activeFar = 100.0f;
	if (height > 0 && (width != m_lastProjectionWidth || height != m_lastProjectionHeight))
	{
		float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
		m_projectionMatrix = glm::perspective(
			glm::radians(activeFov),
			aspectRatio,
			activeNear,
			activeFar
		);
		m_lastProjectionWidth = width;
		m_lastProjectionHeight = height;
	}

	#if ENGINE_EDITOR
	// Widget editor tabs render only their UI workspace in the center canvas.
	// The tab framebuffer is still cleared each frame to provide the fill color
	// around the preview area, but no 3D world content is drawn.
	if (m_activeTabId.rfind("WidgetEditor_", 0) == 0)
	{
		return;
	}
#endif

	auto& diagnostics = DiagnosticsManager::Instance();
	EngineLevel* level = diagnostics.getActiveLevelSoft();
	if (!level)
	{
		return;
	}

	if (m_cachedLevel != level)
	{
		m_resourceManager.clearCaches();
		m_renderEntries.clear();
		m_meshEntries.clear();
		diagnostics.setScenePrepared(false);
		m_cachedLevel = level;
		m_textRenderer.reset();
		m_restoreCameraOnPrepare = true;
		// Reset cached skybox path so setSkyboxPath re-loads for the new level
		m_skybox.loadedPath.clear();
		setSkyboxPath(level->getSkyboxPath());
	}

	if (!diagnostics.isScenePrepared())
	{
		if (m_resourceManager.prepareActiveLevel())
		{
			diagnostics.setScenePrepared(true);
		}

		// (Re-)load skybox if the level has one but it is not loaded yet
		const std::string& levelSkybox = level->getSkyboxPath();
		if (!levelSkybox.empty() && (m_skybox.cubemap == 0 || m_skybox.loadedPath != levelSkybox))
		{
			m_skybox.loadedPath.clear();
			setSkyboxPath(levelSkybox);
		}

		auto& ecs = ECS::ECSManager::Instance();
		m_renderEntries.clear();
		const auto renderables = m_resourceManager.buildRenderablesForSchema(ecs.getRenderSchema());
		m_renderEntries.reserve(renderables.size());
		for (const auto& renderable : renderables)
		{
			RenderEntry entry;
			entry.entity = renderable.entity;
			entry.transform = renderable.transform;
			entry.object3D = std::static_pointer_cast<OpenGLObject3D>(renderable.object3D);
			entry.object2D = std::static_pointer_cast<OpenGLObject2D>(renderable.object2D);
			if (!entry.object3D && !entry.object2D)
			{
				continue;
			}

			// Load LOD variants if the entity has a LodComponent
			if (entry.entity != 0)
			{
				if (const auto* lod = ecs.getComponent<ECS::LodComponent>(entry.entity))
				{
					for (const auto& level : lod->levels)
					{
						if (level.meshAssetPath.empty())
							continue;
						const std::string resolvedPath = m_resourceManager.resolveContentPath(level.meshAssetPath);
						auto lodAsset = AssetManager::Instance().getLoadedAssetByPath(resolvedPath);
						if (!lodAsset)
						{
							int id = AssetManager::Instance().loadAsset(resolvedPath, AssetType::Model3D, AssetManager::Sync);
							if (id != 0)
								lodAsset = AssetManager::Instance().getLoadedAssetByID(static_cast<unsigned int>(id));
						}
						if (!lodAsset)
							continue;
						auto lodObj = std::make_shared<OpenGLObject3D>(lodAsset);
						if (lodObj->prepare())
						{
							if (renderable.object3D)
							{
								lodObj->setTextures(renderable.textures);
								lodObj->setShininess(renderable.shininess);
								lodObj->setPbrData(renderable.pbrEnabled, renderable.metallic, renderable.roughness);
							}
							RenderEntry::LodVariant variant;
							variant.object3D = std::move(lodObj);
							variant.maxDistance = level.maxDistance;
							entry.lodLevels.push_back(std::move(variant));
						}
					}
				}
			}

			m_renderEntries.push_back(std::move(entry));
		}

		// Propagate texture streaming manager to all materials
		if (m_textureStreamingEnabled && m_textureStreaming.isInitialized())
		{
			for (auto& entry : m_renderEntries)
			{
				if (entry.object3D)
				{
					if (auto* mat = entry.object3D->getMaterial())
						mat->setTextureStreamingManager(&m_textureStreaming);
					for (auto& lod : entry.lodLevels)
					{
						if (auto glObj = std::dynamic_pointer_cast<OpenGLObject3D>(lod.object3D))
						{
							if (auto* lodMat = glObj->getMaterial())
								lodMat->setTextureStreamingManager(&m_textureStreaming);
						}
					}
				}
			}
		}

		ECS::Schema meshSchema;
		meshSchema.require<ECS::MeshComponent>().require<ECS::TransformComponent>();
		m_meshEntries.clear();

		// Collect entities already covered by the render schema to avoid duplicates
		std::unordered_set<ECS::Entity> renderEntitySet;
		renderEntitySet.reserve(m_renderEntries.size());
		for (const auto& re : m_renderEntries)
		{
			if (re.entity != 0)
				renderEntitySet.insert(re.entity);
		}

		const auto meshRenderables = m_resourceManager.buildRenderablesForSchema(meshSchema, "grid_fragment.glsl");
		m_meshEntries.reserve(meshRenderables.size());
		for (const auto& renderable : meshRenderables)
		{
			// Skip entities already in m_renderEntries (Mesh+Material)
			if (renderable.entity != 0 && renderEntitySet.count(renderable.entity))
			{
				continue;
			}
			RenderEntry entry;
			entry.entity = renderable.entity;
			entry.transform = renderable.transform;
			entry.object3D = std::static_pointer_cast<OpenGLObject3D>(renderable.object3D);
			entry.object2D = std::static_pointer_cast<OpenGLObject2D>(renderable.object2D);
			if (!entry.object3D && !entry.object2D)
			{
				continue;
			}
			m_meshEntries.push_back(std::move(entry));
		}

		// Restore editor camera from the level only on level change (not on every re-prepare)
		if (m_restoreCameraOnPrepare && level && level->hasEditorCamera() && m_camera)
		{
			m_camera->setPosition(level->getEditorCameraPosition());
			const Vec2& rot = level->getEditorCameraRotation();
			m_camera->setRotationDegrees(rot.x, rot.y);
		}
		m_restoreCameraOnPrepare = false;
	}

	// Per-entity refresh for dirty entities (avoids full scene rebuild)
	if (diagnostics.hasDirtyEntities())
	{
		diagnostics.consumeDirtyEntities(m_dirtyEntitiesScratch);
		for (unsigned int entityId : m_dirtyEntitiesScratch)
		{
			refreshEntity(static_cast<ECS::Entity>(entityId));
		}
#if ENGINE_EDITOR
		m_pick.dirty = true;
#endif
	}

	glm::mat4 view(1.0f);
	bool usedEntityCamera = false;

	// Entity camera is only used during PIE (Play In Editor)
	auto& ecs = ECS::ECSManager::Instance();
	const bool pieActive = diagnostics.isPIEActive();
	if (pieActive && m_activeCameraEntity != 0)
	{
		const auto* camComp = ecs.getComponent<ECS::CameraComponent>(static_cast<ECS::Entity>(m_activeCameraEntity));
		const auto* camTransform = ecs.getComponent<ECS::TransformComponent>(static_cast<ECS::Entity>(m_activeCameraEntity));
		if (camComp && camTransform)
		{
			const glm::vec3 pos(camTransform->position[0], camTransform->position[1], camTransform->position[2]);
			const float pitch = glm::radians(camTransform->rotation[0]);
			const float yaw   = glm::radians(camTransform->rotation[1]);
			const glm::vec3 front = glm::normalize(glm::vec3(
				cosf(yaw) * cosf(pitch),
				sinf(pitch),
				sinf(yaw) * cosf(pitch)
			));
			const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
			const glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
			const glm::vec3 up = glm::normalize(glm::cross(right, front));
			view = glm::lookAt(pos, pos + front, up);

			// Override projection with entity camera parameters
			if (height > 0)
			{
				const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
				m_projectionMatrix = glm::perspective(
					glm::radians(camComp->fov),
					aspectRatio,
					camComp->nearClip,
					camComp->farClip
				);
				activeFov = camComp->fov;
				activeNear = camComp->nearClip;
				activeFar = camComp->farClip;
			}
			usedEntityCamera = true;
		}
	}

	// Editor camera (always used outside PIE, or as fallback)
	if (!usedEntityCamera && m_camera)
	{
		// Multi-viewport: use the sub-viewport's dedicated camera
		if (m_currentSubViewportIndex > 0)
		{
			const auto& svCam = m_subViewportCameras[m_currentSubViewportIndex];
			const float yawRad = glm::radians(svCam.yawDeg);
			const float pitchRad = glm::radians(svCam.pitchDeg);
			const glm::vec3 front = glm::normalize(glm::vec3(
				cosf(yawRad) * cosf(pitchRad),
				sinf(pitchRad),
				sinf(yawRad) * cosf(pitchRad)
			));
			const glm::vec3 pos(svCam.position.x, svCam.position.y, svCam.position.z);
			const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
			const glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
			const glm::vec3 up = glm::normalize(glm::cross(right, front));
			view = glm::lookAt(pos, pos + front, up);
		}
		else
		{
			Mat4 engineView = m_camera->getViewMatrixColumnMajor();
			view = glm::make_mat4(engineView.m);
		}
	}
	const glm::mat4 viewProj = m_projectionMatrix * view;
	m_lastViewMatrix = view;
	const auto frustumPlanes = ExtractFrustumPlanes(viewProj);

	// Render skybox before scene (depth write disabled, drawn at depth=1)
	glEnable(GL_DEPTH_TEST);
	renderSkybox(view, m_projectionMatrix);

	const uint64_t ecsStartCounter = SDL_GetPerformanceCounter();
	glm::vec3 lightPosition{ 0.0f, 1.2f, 0.0f };
	glm::vec3 lightColor{ 1.0f, 1.0f, 1.0f };
	float lightIntensity = 1.0f;

	if (!m_lightSchemaInitialized)
	{
		m_lightSchema.require<ECS::LightComponent>().require<ECS::TransformComponent>();
		m_lightSchemaInitialized = true;
	}

	m_sceneLights.clear();
	ecs.getEntitiesMatchingSchema(m_lightSchema, m_lightEntitiesScratch);
	for (const auto lightEntity : m_lightEntitiesScratch)
	{
		const auto* transform = ecs.getComponent<ECS::TransformComponent>(lightEntity);
		const auto* light = ecs.getComponent<ECS::LightComponent>(lightEntity);
		if (!transform || !light)
			continue;

		OpenGLMaterial::LightData ld;
		ld.position = glm::vec3(transform->position[0], transform->position[1], transform->position[2]);
		ld.color = glm::vec3(light->color[0], light->color[1], light->color[2]);
		ld.intensity = light->intensity;
		ld.range = light->range;

		// Compute forward direction from rotation (pitch=X, yaw=Y)
		const float pitch = glm::radians(transform->rotation[0]);
		const float yaw = glm::radians(transform->rotation[1]);
		ld.direction = glm::normalize(glm::vec3(
			std::cos(pitch) * std::sin(yaw),
			-std::sin(pitch),
			std::cos(pitch) * std::cos(yaw)
		));

		switch (light->type)
		{
		case ECS::LightComponent::LightType::Point:
			ld.type = 0;
			break;
		case ECS::LightComponent::LightType::Directional:
			ld.type = 1;
			break;
		case ECS::LightComponent::LightType::Spot:
			ld.type = 2;
			ld.spotCutoff = std::cos(glm::radians(light->spotAngle));
			ld.spotOuterCutoff = std::cos(glm::radians(light->spotAngle + 5.0f));
			break;
		}

		m_sceneLights.push_back(ld);

		if (m_sceneLights.size() >= OpenGLMaterial::kMaxLights)
			break;
	}

	// Legacy fallback: use first point light position
	if (!m_sceneLights.empty())
	{
		lightPosition = m_sceneLights[0].position;
		lightColor = m_sceneLights[0].color;
		lightIntensity = m_sceneLights[0].intensity;
	}
	const uint64_t ecsEndCounter = SDL_GetPerformanceCounter();
	const uint64_t freq = SDL_GetPerformanceFrequency();
	m_cpuEcsMs = (freq > 0) ? (static_cast<double>(ecsEndCounter - ecsStartCounter) * 1000.0 / static_cast<double>(freq)) : 0.0;

	// Pre-compute model matrices for all entry lists
	const auto updateModelMatrices = [&ecs](std::vector<RenderEntry>& entries)
	{
		for (auto& entry : entries)
		{
			if (entry.entity != 0)
			{
				if (const auto* transform = ecs.getComponent<ECS::TransformComponent>(entry.entity))
				{
					entry.transform = *transform;
				}
			}
			auto& m = entry.cachedModelMatrix;
			m = glm::mat4(1.0f);
			m = glm::translate(m, glm::vec3(entry.transform.position[0], entry.transform.position[1], entry.transform.position[2]));
			m = glm::rotate(m, glm::radians(entry.transform.rotation[0]), glm::vec3(1.0f, 0.0f, 0.0f));
			m = glm::rotate(m, glm::radians(entry.transform.rotation[1]), glm::vec3(0.0f, 1.0f, 0.0f));
			m = glm::rotate(m, glm::radians(entry.transform.rotation[2]), glm::vec3(0.0f, 0.0f, 1.0f));
			m = glm::scale(m, glm::vec3(entry.transform.scale[0], entry.transform.scale[1], entry.transform.scale[2]));
		}
	};
	updateModelMatrices(m_renderEntries);
	updateModelMatrices(m_meshEntries);

	const auto& objs = level->getWorldObjects();
	const auto& groups = level->getGroups();

	#if ENGINE_EDITOR
		// Sync selection state with UIManager (outliner may have changed it)
		const unsigned int uiSelected = m_uiManager.getSelectedEntity();
		if (uiSelected != 0 && !m_pickRequested)
		{
			if (m_selectedEntities.empty() || *m_selectedEntities.begin() != uiSelected)
			{
				m_selectedEntities.clear();
				m_selectedEntities.insert(uiSelected);
			}
		}
	#endif // ENGINE_EDITOR

		// Collect all visible 3D objects
	m_drawList.clear();
	m_shadowCasterList.clear();
	m_drawList.reserve(m_renderEntries.size() + objs.size() + m_meshEntries.size() + 16);
	m_shadowCasterList.reserve(m_renderEntries.size() + objs.size() + m_meshEntries.size() + 16);

	// Camera position for LOD distance calculations
	const glm::mat4 invView = glm::inverse(view);
	const glm::vec3 cameraPos(invView[3][0], invView[3][1], invView[3][2]);

	const float timeRotation = static_cast<float>(SDL_GetTicks()) / 1000.0f;

	// Gather ECS render entries
	for (auto& entry : m_renderEntries)
	{
		if (entry.object3D)
		{
			++m_lastTotalCount;

			// LOD selection: pick the appropriate mesh variant based on camera distance
			OpenGLObject3D* selectedObj = entry.object3D.get();
			if (!entry.lodLevels.empty())
			{
				const glm::vec3 objPos(entry.cachedModelMatrix[3]);
				const float dist = glm::length(cameraPos - objPos);
				for (const auto& variant : entry.lodLevels)
				{
					if (variant.maxDistance > 0.0f && dist <= variant.maxDistance && variant.object3D)
					{
						selectedObj = variant.object3D.get();
						break;
					}
				}
				// If no level matched (distance beyond all thresholds), use the last level (fallback)
				if (selectedObj == entry.object3D.get() && !entry.lodLevels.empty())
				{
					const auto& last = entry.lodLevels.back();
					if (last.object3D && (last.maxDistance <= 0.0f))
						selectedObj = last.object3D.get();
				}
			}

			DrawCmd cmd;
					cmd.obj = selectedObj;
					cmd.material = selectedObj->getMaterial();
					cmd.modelMatrix = entry.cachedModelMatrix;
					cmd.entityId = static_cast<unsigned int>(entry.entity);
					cmd.isSkinned = selectedObj->isSkinned();
					// Read per-entity material overrides from ECS (pointer, no copy)
					if (entry.entity != 0)
					{
						if (const auto* matComp = ecs.getComponent<ECS::MaterialComponent>(entry.entity))
						{
							cmd.overrides = &matComp->overrides;
						}
					}
					bool isLight = false;
			if (entry.entity != 0)
			{
				if (const auto* light = ecs.getComponent<ECS::LightComponent>(entry.entity))
				{
					cmd.emissionColor = glm::vec3(light->color[0], light->color[1], light->color[2]);
					cmd.hasEmission = true;
					isLight = true;
				}
			}
			// Shadow caster: include all non-light objects regardless of camera frustum
			if (!isLight)
			{
				m_shadowCasterList.push_back(cmd);
			}
			if (entry.object3D->hasLocalBounds())
			{
				ComputeWorldAabb(entry.object3D->localBoundsMinGLM(), entry.object3D->localBoundsMaxGLM(), cmd.modelMatrix, cmd.boundsMin, cmd.boundsMax);
				cmd.hasBounds = true;
				if (!IsAabbInsideFrustum(frustumPlanes, cmd.boundsMin, cmd.boundsMax))
				{
					++m_lastHiddenCount;
					continue;
				}
				if (m_occlusionEnabled && !testAabbAgainstHzb(cmd.boundsMin, cmd.boundsMax, viewProj))
				{
					++m_lastHiddenCount;
					continue;
				}
			}
			cmd.program = entry.object3D->getProgram();
			++m_lastVisibleCount;
			m_drawList.push_back(std::move(cmd));
		}
		else if (entry.object2D)
		{
			entry.object2D->setMatrices(entry.cachedModelMatrix, view, m_projectionMatrix);
			entry.object2D->render();
		}
	}

	// Gather world objects
	for (const auto& entry : objs)
	{
		if (!entry || !entry->object || entry->object->getPath().empty())
			continue;

		const AssetType objType = entry->object->getAssetType();
		auto asset = std::static_pointer_cast<AssetData>(entry->object);
		if (!asset)
			continue;

		if (objType == AssetType::Model3D || objType == AssetType::PointLight)
		{
			const auto& glObj3D = m_resourceManager.getOrCreateObject3D(asset, {});
			if (!glObj3D)
				continue;
			auto* glObj = static_cast<OpenGLObject3D*>(glObj3D.get());

			++m_lastTotalCount;
			const bool skipOcclusion = (objType == AssetType::PointLight);

			DrawCmd cmd;
			cmd.obj = glObj;
			cmd.material = glObj->getMaterial();
			Mat4 engineMat = entry->transform.getMatrix4ColumnMajor();
			cmd.modelMatrix = glm::make_mat4(engineMat.m);
			cmd.modelMatrix = glm::rotate(cmd.modelMatrix, timeRotation, glm::vec3(0.0f, 1.0f, 0.0f));

			// Shadow caster: include non-light objects regardless of camera frustum
			if (objType != AssetType::PointLight)
			{
				m_shadowCasterList.push_back(cmd);
			}

			if (glObj->hasLocalBounds())
			{
				ComputeWorldAabb(glObj->localBoundsMinGLM(), glObj->localBoundsMaxGLM(), cmd.modelMatrix, cmd.boundsMin, cmd.boundsMax);
				cmd.hasBounds = true;
				if (!IsAabbInsideFrustum(frustumPlanes, cmd.boundsMin, cmd.boundsMax))
				{
					++m_lastHiddenCount;
					continue;
				}
				if (!skipOcclusion && m_occlusionEnabled && !testAabbAgainstHzb(cmd.boundsMin, cmd.boundsMax, viewProj))
				{
					++m_lastHiddenCount;
					continue;
				}
			}

			cmd.program = glObj->getProgram();
				++m_lastVisibleCount;
				m_drawList.push_back(std::move(cmd));
			}
			else if (objType == AssetType::Model2D)
			{
				const auto& glObj2D = m_resourceManager.getOrCreateObject2D(asset, {});
				if (!glObj2D)
					continue;
				auto* glObj = static_cast<OpenGLObject2D*>(glObj2D.get());

				Mat4 engineMat = entry->transform.getMatrix4ColumnMajor();
			glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
			modelMatrix = glm::rotate(modelMatrix, timeRotation, glm::vec3(0.0f, 0.0f, 1.0f));
			glObj->setMatrices(modelMatrix, view, m_projectionMatrix);
			glObj->render();
		}
	}

	// Gather groups
	for (const auto& groupEntry : groups)
	{
		const auto& groupObjects = groupEntry.objects;
		for (size_t i = 0; i < groupObjects.size(); ++i)
		{
			const auto& entry = groupObjects[i];
			if (!entry || entry->getPath().empty())
				continue;
			Transform* t = (i < groupEntry.transforms.size()) ? const_cast<Transform*>(&groupEntry.transforms[i]) : nullptr;
			const AssetType groupObjType = entry->getAssetType();
			auto asset = std::static_pointer_cast<AssetData>(entry);
			if (!asset)
				continue;

			if (groupObjType == AssetType::Model3D)
			{
				const auto& glObj3D = m_resourceManager.getOrCreateObject3D(asset, {});
				if (!glObj3D || !t)
					continue;
				auto* glObj = static_cast<OpenGLObject3D*>(glObj3D.get());

				++m_lastTotalCount;

				DrawCmd cmd;
				cmd.obj = glObj;
				cmd.material = glObj->getMaterial();
				Mat4 engineMat = t->getMatrix4ColumnMajor();
				cmd.modelMatrix = glm::make_mat4(engineMat.m);

				m_shadowCasterList.push_back(cmd);

				if (glObj->hasLocalBounds())
				{
					ComputeWorldAabb(glObj->localBoundsMinGLM(), glObj->localBoundsMaxGLM(), cmd.modelMatrix, cmd.boundsMin, cmd.boundsMax);
					cmd.hasBounds = true;
					if (!IsAabbInsideFrustum(frustumPlanes, cmd.boundsMin, cmd.boundsMax))
					{
						++m_lastHiddenCount;
						continue;
					}
					if (m_occlusionEnabled && !testAabbAgainstHzb(cmd.boundsMin, cmd.boundsMax, viewProj))
					{
						++m_lastHiddenCount;
						continue;
					}
				}

				cmd.program = glObj->getProgram();
				++m_lastVisibleCount;
				m_drawList.push_back(std::move(cmd));
			}
			else if (groupObjType == AssetType::Model2D)
			{
				const auto& glObj2D = m_resourceManager.getOrCreateObject2D(asset, {});
				if (!glObj2D || !t)
					continue;
				auto* glObj = static_cast<OpenGLObject2D*>(glObj2D.get());

				Mat4 engineMat = t->getMatrix4ColumnMajor();
				glm::mat4 modelMatrix = glm::make_mat4(engineMat.m);
				glObj->setMatrices(modelMatrix, view, m_projectionMatrix);
				glObj->render();
			}
		}
	}

	// Gather mesh entries
	for (auto& entry : m_meshEntries)
	{
		if (entry.object3D)
		{
			++m_lastTotalCount;
			DrawCmd cmd;
			cmd.obj = entry.object3D.get();
			cmd.material = entry.object3D->getMaterial();
			cmd.modelMatrix = entry.cachedModelMatrix;
			cmd.entityId = static_cast<unsigned int>(entry.entity);
			bool isLight = false;
			if (entry.entity != 0)
			{
				if (const auto* light = ecs.getComponent<ECS::LightComponent>(entry.entity))
				{
					cmd.emissionColor = glm::vec3(light->color[0], light->color[1], light->color[2]);
					cmd.hasEmission = true;
					isLight = true;
				}
			}
			// Shadow caster: include all non-light objects regardless of camera frustum
			if (!isLight)
			{
				m_shadowCasterList.push_back(cmd);
			}
			if (entry.object3D->hasLocalBounds())
			{
				ComputeWorldAabb(entry.object3D->localBoundsMinGLM(), entry.object3D->localBoundsMaxGLM(), cmd.modelMatrix, cmd.boundsMin, cmd.boundsMax);
				cmd.hasBounds = true;
				if (!IsAabbInsideFrustum(frustumPlanes, cmd.boundsMin, cmd.boundsMax))
				{
					++m_lastHiddenCount;
					continue;
				}
				if (m_occlusionEnabled && !testAabbAgainstHzb(cmd.boundsMin, cmd.boundsMax, viewProj))
				{
					++m_lastHiddenCount;
					continue;
				}
			}
			cmd.program = entry.object3D->getProgram();
			++m_lastVisibleCount;
			m_drawList.push_back(std::move(cmd));
		}
		else if (entry.object2D)
		{
			entry.object2D->setMatrices(entry.cachedModelMatrix, view, m_projectionMatrix);
			entry.object2D->render();
		}
	}

	// Sort by (material, obj) so only objects with same mesh AND material are batched
	std::sort(m_drawList.begin(), m_drawList.end(),
		[](const DrawCmd& a, const DrawCmd& b)
		{
			if (a.material != b.material) return a.material < b.material;
			return a.obj < b.obj;
		});

	// Sort shadow caster list by (material, obj) for instanced shadow rendering
	std::sort(m_shadowCasterList.begin(), m_shadowCasterList.end(),
		[](const DrawCmd& a, const DrawCmd& b)
		{
			if (a.material != b.material) return a.material < b.material;
			return a.obj < b.obj;
		});

	// Partition draw list into opaque and transparent for OIT
	m_transparentDrawList.clear();
	if (m_oit.enabled)
	{
		// Mark transparent objects based on material flag
		for (auto& cmd : m_drawList)
		{
			if (cmd.material && cmd.material->isTransparent())
				cmd.isTransparent = true;
		}
		// Move transparent objects to separate list
		auto partIt = std::stable_partition(m_drawList.begin(), m_drawList.end(),
			[](const DrawCmd& cmd) { return !cmd.isTransparent; });
		m_transparentDrawList.assign(std::make_move_iterator(partIt), std::make_move_iterator(m_drawList.end()));
		m_drawList.erase(partIt, m_drawList.end());

		// Sort transparent list by (material, obj) for instanced batching
		std::sort(m_transparentDrawList.begin(), m_transparentDrawList.end(),
			[](const DrawCmd& a, const DrawCmd& b)
			{
				if (a.material != b.material) return a.material < b.material;
				return a.obj < b.obj;
			});
	}

	// ---- Shadow map pass (multi-light) ----
	const int debugMode = static_cast<int>(m_debugRenderMode);
	const bool debugNeedsShadows = (debugMode == 0 || debugMode == 3 || debugMode == 4); // Lit, ShadowMap, ShadowCascades
	m_shadow.count = 0;
	m_csm.enabled = false;
	m_csm.lightIndex = -1;
	if (m_shadow.enabled && debugNeedsShadows && ensureShadowResources())
	{
		findShadowLightIndices();
		for (int s = 0; s < m_shadow.count; ++s)
		{
			m_shadow.lightSpaceMatrices[s] = computeLightSpaceMatrix(m_sceneLights[m_shadow.lightIndices[s]]);
		}
		if (m_shadow.count > 0)
		{
			renderShadowMap(m_shadowCasterList);

			// Restore main viewport
			glViewport(vpX, viewportY, width, height);
		}
	}

	// ---- Cascaded Shadow Maps (first directional light) ----
	if (m_shadow.enabled && debugNeedsShadows && m_csm.userEnabled && m_csm.lightIndex >= 0 && ensureCsmResources())
	{
		computeCsmMatrices(m_sceneLights[m_csm.lightIndex], view, m_projectionMatrix, activeNear, activeFar);
		renderCsmShadowMaps(m_shadowCasterList);
		m_csm.enabled = true;

		// Restore main viewport
		glViewport(vpX, viewportY, width, height);
	}

	// ---- Point light shadow map pass (cube maps) ----
	m_pointShadow.count = 0;
	if (m_shadow.enabled && debugNeedsShadows && ensurePointShadowResources())
	{
		findPointShadowLightIndices();
		if (m_pointShadow.count > 0)
		{
			renderPointShadowMaps(m_shadowCasterList);

			// Restore main viewport
			glViewport(vpX, viewportY, width, height);
		}
	}

	// Render sorted draw list (instanced batching by material)
	const bool debugWireframe = (m_debugRenderMode == DebugRenderMode::Wireframe);
	if (m_wireframeEnabled || debugWireframe)
	{
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	}
	// Overdraw mode: additive blending
	if (m_debugRenderMode == DebugRenderMode::Overdraw)
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		glDepthFunc(GL_ALWAYS);
	}

	// Tick skeletal animators
	{
		const uint64_t now = SDL_GetPerformanceCounter();
		const uint64_t freq = SDL_GetPerformanceFrequency();
		float animDt = 0.0f;
		if (m_lastAnimTickCounter > 0 && freq > 0)
			animDt = static_cast<float>(static_cast<double>(now - m_lastAnimTickCounter) / static_cast<double>(freq));
		m_lastAnimTickCounter = now;
		if (animDt > 0.0f && animDt < 1.0f)
		{
			for (auto& [entityId, animator] : m_entityAnimators)
			{
				animator->tick(animDt);
			}
		}
	}

	const glm::vec3 fogColor(m_fogColor.x, m_fogColor.y, m_fogColor.z);

	// Displacement mapping: propagate global setting to all materials in draw list
	const bool dispEnabled = m_displacementEnabled && ensureDisplacementResources();
	if (dispEnabled)
	{
		for (auto& cmd : m_drawList)
		{
			if (cmd.material)
			{
				cmd.material->setDisplacementEnabled(true);
				cmd.material->setDisplacementScale(m_displacementScale);
				cmd.material->setTessellationLevel(m_tessLevel);
			}
		}
	}

	size_t batchIndex = 0; // for InstanceGroups coloring
	size_t di = 0;
	while (di < m_drawList.size())
	{
		const auto& first = m_drawList[di];

		// Compute per-batch debug color for InstanceGroups mode
		glm::vec3 batchDebugColor(1.0f);
		if (m_debugRenderMode == DebugRenderMode::InstanceGroups)
		{
			// Deterministic hue from batch index
			const float hue = static_cast<float>(batchIndex % 12) / 12.0f;
			const float h6 = hue * 6.0f;
			const int hi = static_cast<int>(h6);
			const float f = h6 - static_cast<float>(hi);
			switch (hi % 6) {
				case 0: batchDebugColor = glm::vec3(1.0f, f, 0.0f); break;
				case 1: batchDebugColor = glm::vec3(1.0f - f, 1.0f, 0.0f); break;
				case 2: batchDebugColor = glm::vec3(0.0f, 1.0f, f); break;
				case 3: batchDebugColor = glm::vec3(0.0f, 1.0f - f, 1.0f); break;
				case 4: batchDebugColor = glm::vec3(f, 0.0f, 1.0f); break;
				case 5: batchDebugColor = glm::vec3(1.0f, 0.0f, 1.0f - f); break;
			}
		}

		// Emission objects always draw individually (per-object light override)
		if (first.hasEmission)
		{
			first.obj->setMatrices(first.modelMatrix, view, m_projectionMatrix);
			first.obj->setShadowData(m_shadow.depthArray, m_shadow.lightSpaceMatrices, m_shadow.lightIndices, m_shadow.count);
			first.obj->setPointShadowData(m_pointShadow.cubeArray, m_pointShadow.positions, m_pointShadow.farPlanes, m_pointShadow.lightIndices, m_pointShadow.count);
			first.obj->setFogData(m_fogEnabled, fogColor, m_fogParams.z);
			first.obj->setCsmData(m_csm.depthArray, m_csm.matrices, m_csm.splits, m_csm.lightIndex, m_csm.enabled, view);
			first.obj->setLightData(lightPosition, first.emissionColor, lightIntensity);
			first.obj->setLights(m_sceneLights);
			first.obj->setDebugMode(debugMode);
			first.obj->setDebugColor(batchDebugColor);
			first.obj->setNearFarPlanes(activeNear, activeFar);
			// Skinned emission objects: upload bone matrices
			if (first.isSkinned)
			{
				auto ait = m_entityAnimators.find(first.entityId);
				if (ait != m_entityAnimators.end())
				{
					const auto& bones = ait->second->getBoneMatrices();
					first.obj->setSkinned(true);
					first.obj->setBoneMatrices(bones.empty() ? nullptr : bones[0].m, static_cast<int>(bones.size()));
				}
			}
			first.obj->render();
			if (m_boundsDebug.enabled && first.hasBounds)
			{
				const glm::vec3 center = (first.boundsMin + first.boundsMax) * 0.5f;
				const glm::vec3 extent = (first.boundsMax - first.boundsMin) * 0.5f;
				drawBoundsDebugBox(center, extent, viewProj);
			}
			++di;
			++batchIndex;
			continue;
		}

		// Skinned objects must be drawn individually (each entity has unique bone pose)
		if (first.isSkinned)
		{
			first.obj->setMatrices(first.modelMatrix, view, m_projectionMatrix);
			first.obj->setShadowData(m_shadow.depthArray, m_shadow.lightSpaceMatrices, m_shadow.lightIndices, m_shadow.count);
			first.obj->setPointShadowData(m_pointShadow.cubeArray, m_pointShadow.positions, m_pointShadow.farPlanes, m_pointShadow.lightIndices, m_pointShadow.count);
			first.obj->setFogData(m_fogEnabled, fogColor, m_fogParams.z);
			first.obj->setCsmData(m_csm.depthArray, m_csm.matrices, m_csm.splits, m_csm.lightIndex, m_csm.enabled, view);
			first.obj->setLightData(lightPosition, lightColor, lightIntensity);
			first.obj->setLights(m_sceneLights);
			first.obj->setDebugMode(debugMode);
			first.obj->setDebugColor(batchDebugColor);
			first.obj->setNearFarPlanes(activeNear, activeFar);
			// Upload bone matrices for this entity
			auto ait = m_entityAnimators.find(first.entityId);
			if (ait != m_entityAnimators.end())
			{
				const auto& bones = ait->second->getBoneMatrices();
				first.obj->setSkinned(true);
				first.obj->setBoneMatrices(bones.empty() ? nullptr : bones[0].m, static_cast<int>(bones.size()));
			}
			if (first.overrides && first.overrides->hasAnyOverride() && first.material)
			{
				if (first.overrides->hasColorTint)
					first.material->setColorTint(glm::vec3(first.overrides->colorTint[0], first.overrides->colorTint[1], first.overrides->colorTint[2]));
				if (first.overrides->hasMetallic)
					first.material->setOverrideMetallic(first.overrides->metallic);
				if (first.overrides->hasRoughness)
					first.material->setOverrideRoughness(first.overrides->roughness);
				if (first.overrides->hasShininess)
					first.material->setOverrideShininess(first.overrides->shininess);
				if (first.overrides->hasSpecularMultiplier)
					first.material->setOverrideSpecularMultiplier(first.overrides->specularMultiplier);
			}
			first.obj->render();
			// Restore defaults after override draw
			if (first.overrides && first.overrides->hasAnyOverride() && first.material)
			{
				first.material->setColorTint(glm::vec3(1.0f));
			}
			if (m_boundsDebug.enabled && first.hasBounds)
			{
				const glm::vec3 center = (first.boundsMin + first.boundsMax) * 0.5f;
				const glm::vec3 extent = (first.boundsMax - first.boundsMin) * 0.5f;
				drawBoundsDebugBox(center, extent, viewProj);
			}
			++di;
			++batchIndex;
			continue;
		}

		// Material override objects: draw individually (per-entity uniforms)
		if (first.overrides && first.overrides->hasAnyOverride())
		{
			first.obj->setMatrices(first.modelMatrix, view, m_projectionMatrix);
			first.obj->setShadowData(m_shadow.depthArray, m_shadow.lightSpaceMatrices, m_shadow.lightIndices, m_shadow.count);
			first.obj->setPointShadowData(m_pointShadow.cubeArray, m_pointShadow.positions, m_pointShadow.farPlanes, m_pointShadow.lightIndices, m_pointShadow.count);
			first.obj->setFogData(m_fogEnabled, fogColor, m_fogParams.z);
			first.obj->setCsmData(m_csm.depthArray, m_csm.matrices, m_csm.splits, m_csm.lightIndex, m_csm.enabled, view);
			first.obj->setLightData(lightPosition, lightColor, lightIntensity);
			first.obj->setLights(m_sceneLights);
			first.obj->setDebugMode(debugMode);
			first.obj->setDebugColor(batchDebugColor);
			first.obj->setNearFarPlanes(activeNear, activeFar);
			if (first.material)
			{
				if (first.overrides->hasColorTint)
					first.material->setColorTint(glm::vec3(first.overrides->colorTint[0], first.overrides->colorTint[1], first.overrides->colorTint[2]));
				if (first.overrides->hasMetallic)
					first.material->setOverrideMetallic(first.overrides->metallic);
				if (first.overrides->hasRoughness)
					first.material->setOverrideRoughness(first.overrides->roughness);
				if (first.overrides->hasShininess)
					first.material->setOverrideShininess(first.overrides->shininess);
				if (first.overrides->hasSpecularMultiplier)
					first.material->setOverrideSpecularMultiplier(first.overrides->specularMultiplier);
			}
			first.obj->render();
			// Restore defaults
			if (first.material)
			{
				first.material->setColorTint(glm::vec3(1.0f));
			}
			if (m_boundsDebug.enabled && first.hasBounds)
			{
				const glm::vec3 center = (first.boundsMin + first.boundsMax) * 0.5f;
				const glm::vec3 extent = (first.boundsMax - first.boundsMin) * 0.5f;
				drawBoundsDebugBox(center, extent, viewProj);
			}
			++di;
			++batchIndex;
			continue;
		}

		// Find batch: same mesh AND material, no emission, no skinning, no overrides
		size_t batchEnd = di + 1;
		while (batchEnd < m_drawList.size() &&
			   m_drawList[batchEnd].obj == first.obj &&
			   m_drawList[batchEnd].material == first.material &&
			   !m_drawList[batchEnd].hasEmission &&
			   !m_drawList[batchEnd].isSkinned &&
			   !(m_drawList[batchEnd].overrides && m_drawList[batchEnd].overrides->hasAnyOverride()))
		{
			++batchEnd;
		}
		const size_t batchSize = batchEnd - di;

		// Set scene-level uniforms via first object in batch
		first.obj->setMatrices(first.modelMatrix, view, m_projectionMatrix);
		first.obj->setShadowData(m_shadow.depthArray, m_shadow.lightSpaceMatrices, m_shadow.lightIndices, m_shadow.count);
		first.obj->setPointShadowData(m_pointShadow.cubeArray, m_pointShadow.positions, m_pointShadow.farPlanes, m_pointShadow.lightIndices, m_pointShadow.count);
		first.obj->setFogData(m_fogEnabled, fogColor, m_fogParams.z);
		first.obj->setCsmData(m_csm.depthArray, m_csm.matrices, m_csm.splits, m_csm.lightIndex, m_csm.enabled, view);
		first.obj->setLightData(lightPosition, lightColor, lightIntensity);
		first.obj->setLights(m_sceneLights);
		first.obj->setDebugMode(debugMode);
		first.obj->setDebugColor(batchDebugColor);
		first.obj->setNearFarPlanes(activeNear, activeFar);

		if (batchSize > 1 && first.material)
		{
			// GPU instanced draw: upload model matrices to SSBO
			m_instanceMatrixBuffer.clear();
			for (size_t j = di; j < batchEnd; ++j)
				m_instanceMatrixBuffer.push_back(m_drawList[j].modelMatrix);
			uploadInstanceData(m_instanceMatrixBuffer.data(), batchSize);
			first.material->renderInstanced(static_cast<int>(batchSize));
		}
		else
		{
			first.obj->render();
		}

		// Debug bounds for all objects in the batch
		if (m_boundsDebug.enabled)
		{
			for (size_t j = di; j < batchEnd; ++j)
			{
				if (m_drawList[j].hasBounds)
				{
					const glm::vec3 center = (m_drawList[j].boundsMin + m_drawList[j].boundsMax) * 0.5f;
					const glm::vec3 extent = (m_drawList[j].boundsMax - m_drawList[j].boundsMin) * 0.5f;
					drawBoundsDebugBox(center, extent, viewProj);
				}
			}
		}

		di = batchEnd;
		++batchIndex;
	}

	// Reset displacement state on materials after main draw pass
	if (dispEnabled)
	{
		for (auto& cmd : m_drawList)
		{
			if (cmd.material)
				cmd.material->setDisplacementEnabled(false);
		}
	}

	if (m_wireframeEnabled || debugWireframe)
	{
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
	if (m_debugRenderMode == DebugRenderMode::Overdraw)
	{
		glDisable(GL_BLEND);
		glDepthFunc(GL_LESS);
	}

	// ---- Particle Pass (after opaque, before OIT) ----
	if (pieActive)
	{
		// Compute frame dt from SDL performance counters
		const uint64_t pFreq = SDL_GetPerformanceFrequency();
		const uint64_t pNow = SDL_GetPerformanceCounter();
		float pDt = 0.0f;
		if (m_lastParticleTick > 0 && pFreq > 0)
			pDt = static_cast<float>(static_cast<double>(pNow - m_lastParticleTick) / static_cast<double>(pFreq));
		m_lastParticleTick = pNow;
		if (pDt > 0.0f && pDt < 1.0f)
		{
			m_particleSystem.update(pDt);
		}
		// Extract camera world position from view matrix
		const glm::mat4 invView = glm::inverse(view);
		const glm::vec3 camWorldPos(invView[3][0], invView[3][1], invView[3][2]);
		glEnable(GL_PROGRAM_POINT_SIZE);
		m_particleSystem.render(view, m_projectionMatrix, camWorldPos);
		glDisable(GL_PROGRAM_POINT_SIZE);
	}
	else
	{
		m_lastParticleTick = 0;
		m_particleSystem.clear();
	}

	// ---- OIT Transparent Pass ----
	if (m_oit.enabled && !m_transparentDrawList.empty() && ensureOitResources(fullWidth, fullHeight))
	{
		// Blit depth from the current render target (HDR or tab FBO) into the OIT FBO
		// so transparent objects are correctly depth-tested against opaque geometry.
		GLuint srcFbo = 0;
		if (usePostProcess && m_postProcessStack.isInitialized())
			srcFbo = m_postProcessStack.getHdrFbo();
#if ENGINE_EDITOR
		else if (m_cachedActiveTab && m_cachedActiveTab->renderTarget && m_cachedActiveTab->renderTarget->isValid())
			srcFbo = static_cast<OpenGLRenderTarget*>(m_cachedActiveTab->renderTarget.get())->getGLFramebuffer();
#endif

		glBindFramebuffer(GL_READ_FRAMEBUFFER, srcFbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_oit.fbo);
		glBlitFramebuffer(0, 0, fullWidth, fullHeight, 0, 0, fullWidth, fullHeight,
			GL_DEPTH_BUFFER_BIT, GL_NEAREST);

		glViewport(vpX, viewportY, width, height);

		renderOitTransparentPass(view, lightPosition, lightColor, lightIntensity,
			fogColor, debugMode, activeNear, activeFar);

		// Composite OIT result over the scene in the tab/HDR FBO
		GLuint dstFbo = srcFbo;
		compositeOit(dstFbo, vpX, viewportY, width, height);
	}

	// HeightField debug wireframe overlay
	renderHeightFieldDebug(viewProj);

	// Build HZB from current frame's depth buffer for next frame's occlusion tests
	// Skip HZB when scene has few objects (overhead exceeds benefit)
	static constexpr uint32_t kHzbMinObjects = 20;
	if (m_occlusionEnabled && m_lastTotalCount >= kHzbMinObjects)
	{
		buildHzb();
	}

	// Pick pass: full scene render only on actual click; single-entity for outline
#if ENGINE_EDITOR
	if (m_pickRequested)
	{
		if (ensurePickFbo(fullWidth, fullHeight))
		{
			renderPickBuffer(view, m_projectionMatrix);
		}
		m_pickRequested = false;
		if (!diagnostics.isPIEActive())
		{
			const unsigned int picked = pickEntityAt(m_pickX, m_pickY);
			if (m_pickCtrlHeld)
			{
				// Ctrl+click: toggle entity in selection set
				if (picked != 0)
				{
					if (m_selectedEntities.count(picked))
						m_selectedEntities.erase(picked);
					else
						m_selectedEntities.insert(picked);
				}
			}
			else
			{
				// Normal click: replace selection
				m_selectedEntities.clear();
				if (picked != 0)
					m_selectedEntities.insert(picked);
			}
#if ENGINE_EDITOR
			m_uiManager.selectEntity(m_selectedEntities.empty() ? 0u : *m_selectedEntities.begin());
#endif // ENGINE_EDITOR
					m_pickCtrlHeld = false;
					}
				}
			#endif // ENGINE_EDITOR — pick handling

#if ENGINE_EDITOR
				// Render selected entity into pick buffer (needed for outline + picking before resolve)
	if (!m_selectedEntities.empty() && !diagnostics.isPIEActive())
	{
		if (ensurePickFbo(fullWidth, fullHeight))
		{
			renderPickBufferSelectedEntities(view, m_projectionMatrix, m_selectedEntities);
		}

		// Update gizmo hover highlight
		if (!m_gizmo.dragging)
		{
			const Vec2 mousePos = m_uiManager.getMousePosition();
			m_gizmo.hoveredAxis = pickGizmoAxis(view, m_projectionMatrix,
				static_cast<int>(mousePos.x), static_cast<int>(mousePos.y));
		}
	}
#endif // ENGINE_EDITOR

	// Resolve HDR FBO → tab FBO via post-process resolve pass.
	// Use the full FBO dimensions so the fullscreen triangle's 0..1 UV maps
	// 1:1 to the HDR texture pixels (the scene was rendered into a sub-rect,
	// but the clear colour fills the rest, preserving the correct layout).
	// FXAA is deferred to after gizmo/outline so those overlays also get AA.
	if (usePostProcess)
	{
#if ENGINE_EDITOR
		EditorTab* activeTab = m_cachedActiveTab;
		if (activeTab && activeTab->renderTarget && activeTab->renderTarget->isValid())
		{
			auto* glRT = static_cast<OpenGLRenderTarget*>(activeTab->renderTarget.get());
			m_postProcessStack.setGammaEnabled(m_gammaEnabled);
			m_postProcessStack.setToneMappingEnabled(m_toneMappingEnabled);
			m_postProcessStack.setExposure(m_exposure);
			// Defer FXAA to after overlays; MSAA modes pass through for resolve
			const int resolveAaMode = (m_aaMode == AntiAliasingMode::FXAA)
				? 0 : static_cast<int>(m_aaMode);
			m_postProcessStack.setAntiAliasingMode(resolveAaMode);
			m_postProcessStack.setBloomEnabled(m_bloomEnabled);
			m_postProcessStack.setBloomThreshold(m_bloomThreshold);
			m_postProcessStack.setBloomIntensity(m_bloomIntensity);
			m_postProcessStack.setSsaoEnabled(m_ssaoEnabled);
			m_postProcessStack.setProjectionMatrix(m_projectionMatrix);
			m_postProcessStack.execute(glRT->getGLFramebuffer(), 0, 0, fullWidth, fullHeight);

			// Restore content-rect viewport so outline/gizmo draw in the right area
			glViewport(vpX, viewportY, width, height);
		}
#else
		m_postProcessStack.setGammaEnabled(m_gammaEnabled);
		m_postProcessStack.setToneMappingEnabled(m_toneMappingEnabled);
		m_postProcessStack.setExposure(m_exposure);
		const int resolveAaMode = (m_aaMode == AntiAliasingMode::FXAA)
			? 0 : static_cast<int>(m_aaMode);
		m_postProcessStack.setAntiAliasingMode(resolveAaMode);
		m_postProcessStack.setBloomEnabled(m_bloomEnabled);
		m_postProcessStack.setBloomThreshold(m_bloomThreshold);
		m_postProcessStack.setBloomIntensity(m_bloomIntensity);
		m_postProcessStack.setSsaoEnabled(m_ssaoEnabled);
		m_postProcessStack.setProjectionMatrix(m_projectionMatrix);
		m_postProcessStack.execute(0, 0, 0, fullWidth, fullHeight);
		glViewport(vpX, viewportY, width, height);
#endif
	}

	// Draw viewport grid overlay (XZ plane) when not in PIE
#if ENGINE_EDITOR
	if (!diagnostics.isPIEActive())
	{
		// Always show the grid in the actor editor viewport for spatial
		// reference, regardless of the main viewport's snap/grid setting.
		const bool gridWasVisible = m_gridVisible;
		if (isActorEditorTab)
			m_gridVisible = true;
		drawViewportGrid(view, m_projectionMatrix);
		m_gridVisible = gridWasVisible;
	}

	// Draw origin axis lines in actor editor viewport
	if (isActorEditorTab)
	{
		drawOriginAxes(view, m_projectionMatrix);
	}

	// Draw collider wireframe debug overlay when not in PIE
	if (!diagnostics.isPIEActive())
	{
		renderColliderDebug(view, m_projectionMatrix);
	}

	// Draw streaming volume wireframe overlay (Phase 11.4) when not in PIE
	if (!diagnostics.isPIEActive() && m_streamingVolumesVisible)
	{
		renderStreamingVolumeDebug(view, m_projectionMatrix);
	}

	// Draw bone / skeleton debug overlay for selected entities when not in PIE
	if (!diagnostics.isPIEActive())
	{
		renderBoneDebug(view, m_projectionMatrix);
	}
#endif // ENGINE_EDITOR

	#if ENGINE_EDITOR
		// Draw camera path spline in viewport when Sequencer is open and spline visible
		if (!diagnostics.isPIEActive() && m_cameraPath.points.size() >= 2)
		{
			auto& uiMgr = getUIManager();
			if (uiMgr.isSequencerOpen())
		{
			// Evaluate spline as polyline (100 segments)
			const int segCount = 100;
			std::vector<glm::vec3> lineVerts;
			lineVerts.reserve((segCount + 1) * 2);
			for (int s = 0; s <= segCount; ++s)
			{
				float t = static_cast<float>(s) / static_cast<float>(segCount);
				CameraPathPoint pt = m_cameraPath.evaluate(t);
				glm::vec3 v(pt.position.x, pt.position.y, pt.position.z);
				if (s > 0)
				{
					lineVerts.push_back(lineVerts.back());
					lineVerts.push_back(v);
				}
				else
				{
					lineVerts.push_back(v);
				}
			}

			if (lineVerts.size() >= 2)
			{
				glm::mat4 mvp = m_projectionMatrix * view;
				if (m_gizmo.program != 0)
				{
					glUseProgram(m_gizmo.program);
					if (m_gizmo.locMVP >= 0) glUniformMatrix4fv(m_gizmo.locMVP, 1, GL_FALSE, &mvp[0][0]);
					if (m_gizmo.locColor >= 0) glUniform4f(m_gizmo.locColor, 1.0f, 0.6f, 0.1f, 1.0f); // orange spline

					GLuint vao, vbo;
					glGenVertexArrays(1, &vao);
					glGenBuffers(1, &vbo);
					glBindVertexArray(vao);
					glBindBuffer(GL_ARRAY_BUFFER, vbo);
					glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lineVerts.size() * sizeof(glm::vec3)), lineVerts.data(), GL_STREAM_DRAW);
					glEnableVertexAttribArray(0);
					glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);

					glLineWidth(2.0f);
					glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVerts.size()));
					glLineWidth(1.0f);

					// Draw control point markers
					std::vector<glm::vec3> cpVerts;
					for (const auto& cp : m_cameraPath.points)
					{
						cpVerts.push_back(glm::vec3(cp.position.x, cp.position.y, cp.position.z));
					}
					if (!cpVerts.empty())
					{
						if (m_gizmo.locColor >= 0) glUniform4f(m_gizmo.locColor, 1.0f, 1.0f, 0.2f, 1.0f); // yellow points
						glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpVerts.size() * sizeof(glm::vec3)), cpVerts.data(), GL_STREAM_DRAW);
						glPointSize(8.0f);
						glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(cpVerts.size()));
						glPointSize(1.0f);
					}

					glBindVertexArray(0);
					glDeleteBuffers(1, &vbo);
					glDeleteVertexArrays(1, &vao);
				}
			}
		}
	}
#endif // ENGINE_EDITOR

	// Gizmo, selection outline, and rubber-band only in the active sub-viewport
#if ENGINE_EDITOR
	const bool isActiveSubViewport = (m_currentSubViewportIndex < 0 || m_currentSubViewportIndex == m_activeSubViewport);

	// Draw selection outline + gizmo AFTER post-process resolve so they are not overwritten
	if (!m_selectedEntities.empty() && !diagnostics.isPIEActive() && isActiveSubViewport)
	{
		if (m_pick.colorTex != 0)
		{
			drawSelectionOutline();
		}

		// Draw gizmo overlay on top of the scene
		renderGizmo(view, m_projectionMatrix);
	}

	// Draw rubber-band rectangle overlay (screen-space)
	if (m_rubberBandActive && !diagnostics.isPIEActive() && isActiveSubViewport)
	{
		// Rubber-band coords are in full-window SDL space, so use full FBO viewport
		glViewport(0, 0, fullWidth, fullHeight);
		const glm::mat4 rbOrtho = glm::ortho(0.0f, static_cast<float>(fullWidth), static_cast<float>(fullHeight), 0.0f);
		drawRubberBand(rbOrtho);
		glViewport(vpX, viewportY, width, height);
	}

	// Draw active sub-viewport border highlight (thin blue outline)
	if (m_currentSubViewportIndex >= 0 && isActiveSubViewport)
	{
		glViewport(0, 0, fullWidth, fullHeight);
		const glm::mat4 ortho = glm::ortho(0.0f, static_cast<float>(fullWidth),
											static_cast<float>(fullHeight), 0.0f);
		const Vec4 borderColor{ 0.25f, 0.55f, 0.95f, 0.8f };
		const float bx0 = static_cast<float>(vpX);
		const float by0 = static_cast<float>(vpY);
		const float bx1 = bx0 + static_cast<float>(width);
		const float by1 = by0 + static_cast<float>(height);
		if (ensureUIQuadRenderer())
		{
			ensureUIShaderDefaults();
			drawUIOutline(bx0, by0, bx1, by1, borderColor, ortho, m_uiQuadProgram);
		}
		glViewport(vpX, viewportY, width, height);
	}
#endif // ENGINE_EDITOR

	// Draw sub-viewport preset label (Top/Front/Right/Perspective) in corner
#if ENGINE_EDITOR
	if (m_currentSubViewportIndex >= 0 && m_textRenderer)
	{
		const auto& svCam = m_subViewportCameras[m_currentSubViewportIndex];
		const char* presetName = subViewportPresetToString(svCam.preset);
		const float labelX = static_cast<float>(vpX) + 6.0f;
		const float labelY = static_cast<float>(fullHeight - vpY) - 6.0f;
		glViewport(0, 0, fullWidth, fullHeight);
		m_textRenderer->setScreenSize(fullWidth, fullHeight);
		m_textRenderer->drawText(presetName, Vec2{ labelX, labelY }, 0.35f,
			Vec4{ 0.6f, 0.7f, 0.85f, 1.0f });
		glViewport(vpX, viewportY, width, height);
	}
#endif // ENGINE_EDITOR

	// Deferred FXAA pass: apply after overlays (gizmos, outlines) so they also get AA.
	// Use content-rect viewport so FXAA does not process the clear-colour border.
	if (usePostProcess && m_aaMode == AntiAliasingMode::FXAA)
	{
#if ENGINE_EDITOR
		EditorTab* activeTab = m_cachedActiveTab;
		if (activeTab && activeTab->renderTarget && activeTab->renderTarget->isValid())
		{
			auto* glRT = static_cast<OpenGLRenderTarget*>(activeTab->renderTarget.get());
			m_postProcessStack.setAntiAliasingMode(static_cast<int>(m_aaMode));
			m_postProcessStack.executeFxaaPass(glRT->getGLFramebuffer(), vpX, viewportY, width, height);
			glViewport(vpX, viewportY, width, height);
		}
#else
		m_postProcessStack.setAntiAliasingMode(static_cast<int>(m_aaMode));
		m_postProcessStack.executeFxaaPass(0, vpX, viewportY, width, height);
		glViewport(vpX, viewportY, width, height);
#endif
	}

	// Disable scissor test that multi-viewport rendering may have enabled
	if (m_currentSubViewportIndex >= 0)
	{
		glDisable(GL_SCISSOR_TEST);
	}
}

void OpenGLRenderer::refreshEntity(ECS::Entity entity)
{
	if (entity == 0)
		return;

	auto& ecs = ECS::ECSManager::Instance();
	const bool hasMesh = ecs.hasComponent<ECS::MeshComponent>(entity);
	const bool hasMat = ecs.hasComponent<ECS::MaterialComponent>(entity);

	// Helper: try to find and update an existing entry in a list.
	// Returns true if the entity was found (updated or removed).
	auto updateList = [&](std::vector<RenderEntry>& entries, const std::string& fragDefault) -> bool
	{
		for (auto it = entries.begin(); it != entries.end(); ++it)
		{
			if (it->entity != entity)
				continue;

			if (!hasMesh)
			{
				// Entity no longer has a mesh → remove from list
				entries.erase(it);
				return true;
			}

			auto renderable = m_resourceManager.refreshEntityRenderable(entity, fragDefault);
			if (renderable.entity == 0 || (!renderable.object3D && !renderable.object2D))
			{
				entries.erase(it);
				return true;
			}

			it->object3D = std::static_pointer_cast<OpenGLObject3D>(renderable.object3D);
			it->object2D = std::static_pointer_cast<OpenGLObject2D>(renderable.object2D);
			it->transform = renderable.transform;
			return true;
		}
		return false;
	};

	// Try render entries first (entities with Mesh+Material)
	if (updateList(m_renderEntries, ""))
	{
		// If entity was removed from renderEntries but still has a mesh (without material),
		// it might need to go into meshEntries.
		if (hasMesh && !hasMat)
		{
			// Check if already in meshEntries
			bool inMesh = false;
			for (const auto& e : m_meshEntries)
			{
				if (e.entity == entity) { inMesh = true; break; }
			}
			if (!inMesh)
			{
				auto renderable = m_resourceManager.refreshEntityRenderable(entity, "grid_fragment.glsl");
				if (renderable.entity != 0 && (renderable.object3D || renderable.object2D))
				{
					RenderEntry entry;
					entry.entity = renderable.entity;
					entry.transform = renderable.transform;
					entry.object3D = std::static_pointer_cast<OpenGLObject3D>(renderable.object3D);
					entry.object2D = std::static_pointer_cast<OpenGLObject2D>(renderable.object2D);
					m_meshEntries.push_back(std::move(entry));
				}
			}
		}
		return;
	}

	// Try mesh-only entries
	if (updateList(m_meshEntries, "grid_fragment.glsl"))
	{
		// If entity gained a material, it should move to renderEntries
		if (hasMesh && hasMat)
		{
			bool inRender = false;
			for (const auto& e : m_renderEntries)
			{
				if (e.entity == entity) { inRender = true; break; }
			}
			if (!inRender)
			{
				auto renderable = m_resourceManager.refreshEntityRenderable(entity, "");
				if (renderable.entity != 0 && (renderable.object3D || renderable.object2D))
				{
					RenderEntry entry;
					entry.entity = renderable.entity;
					entry.transform = renderable.transform;
					entry.object3D = std::static_pointer_cast<OpenGLObject3D>(renderable.object3D);
					entry.object2D = std::static_pointer_cast<OpenGLObject2D>(renderable.object2D);
					m_renderEntries.push_back(std::move(entry));
				}
			}
		}
		return;
	}

	// Entity wasn't in either list — it's new. Add if it has a mesh.
	if (hasMesh)
	{
		const std::string fragDefault = hasMat ? "" : "grid_fragment.glsl";
		auto renderable = m_resourceManager.refreshEntityRenderable(entity, fragDefault);
		if (renderable.entity != 0 && (renderable.object3D || renderable.object2D))
		{
			RenderEntry entry;
			entry.entity = renderable.entity;
			entry.transform = renderable.transform;
			entry.object3D = std::static_pointer_cast<OpenGLObject3D>(renderable.object3D);
			entry.object2D = std::static_pointer_cast<OpenGLObject2D>(renderable.object2D);
			if (hasMat)
				m_renderEntries.push_back(std::move(entry));
			else
				m_meshEntries.push_back(std::move(entry));
		}
	}
}

bool OpenGLRenderer::ensureUiFbo(int width, int height)
{
	if (m_uiFbo != 0 && m_uiFboWidth == width && m_uiFboHeight == height)
	{
		return true;
	}

	releaseUiFbo();

	m_uiFboWidth = width;
	m_uiFboHeight = height;

	glGenFramebuffers(1, &m_uiFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, m_uiFbo);

	glGenTextures(1, &m_uiFboTexture);
	glBindTexture(GL_TEXTURE_2D, m_uiFboTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_uiFboTexture, 0);

	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		releaseUiFbo();
		return false;
	}

	m_uiFboCacheValid = false;
	return true;
}

void OpenGLRenderer::releaseUiFbo()
{
	if (m_uiFboTexture)
	{
		glDeleteTextures(1, &m_uiFboTexture);
		m_uiFboTexture = 0;
	}
	if (m_uiFbo)
	{
		glDeleteFramebuffers(1, &m_uiFbo);
		m_uiFbo = 0;
	}
	m_uiFboWidth = 0;
	m_uiFboHeight = 0;
	m_uiFboCacheValid = false;
}

bool OpenGLRenderer::ensurePostProcessResources()
{
	if (m_postProcessStack.isInitialized())
		return true;

	const std::filesystem::path shadersDir = std::filesystem::current_path() / "shaders";
	const std::string vertPath = (shadersDir / "resolve_vertex.glsl").string();
	const std::string fragPath = (shadersDir / "resolve_fragment.glsl").string();
	if (!m_postProcessStack.init(vertPath.c_str(), fragPath.c_str()))
	{
		Logger::Instance().log(Logger::Category::Rendering,
			"ensurePostProcessResources: failed to init resolve shaders (vert=" + vertPath + ", frag=" + fragPath + ")",
			Logger::LogLevel::ERROR);
		return false;
	}

	const std::string bloomDownPath = (shadersDir / "bloom_downsample.glsl").string();
	const std::string bloomBlurPath = (shadersDir / "bloom_blur.glsl").string();
	m_postProcessStack.initBloom(vertPath.c_str(), bloomDownPath.c_str(), bloomBlurPath.c_str());

	const std::string ssaoFragPath = (shadersDir / "ssao_fragment.glsl").string();
	const std::string ssaoBlurPath = (shadersDir / "ssao_blur.glsl").string();
	m_postProcessStack.initSsao(vertPath.c_str(), ssaoFragPath.c_str(), ssaoBlurPath.c_str());
	return true;
}

void OpenGLRenderer::blitUiCache(int width, int height)
{
	if (m_uiFboTexture == 0)
	{
		return;
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	// Use standard Y-up projection so texture V=0 (bottom) maps to screen bottom
	// and V=1 (top) maps to screen top, matching how the FBO stores the UI.
	const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height));
	drawUIImage(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), m_uiFboTexture, projection);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
}

// ---------------------------------------------------------------------------
// cleanupWidgetEditorPreview
// ---------------------------------------------------------------------------
#if ENGINE_EDITOR
void OpenGLRenderer::cleanupWidgetEditorPreview(const std::string& tabId)
{
	m_widgetEditorPreviews.erase(tabId);
}
#endif

// ---------------------------------------------------------------------------
// renderWidgetElement  (unified from 3 duplicate renderElement lambdas)
// ---------------------------------------------------------------------------
void OpenGLRenderer::renderWidgetElement(
	const WidgetElement& element,
	float parentX, float parentY, float parentW, float parentH,
	float parentOpacity, UIRenderContext& ctx)
{
	if (!element.style.isVisible)
		return;

	const auto applyAlpha = [](const Vec4& c, float a) -> Vec4 {
		return Vec4{ c.x, c.y, c.z, c.w * a };
	};

	// ── Opacity inheritance ───────────────────────────────────────────
	const float opa = element.style.opacity * parentOpacity;

	float x0 = element.hasComputedPosition ? element.computedPositionPixels.x : (parentX + element.from.x * parentW);
	float y0 = element.hasComputedPosition ? element.computedPositionPixels.y : (parentY + element.from.y * parentH);
	float widthPx = element.hasComputedSize ? element.computedSizePixels.x : (parentW * (element.to.x - element.from.x));
	float heightPx = element.hasComputedSize ? element.computedSizePixels.y : (parentH * (element.to.y - element.from.y));
	const float x1 = x0 + widthPx;
	const float y1 = y0 + heightPx;

	// ── RenderTransform (Phase 2) ────────────────────────────────────
	const bool hasTransform = !element.renderTransform.isIdentity();
	glm::mat4 savedProjectionRT(1.0f);
	if (hasTransform)
	{
		savedProjectionRT = ctx.projection;
		ctx.projection = ctx.projection * ComputeRenderTransformMatrix(
			element.renderTransform, x0, y0, widthPx, heightPx);
	}
	struct RtRestore { glm::mat4& proj; glm::mat4 saved; bool active;
		~RtRestore() { if (active) proj = saved; } };
	RtRestore rtRestore{ ctx.projection, savedProjectionRT, hasTransform };

	// ── ClipMode scissor (Phase 2) ──────────────────────────────────
	GLint prevScissorBox[4]{};
	GLboolean prevScissorEnabled = GL_FALSE;
	const bool clipToBounds = (element.clipMode == ClipMode::ClipToBounds);
	if (clipToBounds)
	{
		prevScissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
		if (prevScissorEnabled)
			glGetIntegerv(GL_SCISSOR_BOX, prevScissorBox);
		else
		{
			GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
			prevScissorBox[0] = vp[0]; prevScissorBox[1] = vp[1];
			prevScissorBox[2] = vp[2]; prevScissorBox[3] = vp[3];
			glEnable(GL_SCISSOR_TEST);
		}
		const GLint nx = static_cast<GLint>(ctx.scissorOffset.x + x0);
			const GLint ny = static_cast<GLint>(static_cast<float>(ctx.screenHeight) - (ctx.scissorOffset.y + y0 + heightPx));
		const GLint nw = static_cast<GLint>(widthPx);
		const GLint nh = static_cast<GLint>(heightPx);
		const GLint ix0 = std::max(nx, prevScissorBox[0]);
		const GLint iy0 = std::max(ny, prevScissorBox[1]);
		const GLint ix1 = std::min(nx + nw, prevScissorBox[0] + prevScissorBox[2]);
		const GLint iy1 = std::min(ny + nh, prevScissorBox[1] + prevScissorBox[3]);
		glScissor(ix0, iy0, std::max(0, ix1 - ix0), std::max(0, iy1 - iy0));
	}
	struct ScissorRestore { GLint box[4]; GLboolean wasEnabled; bool active;
		~ScissorRestore() { if (active) { glScissor(box[0], box[1], box[2], box[3]); if (!wasEnabled) glDisable(GL_SCISSOR_TEST); } } };
	ScissorRestore scissorRestore{ {prevScissorBox[0], prevScissorBox[1], prevScissorBox[2], prevScissorBox[3]}, prevScissorEnabled, clipToBounds };

	// ── Drop shadow (Phase 1.2) ──────────────────────────────────────
	if (element.style.shadowColor.w > 0.001f)
	{
		const std::string& sv = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
		const std::string& sf = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
		drawUIShadow(x0, y0, x1, y1, element.style.shadowColor, element.style.shadowOffset, ctx.projection, getUIQuadProgram(sv, sf), element.style.borderRadius, element.style.shadowBlurRadius);
	}

	// ── Brush-based background (Phase 2) ─────────────────────────────
	if (element.background.isVisible())
	{
		drawUIBrush(x0, y0, x1, y1, element.background, ctx.projection, opa,
			element.hoverTransitionT, element.hoverBrush.isVisible() ? &element.hoverBrush : nullptr, element.style.borderRadius);
	}

	if (element.type == WidgetElementType::Panel)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		if (!element.background.isVisible()) drawUIPanel(x0, y0, x1, y1, applyAlpha(element.style.color, opa), ctx.projection, prog, applyAlpha(element.style.color, opa), false, element.style.borderRadius);
		for (const auto& child : element.children) renderWidgetElement(child, x0, y0, widthPx, heightPx, opa, ctx);
		if (ctx.debugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{1.0f,0.9f,0.1f,1.0f}, ctx.projection, prog);
		return;
	}
	if (element.type == WidgetElementType::ColorPicker)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		if (element.isCompact)
		{
			if (element.style.color.w > 0.0f) drawUIPanel(x0, y0, x1, y1, applyAlpha(element.style.color, opa), ctx.projection, prog, applyAlpha(element.style.color, opa), false);
			for (const auto& child : element.children) renderWidgetElement(child, x0, y0, widthPx, heightPx, opa, ctx);
			if (ctx.debugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.6f,0.9f,0.2f,1.0f}, ctx.projection, prog);
			return;
		}
		const int columns = 24; const int rows = 6;
		const float cellW = widthPx / static_cast<float>(columns);
		const float cellH = heightPx / static_cast<float>(rows);
		for (int row = 0; row < rows; ++row)
		{
			const float value = 1.0f - (static_cast<float>(row) / std::max(1.0f, static_cast<float>(rows-1)));
			for (int col = 0; col < columns; ++col)
			{
				const float hue = static_cast<float>(col) / std::max(1.0f, static_cast<float>(columns-1));
				const Vec4 c = HsvToRgb(hue, 1.0f, value);
				drawUIPanel(x0+cellW*col, y0+cellH*row, x0+cellW*(col+1), y0+cellH*(row+1), c, ctx.projection, prog, c, false);
			}
		}
		const float sw = std::min(heightPx, 18.0f);
		if (sw > 0.0f)
		{
			const float sx = x1 - sw - 4.0f, sy = y0 + 4.0f;
			drawUIPanel(sx, sy, sx+sw, sy+sw, element.style.color, ctx.projection, prog, element.style.color, false);
			drawUIOutline(sx, sy, sx+sw, sy+sw, Vec4{0.1f,0.1f,0.1f,0.9f}, ctx.projection, prog);
		}
		if (ctx.debugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.6f,0.9f,0.2f,1.0f}, ctx.projection, prog);
		return;
	}
	if (element.type == WidgetElementType::Text || element.type == WidgetElementType::Label)
	{
		const float wPx = std::max(0.0f, x1-x0), hPx = std::max(0.0f, y1-y0);
		const float cx0 = x0+element.padding.x, cy0 = y0+element.padding.y;
		const float cx1 = x1-element.padding.x, cy1 = y1-element.padding.y;
		const float cw = std::max(0.0f, cx1-cx0), ch = std::max(0.0f, cy1-cy0);
		float scale = (element.fontSize > 0.0f) ? (element.fontSize/48.0f) : 1.0f;
		if (element.fontSize <= 0.0f)
		{
			const Vec2 ts = m_textRenderer->measureText(element.text, 1.0f);
			if (ts.x > 0.0f && ts.y > 0.0f) scale = std::min(wPx/ts.x, hPx/ts.y)*0.9f;
			else if (hPx > 0.0f) scale = hPx/48.0f;
		}
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultTextVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultTextFragment);
		const auto drawLine = [&](const std::string& line, const Vec2& pos)
		{
			if (!element.shaderVertex.empty() || !element.shaderFragment.empty())
				m_textRenderer->drawTextWithShader(line, pos, scale, element.style.textColor, vp, fp);
			else
				m_textRenderer->drawText(line, pos, scale, element.style.textColor);
		};
		if (element.wrapText && cw > 0.0f)
		{
			m_textWrapLinesScratch.clear();
			m_textWrapParagraphsScratch.clear();
			auto& lines = m_textWrapLinesScratch;
			auto& paragraphs = m_textWrapParagraphsScratch;
			std::string para;
			for (char c : element.text) { if (c=='\n'){paragraphs.push_back(para);para.clear();}else para.push_back(c); }
			paragraphs.push_back(para);
			const auto wrapWord = [&](const std::string& word, std::string& cur)
			{
				const Vec2 ws = m_textRenderer->measureText(word, scale);
				if (cur.empty() && ws.x > cw)
				{
					std::string chunk;
					for (char wc : word)
					{
						std::string cand = chunk; cand.push_back(wc);
						if (m_textRenderer->measureText(cand, scale).x > cw && !chunk.empty()) { lines.push_back(chunk); chunk.clear(); chunk.push_back(wc); }
						else chunk = cand;
					}
					cur = chunk; return;
				}
				const std::string cand = cur.empty() ? word : (cur+" "+word);
				if (m_textRenderer->measureText(cand, scale).x <= cw || cur.empty()) cur = cand;
				else { lines.push_back(cur); cur = word; }
			};
			for (const auto& p : paragraphs)
			{
				std::string cur, wrd;
				for (char c : p) { if (std::isspace(static_cast<unsigned char>(c))) { if (!wrd.empty()){ wrapWord(wrd,cur); wrd.clear(); } } else wrd.push_back(c); }
				if (!wrd.empty()) wrapWord(wrd, cur);
				if (!cur.empty()) lines.push_back(cur);
			}
			const float lh = m_textRenderer->getLineHeight(scale);
			const float th = lh * static_cast<float>(lines.size());
			float sy = cy0;
			if (element.textAlignV==TextAlignV::Center) sy = cy0+(ch-th)*0.5f;
			else if (element.textAlignV==TextAlignV::Bottom) sy = cy1-th;
			for (size_t i=0;i<lines.size();++i)
			{
				const Vec2 ls = m_textRenderer->measureText(lines[i], scale);
				float tx = cx0;
				if (element.textAlignH==TextAlignH::Center) tx = cx0+(cw-ls.x)*0.5f;
				else if (element.textAlignH==TextAlignH::Right) tx = cx1-ls.x;
				drawLine(lines[i], Vec2{tx, sy+lh*static_cast<float>(i)});
			}
		}
		else
		{
			Vec2 ts = m_textRenderer->measureText(element.text, scale);
			float tx = cx0, ty = cy0;
			if (element.textAlignH==TextAlignH::Center) tx = cx0+(cw-ts.x)*0.5f;
			else if (element.textAlignH==TextAlignH::Right) tx = cx1-ts.x;
			if (element.textAlignV==TextAlignV::Center) ty = cy0+(ch-ts.y)*0.5f;
			else if (element.textAlignV==TextAlignV::Bottom) ty = cy1-ts.y;
			drawLine(element.text, Vec2{tx, ty});
		}
		if (ctx.debugEnabled)
		{
			const GLuint prog = getUIQuadProgram(m_defaultPanelVertex, m_defaultPanelFragment);
			drawUIOutline(x0, y0, x1, y1, Vec4{0.1f,0.8f,1.0f,1.0f}, ctx.projection, prog);
		}
		return;
	}
	if (element.type == WidgetElementType::ProgressBar)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		drawUIPanel(x0, y0, x1, y1, element.style.color, ctx.projection, prog, element.style.color, false, element.style.borderRadius);
		const float range = element.maxValue - element.minValue;
		const float ratio = (range>0.0f) ? std::clamp((element.valueFloat-element.minValue)/range, 0.0f, 1.0f) : 0.0f;
		if (ratio > 0.0f) drawUIPanel(x0, y0, x0+widthPx*ratio, y1, element.style.fillColor, ctx.projection, prog, element.style.fillColor, false, element.style.borderRadius);
		if (ctx.debugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.2f,0.9f,0.6f,1.0f}, ctx.projection, prog);
		return;
	}
	if (element.type == WidgetElementType::Slider)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		const float range = element.maxValue - element.minValue;
		const float ratio = (range>0.0f) ? std::clamp((element.valueFloat-element.minValue)/range, 0.0f, 1.0f) : 0.0f;
		const float th = std::min(heightPx, 6.0f);
		const float ty0 = y0+(heightPx-th)*0.5f, ty1 = ty0+th;
		drawUIPanel(x0, ty0, x1, ty1, element.style.color, ctx.projection, prog, element.style.color, false);
		if (ratio > 0.0f) drawUIPanel(x0, ty0, x0+widthPx*ratio, ty1, element.style.fillColor, ctx.projection, prog, element.style.fillColor, false);
		const float hs = std::max(10.0f, heightPx);
		float hx = std::clamp(x0+widthPx*ratio-hs*0.5f, x0, x1-hs);
		drawUIPanel(hx, y0, hx+hs, y1, element.style.textColor, ctx.projection, prog, element.style.textColor, false);
		if (ctx.debugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.2f,0.7f,1.0f,1.0f}, ctx.projection, prog);
		return;
	}
	if (element.type == WidgetElementType::EntryBar)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		drawUIPanel(x0, y0, x1, y1, element.style.color, ctx.projection, prog, element.style.hoverColor, element.hoverTransitionT, element.style.borderRadius);
		const float fs = (element.fontSize>0.0f)?element.fontSize:14.0f, sc = fs/48.0f;
		std::string disp = element.value;
		if (element.isPassword) disp.assign(element.value.size(), '*');
		const float cx0e = x0+element.padding.x, cy0e = y0+element.padding.y;
		const float cx1e = x1-element.padding.x, cy1e = y1-element.padding.y;
		const float chE = std::max(0.0f, cy1e-cy0e);
		Vec2 ts{};
		if (!disp.empty())
		{
			ts = m_textRenderer->measureText(disp, sc);
			m_textRenderer->drawText(disp, Vec2{cx0e, cy0e+std::max(0.0f,(chE-ts.y)*0.5f)}, sc, element.style.textColor);
		}
		if (element.isFocused)
		{
			const float ch2 = std::max(0.0f, chE-2.0f);
			const float cx = cx0e+ts.x+1.0f;
			if (cx < cx1e) drawUIPanel(cx, cy0e+1.0f, cx+2.0f, cy0e+1.0f+ch2, element.style.textColor, ctx.projection, prog, element.style.textColor, false);
			// Focus highlight: subtle blue outline around the entry bar
			drawUIOutline(x0, y0, x1, y1, Vec4{0.25f, 0.55f, 0.95f, 0.8f}, ctx.projection, prog);
		}
		if (ctx.debugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.5f,0.8f,1.0f,1.0f}, ctx.projection, prog);
		return;
	}
	if (element.type == WidgetElementType::Button)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultButtonVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultButtonFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		drawUIPanel(x0, y0, x1, y1, element.style.color, ctx.projection, prog, element.style.hoverColor, element.hoverTransitionT, element.style.borderRadius);
		if (!element.text.empty())
		{
			const float cx0b=x0+element.padding.x, cy0b=y0+element.padding.y;
			const float cx1b=x1-element.padding.x, cy1b=y1-element.padding.y;
			const float cwB=std::max(0.0f,cx1b-cx0b), chB=std::max(0.0f,cy1b-cy0b);
			float sc = (element.fontSize>0.0f) ? element.fontSize/48.0f : 1.0f;
			if (element.fontSize<=0.0f)
			{
				const Vec2 ts=m_textRenderer->measureText(element.text,1.0f);
				if (ts.x>0.0f&&ts.y>0.0f) sc=std::min(cwB/ts.x,chB/ts.y)*0.9f;
			}
			Vec2 ts=m_textRenderer->measureText(element.text,sc);
			float tx=cx0b, ty=cy0b;
			if (element.textAlignH==TextAlignH::Center) tx=cx0b+(cwB-ts.x)*0.5f;
			else if (element.textAlignH==TextAlignH::Right) tx=cx1b-ts.x;
			if (element.textAlignV==TextAlignV::Center) ty=cy0b+(chB-ts.y)*0.5f;
			else if (element.textAlignV==TextAlignV::Bottom) ty=cy1b-ts.y;
			m_textRenderer->drawText(element.text, Vec2{tx,ty}, sc, element.style.textColor);
		}
		else if (element.textureId!=0 || !element.imagePath.empty())
		{
			const GLuint tex=(element.textureId!=0)?static_cast<GLuint>(element.textureId):getOrLoadUITexture(element.imagePath);
			if (tex!=0) drawUIImage(x0+4.0f, y0+4.0f, x1-4.0f, y1-4.0f, tex, ctx.projection);
		}
		for (const auto& child : element.children) renderWidgetElement(child, x0, y0, x1-x0, y1-y0, opa, ctx);
		if (ctx.debugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.7f,1.0f,0.3f,1.0f}, ctx.projection, prog);
		return;
	}
	if (element.type == WidgetElementType::ToggleButton || element.type == WidgetElementType::RadioButton)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultButtonVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultButtonFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		const Vec4 displayColor = element.isChecked ? element.style.fillColor : element.style.color;
		drawUIPanel(x0, y0, x1, y1, displayColor, ctx.projection, prog, element.style.hoverColor, element.hoverTransitionT, element.style.borderRadius);
		if (!element.text.empty())
		{
			const float cx0b=x0+element.padding.x, cy0b=y0+element.padding.y;
			const float cx1b=x1-element.padding.x, cy1b=y1-element.padding.y;
			const float cwB=std::max(0.0f,cx1b-cx0b), chB=std::max(0.0f,cy1b-cy0b);
			float sc = (element.fontSize>0.0f) ? element.fontSize/48.0f : 1.0f;
			if (element.fontSize<=0.0f)
			{
				const Vec2 ts=m_textRenderer->measureText(element.text,1.0f);
				if (ts.x>0.0f&&ts.y>0.0f) sc=std::min(cwB/ts.x,chB/ts.y)*0.9f;
			}
			Vec2 ts=m_textRenderer->measureText(element.text,sc);
			float tx=cx0b, ty=cy0b;
			if (element.textAlignH==TextAlignH::Center) tx=cx0b+(cwB-ts.x)*0.5f;
			else if (element.textAlignH==TextAlignH::Right) tx=cx1b-ts.x;
			if (element.textAlignV==TextAlignV::Center) ty=cy0b+(chB-ts.y)*0.5f;
			else if (element.textAlignV==TextAlignV::Bottom) ty=cy1b-ts.y;
			m_textRenderer->drawText(element.text, Vec2{tx,ty}, sc, element.style.textColor);
		}
		if (ctx.debugEnabled) drawUIOutline(x0, y0, x1, y1, Vec4{0.7f,1.0f,0.3f,1.0f}, ctx.projection, prog);
		return;
	}
	if (element.type == WidgetElementType::Separator)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		const Vec4 lineColor = (element.style.color.w > 0.0f) ? element.style.color : Vec4{0.35f, 0.35f, 0.40f, 0.8f};
		drawUIPanel(x0, y0, x1, y1, lineColor, ctx.projection, prog, lineColor, false);
		return;
	}
	if (element.type == WidgetElementType::ScrollView)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		drawUIPanel(x0, y0, x1, y1, applyAlpha(element.style.color, opa), ctx.projection, prog, applyAlpha(element.style.color, opa), false, element.style.borderRadius);
		for (const auto& child : element.children) renderWidgetElement(child, x0, y0, widthPx, heightPx, opa, ctx);

		// ── Scrollbar overlay ────────────────────────────────────
		if (element.scrollbarOpacity > 0.001f &&
			element.hasContentSize && element.hasComputedSize)
		{
			const auto& sbTheme = EditorTheme::Get();
			const float contentH = element.contentSizePixels.y;
			const float viewportH = element.computedSizePixels.y;
			if (contentH > viewportH)
			{
				const float maxScroll = contentH - viewportH;
				const float sbW = element.scrollbarHovered ? sbTheme.scrollbarWidthHover : sbTheme.scrollbarWidth;
				const float thumbRatio = viewportH / contentH;
				const float thumbH = std::max(EditorTheme::Scaled(20.0f), heightPx * thumbRatio);
				const float trackAvail = heightPx - thumbH;
				const float thumbOff = (maxScroll > 0.0f) ? (element.scrollOffset / maxScroll) * trackAvail : 0.0f;
				const float sbX0 = x1 - sbW;

				const GLuint sbProg = getUIQuadProgram(m_defaultPanelVertex, m_defaultPanelFragment);

				Vec4 trackColor = sbTheme.scrollbarTrack;
				trackColor.w *= element.scrollbarOpacity * opa;
				drawUIPanel(sbX0, y0, x1, y1, trackColor, ctx.projection, sbProg, trackColor, 0.0f, sbTheme.scrollbarBorderRadius);

				Vec4 thumbColor = element.scrollbarHovered ? sbTheme.scrollbarThumbHover : sbTheme.scrollbarThumb;
				thumbColor.w *= element.scrollbarOpacity * opa;
				drawUIPanel(sbX0, y0 + thumbOff, x1, y0 + thumbOff + thumbH, thumbColor, ctx.projection, sbProg, thumbColor, 0.0f, sbTheme.scrollbarBorderRadius);
			}
		}
		return;
	}
	if (element.type == WidgetElementType::Image)
	{
		if (element.textureId!=0 || !element.imagePath.empty())
		{
			const GLuint tex=(element.textureId!=0)?static_cast<GLuint>(element.textureId):getOrLoadUITexture(element.imagePath);
			if (tex!=0) drawUIImage(x0, y0, x1, y1, tex, ctx.projection, element.style.color, true);
		}
		if (ctx.debugEnabled)
		{
			const GLuint prog=getUIQuadProgram(m_defaultPanelVertex,m_defaultPanelFragment);
			drawUIOutline(x0, y0, x1, y1, Vec4{0.2f,1.0f,1.0f,1.0f}, ctx.projection, prog);
		}
		return;
	}
	if (element.type == WidgetElementType::CheckBox)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		const float bs=std::min(heightPx-2.0f,16.0f);
		const float bx0=x0+element.padding.x, by0=y0+(heightPx-bs)*0.5f, bx1=bx0+bs, by1=by0+bs;
		drawUIPanel(bx0,by0,bx1,by1,element.style.color,ctx.projection,prog,element.style.hoverColor,element.hoverTransitionT,element.style.borderRadius);
		drawUIOutline(bx0,by0,bx1,by1,Vec4{0.4f,0.4f,0.45f,0.9f},ctx.projection,prog);
		if (element.isChecked)
		{
			const float inset=3.0f;
			drawUIPanel(bx0+inset,by0+inset,bx1-inset,by1-inset,element.style.fillColor,ctx.projection,prog,element.style.fillColor,false);
		}
		if (!element.text.empty())
		{
			const float sc=(element.fontSize>0.0f)?element.fontSize/48.0f:14.0f/48.0f;
			const Vec2 ts=m_textRenderer->measureText(element.text,sc);
			m_textRenderer->drawText(element.text,Vec2{bx1+6.0f,y0+(heightPx-ts.y)*0.5f},sc,element.style.textColor);
		}
		if (ctx.debugEnabled) drawUIOutline(x0,y0,x1,y1,Vec4{0.9f,0.5f,0.2f,1.0f},ctx.projection,prog);
		return;
	}
	if (element.type == WidgetElementType::DropdownButton)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultButtonVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultButtonFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		drawUIPanel(x0,y0,x1,y1,element.style.color,ctx.projection,prog,element.style.hoverColor,element.hoverTransitionT,element.style.borderRadius);
		const float sc=(element.fontSize>0.0f)?element.fontSize/48.0f:14.0f/48.0f;

		// Draw image icon (if set, like a Button)
		if (!element.imagePath.empty() && element.textureId != 0)
		{
			const float imgSize=std::min(widthPx-element.padding.x*2.0f-12.0f,heightPx-element.padding.y*2.0f);
			if (imgSize>0.0f)
			{
				const float ix0=x0+element.padding.x, iy0=y0+(heightPx-imgSize)*0.5f;
				drawUIImage(ix0,iy0,ix0+imgSize,iy0+imgSize,element.textureId,ctx.projection,element.style.textColor);
			}
		}

		// Draw text
		if (!element.text.empty())
		{
			const Vec2 ts=m_textRenderer->measureText(element.text,sc);
			float tx=x0+element.padding.x;
			if (!element.imagePath.empty() && element.textureId!=0)
			{
				const float imgSize=std::min(widthPx-element.padding.x*2.0f-12.0f,heightPx-element.padding.y*2.0f);
				tx+=imgSize+4.0f;
			}
			const float ty=y0+(heightPx-ts.y)*0.5f;
			m_textRenderer->drawText(element.text,Vec2{tx,ty},sc,element.style.textColor);
		}

		// Draw small down-arrow indicator on the right
		const float arrowSize=std::min(heightPx*0.3f,6.0f);
		const float arrowX=x1-element.padding.x-arrowSize;
		const float arrowY=y0+(heightPx-arrowSize)*0.5f;
		drawUIPanel(arrowX,arrowY,arrowX+arrowSize,arrowY+arrowSize,element.style.textColor,ctx.projection,prog,element.style.textColor,false);

		if (ctx.debugEnabled) drawUIOutline(x0,y0,x1,y1,Vec4{0.9f,0.7f,0.1f,1.0f},ctx.projection,prog);
		return;
	}
	if (element.type == WidgetElementType::DropDown)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		drawUIPanel(x0,y0,x1,y1,element.style.color,ctx.projection,prog,element.style.hoverColor,element.hoverTransitionT,element.style.borderRadius);
		const float sc=(element.fontSize>0.0f)?element.fontSize/48.0f:14.0f/48.0f;
		const float cx0d=x0+element.padding.x, cy0d=y0+element.padding.y, cy1d=y1-element.padding.y;
		const float chD=std::max(0.0f,cy1d-cy0d);
		std::string disp=element.text;
		if (disp.empty()&&element.selectedIndex>=0&&element.selectedIndex<static_cast<int>(element.items.size()))
			disp=element.items[static_cast<size_t>(element.selectedIndex)];
		if (!disp.empty())
		{
			const Vec2 ts=m_textRenderer->measureText(disp,sc);
			m_textRenderer->drawText(disp,Vec2{cx0d,cy0d+std::max(0.0f,(chD-ts.y)*0.5f)},sc,element.style.textColor);
		}
		const float as=std::min(heightPx*0.4f,8.0f);
		drawUIPanel(x1-element.padding.x-as, y0+(heightPx-as)*0.5f, x1-element.padding.x, y0+(heightPx-as)*0.5f+as, element.style.textColor,ctx.projection,prog,element.style.textColor,false);
		if (element.isExpanded && !element.items.empty())
		{
			const float fs=(element.fontSize>0.0f)?element.fontSize:14.0f;
			if (ctx.deferredDropdowns) ctx.deferredDropdowns->push_back({ x0, y0, x1, y1, fs, element.selectedIndex, element.items, element.style.textColor, element.style.hoverColor, element.padding });
		}
		if (ctx.debugEnabled) drawUIOutline(x0,y0,x1,y1,Vec4{0.9f,0.6f,0.1f,1.0f},ctx.projection,prog);
		return;
	}
	if (element.type == WidgetElementType::DropdownButton)
	{
		const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultButtonVertex);
		const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultButtonFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		drawUIPanel(x0,y0,x1,y1,element.style.color,ctx.projection,prog,element.style.hoverColor,element.hoverTransitionT,element.style.borderRadius);
		if (!element.text.empty())
		{
			const float sc=(element.fontSize>0.0f)?element.fontSize/48.0f:14.0f/48.0f;
			const float cx0b=x0+element.padding.x, cy0b=y0+element.padding.y;
			const float cx1b=x1-element.padding.x, cy1b=y1-element.padding.y;
			const float cwB=std::max(0.0f,cx1b-cx0b), chB=std::max(0.0f,cy1b-cy0b);
			const Vec2 ts=m_textRenderer->measureText(element.text,sc);
			float textX=cx0b, textY=cy0b+std::max(0.0f,(chB-ts.y)*0.5f);
			switch(element.textAlignH){case TextAlignH::Center: textX=cx0b+(cwB-ts.x)*0.5f; break; case TextAlignH::Right: textX=cx1b-ts.x; break; default: break;}
			m_textRenderer->drawText(element.text,Vec2{textX,textY},sc,element.style.textColor);
		}
		const float as=std::min(heightPx*0.3f,6.0f);
		const float arrowX=x1-element.padding.x-as;
		const float arrowY=y0+(heightPx-as)*0.5f;
		drawUIPanel(arrowX,arrowY,arrowX+as,arrowY+as,element.style.textColor,ctx.projection,prog,element.style.textColor,false);
		if (ctx.debugEnabled) drawUIOutline(x0,y0,x1,y1,Vec4{0.9f,0.7f,0.1f,1.0f},ctx.projection,prog);
		return;
	}
	if (element.type == WidgetElementType::TreeView || element.type == WidgetElementType::TabView)
	{
		if (element.style.color.w > 0.0f)
		{
			const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
			const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
			drawUIPanel(x0,y0,x1,y1,element.style.color,ctx.projection,getUIQuadProgram(vp,fp),element.style.color,false,element.style.borderRadius);
		}
		const GLint cx=static_cast<GLint>(ctx.scissorOffset.x + x0);
		const GLint cy=static_cast<GLint>(static_cast<float>(ctx.screenHeight)-(ctx.scissorOffset.y + y1));
		const GLsizei cw=static_cast<GLsizei>(std::max(0.0f,widthPx));
		const GLsizei ch=static_cast<GLsizei>(std::max(0.0f,heightPx));
		glEnable(GL_SCISSOR_TEST); glScissor(cx,cy,cw,ch);
		for (const auto& child : element.children) renderWidgetElement(child, x0, y0, widthPx, heightPx, opa, ctx);
		glDisable(GL_SCISSOR_TEST);
		if (ctx.debugEnabled)
		{
			const GLuint prog=getUIQuadProgram(m_defaultPanelVertex,m_defaultPanelFragment);
			drawUIOutline(x0,y0,x1,y1,Vec4{0.4f,0.9f,0.6f,1.0f},ctx.projection,prog);
		}
		return;
	}
	if (element.type == WidgetElementType::StackPanel || element.type == WidgetElementType::Grid)
	{
		if (element.style.color.w > 0.0f)
		{
			const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
			const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
			const Vec4& hc = (element.style.hoverColor.w > 0.0f) ? element.style.hoverColor : element.style.color;
			drawUIPanel(x0,y0,x1,y1,element.style.color,ctx.projection,getUIQuadProgram(vp,fp),hc,element.hoverTransitionT,element.style.borderRadius);
		}
		if (element.scrollable || element.type == WidgetElementType::ScrollView)
		{
			const GLint cx=static_cast<GLint>(ctx.scissorOffset.x + x0);
			const GLint cy=static_cast<GLint>(static_cast<float>(ctx.screenHeight)-(ctx.scissorOffset.y + y1));
			const GLsizei cw=static_cast<GLsizei>(std::max(0.0f,widthPx));
			const GLsizei ch=static_cast<GLsizei>(std::max(0.0f,heightPx));
			glEnable(GL_SCISSOR_TEST); glScissor(cx,cy,cw,ch);
			for (const auto& child : element.children) renderWidgetElement(child, x0, y0, widthPx, heightPx, opa, ctx);
			glDisable(GL_SCISSOR_TEST);

			// ── Scrollbar overlay ────────────────────────────────────
			if (element.scrollbarOpacity > 0.001f &&
				element.hasContentSize && element.hasComputedSize)
			{
				const auto& sbTheme = EditorTheme::Get();
				const float contentH = element.contentSizePixels.y;
				const float viewportH = element.computedSizePixels.y;
				if (contentH > viewportH)
				{
					const float maxScroll = contentH - viewportH;
					const float sbW = element.scrollbarHovered ? sbTheme.scrollbarWidthHover : sbTheme.scrollbarWidth;
					const float thumbRatio = viewportH / contentH;
					const float thumbH = std::max(EditorTheme::Scaled(20.0f), heightPx * thumbRatio);
					const float trackAvail = heightPx - thumbH;
					const float thumbOff = (maxScroll > 0.0f) ? (element.scrollOffset / maxScroll) * trackAvail : 0.0f;
					const float sbX0 = x1 - sbW;

					const GLuint sbProg = getUIQuadProgram(m_defaultPanelVertex, m_defaultPanelFragment);

					Vec4 trackColor = sbTheme.scrollbarTrack;
					trackColor.w *= element.scrollbarOpacity * opa;
					drawUIPanel(sbX0, y0, x1, y1, trackColor, ctx.projection, sbProg, trackColor, 0.0f, sbTheme.scrollbarBorderRadius);

					Vec4 thumbColor = element.scrollbarHovered ? sbTheme.scrollbarThumbHover : sbTheme.scrollbarThumb;
					thumbColor.w *= element.scrollbarOpacity * opa;
					drawUIPanel(sbX0, y0 + thumbOff, x1, y0 + thumbOff + thumbH, thumbColor, ctx.projection, sbProg, thumbColor, 0.0f, sbTheme.scrollbarBorderRadius);
				}
			}
		}
		else
		{
			for (const auto& child : element.children) renderWidgetElement(child, x0, y0, widthPx, heightPx, opa, ctx);
		}
		if (ctx.debugEnabled)
		{
			const GLuint prog=getUIQuadProgram(m_defaultPanelVertex,m_defaultPanelFragment);
			drawUIOutline(x0,y0,x1,y1,Vec4{1.0f,0.4f,0.8f,1.0f},ctx.projection,prog);
		}
		return;
	}
	if (element.type == WidgetElementType::WrapBox
		|| element.type == WidgetElementType::UniformGrid
		|| element.type == WidgetElementType::SizeBox
		|| element.type == WidgetElementType::ScaleBox
		|| element.type == WidgetElementType::WidgetSwitcher
		|| element.type == WidgetElementType::Overlay
		|| element.type == WidgetElementType::ListView
		|| element.type == WidgetElementType::TileView)
	{
		if (element.style.color.w > 0.0f)
		{
			const std::string& vp = resolveUIShaderPath(element.shaderVertex, m_defaultPanelVertex);
			const std::string& fp = resolveUIShaderPath(element.shaderFragment, m_defaultPanelFragment);
			const Vec4& hc = (element.style.hoverColor.w > 0.0f) ? element.style.hoverColor : element.style.color;
			drawUIPanel(x0,y0,x1,y1,element.style.color,ctx.projection,getUIQuadProgram(vp,fp),hc,element.hoverTransitionT,element.style.borderRadius);
		}
		for (const auto& child : element.children) renderWidgetElement(child, x0, y0, widthPx, heightPx, opa, ctx);
		if (ctx.debugEnabled)
		{
			const GLuint prog=getUIQuadProgram(m_defaultPanelVertex,m_defaultPanelFragment);
			drawUIOutline(x0,y0,x1,y1,Vec4{0.3f,0.8f,1.0f,1.0f},ctx.projection,prog);
		}
		return;
	}
	// Border widget: background + 4 border-brush edges + child rendering
	if (element.type == WidgetElementType::Border)
	{
		const GLuint prog = getUIQuadProgram(m_defaultPanelVertex, m_defaultPanelFragment);
		// Background
		if (element.style.color.w > 0.0f)
			drawUIPanel(x0, y0, x1, y1, element.style.color, ctx.projection, prog, element.style.color, false, element.style.borderRadius);
		// Border edges via borderBrush
		if (element.borderBrush.isVisible())
		{
			float bL = element.borderThicknessLeft, bT = element.borderThicknessTop;
			float bR = element.borderThicknessRight, bB = element.borderThicknessBottom;
			Vec4 bc = element.borderBrush.color;
			drawUIPanel(x0, y0, x0 + bL, y1, bc, ctx.projection, prog, bc, false);         // left
			drawUIPanel(x1 - bR, y0, x1, y1, bc, ctx.projection, prog, bc, false);         // right
			drawUIPanel(x0 + bL, y0, x1 - bR, y0 + bT, bc, ctx.projection, prog, bc, false); // top
			drawUIPanel(x0 + bL, y1 - bB, x1 - bR, y1, bc, ctx.projection, prog, bc, false); // bottom
		}
		for (const auto& child : element.children) renderWidgetElement(child, x0, y0, widthPx, heightPx, opa, ctx);
		return;
	}
	// Spinner: animated dots in a circle
	if (element.type == WidgetElementType::Spinner)
	{
		const GLuint prog = getUIQuadProgram(m_defaultPanelVertex, m_defaultPanelFragment);
		int dots = std::max(1, element.spinnerDotCount);
		float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
		float radius = std::min(x1 - x0, y1 - y0) * 0.4f;
		float dotSize = radius * 0.22f;
		float phase = element.spinnerElapsed * element.spinnerSpeed * 2.0f * 3.14159265f;
		for (int d = 0; d < dots; ++d)
		{
			float angle = phase + (static_cast<float>(d) / dots) * 2.0f * 3.14159265f;
			float dx = cx + std::cos(angle) * radius;
			float dy = cy + std::sin(angle) * radius;
			float alpha = 1.0f - static_cast<float>(d) / dots;
			Vec4 dc = element.style.color; dc.w *= alpha * opa;
			drawUIPanel(dx - dotSize, dy - dotSize, dx + dotSize, dy + dotSize, dc, ctx.projection, prog, dc, false);
		}
		return;
	}
	// RichText: render as plain text for now (full markup parsing is a future step)
	if (element.type == WidgetElementType::RichText)
	{
		if (!element.richText.empty() && m_textRenderer)
		{
			std::string plainText = element.richText;
			// Strip basic tags for display
			auto stripTag = [](std::string& s, const std::string& tag) {
				std::string open = "<" + tag + ">", close = "</" + tag + ">";
				size_t pos;
				while ((pos = s.find(open)) != std::string::npos) s.erase(pos, open.size());
				while ((pos = s.find(close)) != std::string::npos) s.erase(pos, close.size());
			};
			stripTag(plainText, "b"); stripTag(plainText, "i"); stripTag(plainText, "u");
			float fs = element.fontSize > 0.0f ? element.fontSize : 14.0f;
			float scale = fs / 48.0f;
			Vec4 tc = element.style.textColor; tc.w *= opa;
			m_textRenderer->drawText(plainText, Vec2{ x0, y0 + fs }, scale, tc);
		}
		return;
	}
}

// ---------------------------------------------------------------------------
// drawUIWidgetsToFramebuffer
// Renders all widgets of 'mgr' into the currently-bound framebuffer.
// Caller is responsible for binding the target FBO and setting glViewport.
// ---------------------------------------------------------------------------
void OpenGLRenderer::drawUIWidgetsToFramebuffer(UIManager& mgr, int width, int height)
{
#if ENGINE_EDITOR
	if (mgr.isUIRenderingPaused()) return;
#endif
	if (!m_textRenderer || !ensureUIQuadRenderer()) return;

	m_textRenderer->setScreenSize(width, height);
	mgr.setAvailableViewportSize(Vec2{ static_cast<float>(width), static_cast<float>(height) });
	m_currentRenderViewportSize = Vec2{ static_cast<float>(width), static_cast<float>(height) };

	if (mgr.needsLayoutUpdate())
	{
		mgr.updateLayouts([this](const std::string& text, float scale)
			{ return m_textRenderer ? m_textRenderer->measureText(text, scale) : Vec2{}; });
	}

	ensureUIShaderDefaults();
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	glm::mat4 uiProjection = glm::ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);


	m_deferredDropDownsScratch.clear();
	UIRenderContext ctx;
	ctx.projection = uiProjection;
	ctx.screenHeight = height;
	ctx.debugEnabled = m_uiDebugEnabled;
	ctx.deferredDropdowns = &m_deferredDropDownsScratch;

	const auto& ordered = mgr.getWidgetsOrderedByZ();
#if ENGINE_EDITOR
	Vec4 editorCanvasRect{};
	const bool hasCanvasClip = mgr.getWidgetEditorCanvasRect(editorCanvasRect);
#endif

	for (const auto* entry : ordered)
	{
		if (!entry || !entry->widget) continue;
		const auto& widget = entry->widget;
		Vec2 widgetSize = widget->getSizePixels();
		Vec2 widgetPosition{};
		if (widget->hasComputedSize())    widgetSize    = widget->getComputedSizePixels();
		if (widget->hasComputedPosition()) widgetPosition = widget->getComputedPositionPixels();
		if (widgetSize.x <= 0.0f) widgetSize.x = static_cast<float>(width);
		if (widgetSize.y <= 0.0f) widgetSize.y = static_cast<float>(height);

#if ENGINE_EDITOR
		// Clip widget editor preview to canvas area
		const bool needsClip = hasCanvasClip && mgr.isWidgetEditorContentWidget(entry->id);
		if (needsClip)
		{
			const GLint cx = static_cast<GLint>(editorCanvasRect.x);
			const GLint cy = static_cast<GLint>(static_cast<float>(height) - editorCanvasRect.y - editorCanvasRect.w);
			const GLsizei cw = static_cast<GLsizei>(std::max(0.0f, editorCanvasRect.z));
			const GLsizei ch = static_cast<GLsizei>(std::max(0.0f, editorCanvasRect.w));
			glEnable(GL_SCISSOR_TEST);
			glScissor(cx, cy, cw, ch);
		}
#endif

		for (const auto& element : widget->getElements())
			renderWidgetElement(element, widgetPosition.x, widgetPosition.y, widgetSize.x, widgetSize.y, 1.0f, ctx);

#if ENGINE_EDITOR
		if (needsClip)
		{
			glDisable(GL_SCISSOR_TEST);
		}
#endif
	}

	// Deferred pass: draw expanded DropDown items on top of everything
	for (const auto& dd : m_deferredDropDownsScratch)
	{
		const std::string& vp = resolveUIShaderPath("", m_defaultPanelVertex);
		const std::string& fp = resolveUIShaderPath("", m_defaultPanelFragment);
		const GLuint prog = getUIQuadProgram(vp, fp);
		const float sc = dd.fontSize / 48.0f;
		const float heightPx = dd.y1 - dd.y0;
		const float ih = std::max(EditorTheme::Scaled(20.0f), heightPx);
		const float cx0d = dd.x0 + dd.padding.x;
		// Background panel behind all items
		if (!dd.items.empty())
		{
			const float totalH = static_cast<float>(dd.items.size()) * ih;
			const auto& theme = EditorTheme::Get();
			drawUIShadow(dd.x0, dd.y1, dd.x1, dd.y1 + totalH, theme.shadowColor, theme.shadowOffset, uiProjection, prog, theme.borderRadius);
			drawUIPanel(dd.x0, dd.y1, dd.x1, dd.y1 + totalH, Vec4{0.12f,0.12f,0.15f,0.98f}, uiProjection, prog, Vec4{0,0,0,0}, false);
		}
		for (size_t i = 0; i < dd.items.size(); ++i)
		{
			const float iy0 = dd.y1 + static_cast<float>(i) * ih;
			const float iy1 = iy0 + ih;
			const bool sel = (static_cast<int>(i) == dd.selectedIndex);
			const Vec4 ic = sel ? Vec4{0.22f,0.22f,0.28f,0.98f} : Vec4{0.14f,0.14f,0.18f,0.98f};
			drawUIPanel(dd.x0, iy0, dd.x1, iy1, ic, uiProjection, prog, dd.hoverColor, false);
			const Vec2 its = m_textRenderer->measureText(dd.items[i], sc);
			m_textRenderer->drawText(dd.items[i], Vec2{cx0d, iy0 + (ih - its.y) * 0.5f}, sc, dd.textColor);
		}
	}

	mgr.clearRenderDirty();
	glDisable(GL_BLEND);
}

#if ENGINE_EDITOR
// ---------------------------------------------------------------------------
// ensurePopupUIVao
// popup GL context that reuses the shared m_uiQuadVbo.
// ---------------------------------------------------------------------------
void OpenGLRenderer::ensurePopupUIVao()
{
	if (m_popupUiVao != 0) return;
	if (m_uiQuadVbo == 0) return; // main VBO must exist first

	glGenVertexArrays(1, &m_popupUiVao);
	glBindVertexArray(m_popupUiVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_uiQuadVbo); // shared VBO is accessible
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// renderPopupWindows – renders every open popup window.
// ---------------------------------------------------------------------------
void OpenGLRenderer::renderPopupWindows()
{
	if (m_popupWindows.empty()) return;

	// Clean up popups that have been marked as closed (deferred destruction).
	bool anyDestroyed = false;
	for (auto it = m_popupWindows.begin(); it != m_popupWindows.end(); )
	{
		if (it->second && !it->second->isOpen())
		{
			it->second->destroy();
			it = m_popupWindows.erase(it);
			anyDestroyed = true;

			// The popup's GL context was destroyed, invalidating context-local VAOs.
			// Reset handles so they get recreated for the next popup.
			m_popupUiVao = 0;
			if (m_textRenderer)
			{
				m_textRenderer->resetPopupVao();
			}
		}
		else
		{
			++it;
		}
	}

	// If a popup was destroyed its GL context is gone – ensure the main
	// context is current again before any further GL work (including renderUI).
	if (anyDestroyed)
	{
		SDL_GL_MakeCurrent(m_window, m_glContext);
	}

	if (m_popupWindows.empty()) return;
	if (!m_textRenderer) return;

	// Ensure main context resources are ready before we switch contexts.
	ensureUIQuadRenderer();
	ensureUIShaderDefaults();

	for (auto& [id, popup] : m_popupWindows)
	{
		if (!popup || !popup->isOpen()) continue;
		if (!popup->sdlWindow() || !popup->renderContext()) continue;

		popup->renderContext()->makeCurrent(popup->sdlWindow());

		// Create context-local VAOs on first use (VAOs are not shared between GL contexts).
		ensurePopupUIVao();
		m_textRenderer->ensurePopupVao();

		popup->refreshSize();
		const int pw = popup->width();
		const int ph = popup->height();
		if (pw <= 0 || ph <= 0)
		{
			SDL_GL_MakeCurrent(m_window, m_glContext);
			continue;
		}

		glViewport(0, 0, pw, ph);
		glDisable(GL_DEPTH_TEST);
		const auto& bg = EditorTheme::Get().windowBackground;
		glClearColor(bg.x, bg.y, bg.z, bg.w);
		glClear(GL_COLOR_BUFFER_BIT);

		// Propagate pending theme changes to the popup's own UIManager.
		popup->uiManager().applyPendingThemeUpdate();

		// Temporarily swap in the popup VAOs so drawUIPanel/drawUIImage/drawText use them.
		const GLuint savedVao = m_uiQuadVao;
		m_uiQuadVao = m_popupUiVao;
		const GLuint savedTextVao = m_textRenderer->swapVao(m_textRenderer->getPopupVao());

		drawUIWidgetsToFramebuffer(popup->uiManager(), pw, ph);

		m_uiQuadVao = savedVao;
		m_textRenderer->swapVao(savedTextVao);

		SDL_GL_SwapWindow(popup->sdlWindow());
	}

	// Restore main context.
	SDL_GL_MakeCurrent(m_window, m_glContext);
}

// ---------------------------------------------------------------------------
// Popup window management
// ---------------------------------------------------------------------------
PopupWindow* OpenGLRenderer::openPopupWindow(const std::string& id, const std::string& title, int width, int height)
{
	auto it = m_popupWindows.find(id);
	if (it != m_popupWindows.end())
	{
		if (it->second && it->second->isOpen())
		{
			SDL_RaiseWindow(it->second->sdlWindow());
			return it->second.get();
		}
		m_popupWindows.erase(it);
	}

	// Main context must be current when creating the shared child context.
	SDL_GL_MakeCurrent(m_window, m_glContext);

	auto popup = std::make_unique<PopupWindow>();
	auto context = std::make_unique<OpenGLRenderContext>();
	if (!popup->create(title, width, height, SDL_WINDOW_OPENGL, std::move(context)))
		return nullptr;

	// Restore main context after popup creation.
	SDL_GL_MakeCurrent(m_window, m_glContext);

	PopupWindow* ptr = popup.get();
	m_popupWindows[id] = std::move(popup);
	Logger::Instance().log(Logger::Category::Rendering,
		"Popup window opened: " + id, Logger::LogLevel::INFO);
	return ptr;
}

void OpenGLRenderer::closePopupWindow(const std::string& id)
{
	auto it = m_popupWindows.find(id);
	if (it != m_popupWindows.end())
	{
		// Mark for deferred destruction in renderPopupWindows().
		// Immediate destruction is unsafe when called from within event routing
		// (the popup's UIManager and SDL window may still be referenced on the stack).
		if (it->second)
		{
			it->second->close();
		}
		Logger::Instance().log(Logger::Category::Rendering,
			"Popup window closing: " + id, Logger::LogLevel::INFO);
	}
}

PopupWindow* OpenGLRenderer::getPopupWindow(const std::string& id)
{
	auto it = m_popupWindows.find(id);
	return (it != m_popupWindows.end() && it->second && it->second->isOpen())
		? it->second.get() : nullptr;
}

bool OpenGLRenderer::routeEventToPopup(SDL_Event& event)
{
	if (m_popupWindows.empty()) return false;

	SDL_WindowID eventWindowId = 0;
	switch (event.type)
	{
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:   eventWindowId = event.button.windowID; break;
	case SDL_EVENT_MOUSE_MOTION:      eventWindowId = event.motion.windowID; break;
	case SDL_EVENT_MOUSE_WHEEL:       eventWindowId = event.wheel.windowID;  break;
	case SDL_EVENT_TEXT_INPUT:        eventWindowId = event.text.windowID;   break;
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:            eventWindowId = event.key.windowID;    break;
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		for (auto& [id, popup] : m_popupWindows)
		{
			if (popup && popup->sdlWindow() &&
				SDL_GetWindowID(popup->sdlWindow()) == event.window.windowID)
			{
				popup->close();
				return true;
			}
		}
		return false;
	default: return false;
	}

	for (auto& [id, popup] : m_popupWindows)
	{
		if (!popup || !popup->isOpen() || !popup->sdlWindow()) continue;
		if (SDL_GetWindowID(popup->sdlWindow()) != eventWindowId) continue;

		UIManager& uiMgr = popup->uiManager();
		switch (event.type)
		{
		case SDL_EVENT_MOUSE_MOTION:
		{
			const Vec2 pos{ event.motion.x, event.motion.y };
			uiMgr.setMousePosition(pos);
			uiMgr.handleMouseMotion(pos);
			return true;
		}
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		{
			const Vec2 pos{ event.button.x, event.button.y };
			uiMgr.setMousePosition(pos);
			uiMgr.handleMouseDown(pos, event.button.button);
			return true;
		}
		case SDL_EVENT_MOUSE_BUTTON_UP:
		{
			const Vec2 pos{ event.button.x, event.button.y };
			uiMgr.handleMouseUp(pos, event.button.button);
			return true;
		}
		case SDL_EVENT_MOUSE_WHEEL:
			uiMgr.handleScroll(uiMgr.getMousePosition(), event.wheel.y);
			return true;
		case SDL_EVENT_TEXT_INPUT:
			uiMgr.handleTextInput(event.text.text);
			return true;
		case SDL_EVENT_KEY_DOWN:
			uiMgr.handleKeyDown(event.key.key);
			return true;
		case SDL_EVENT_KEY_UP:
			return true;
		default: break;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Mesh viewer editor window – takes over viewport, no tab created
// ---------------------------------------------------------------------------
MeshViewerWindow* OpenGLRenderer::openMeshViewer(const std::string& assetPath)
{
	// Return existing viewer if already open – just switch to its tab.
	{
		auto it = m_meshViewers.find(assetPath);
		if (it != m_meshViewers.end() && it->second)
		{
			setActiveTab(assetPath);
			return it->second.get();
		}
	}

	const std::string displayName = std::filesystem::path(assetPath).stem().string();

	m_uiManager.showToastMessage("Loading " + displayName + "...", UIManager::kToastLong);

	const std::string resolvedPath = m_resourceManager.resolveContentPath(assetPath);
	if (resolvedPath.empty())
	{
		Logger::Instance().log(Logger::Category::Rendering,
			"openMeshViewer: could not resolve content path for '" + assetPath + "'",
			Logger::LogLevel::WARNING);
		m_uiManager.showToastMessage("Failed to resolve " + displayName, UIManager::kToastMedium);
		return nullptr;
	}

	auto& assetMgr = AssetManager::Instance();
	auto existingAsset = assetMgr.getLoadedAssetByPath(resolvedPath);
	if (!existingAsset)
	{
		Logger::Instance().log(Logger::Category::Rendering,
			"openMeshViewer: asset '" + resolvedPath + "' not in memory, calling loadAsset (Sync)...",
			Logger::LogLevel::INFO);
		const int loadId = assetMgr.loadAsset(resolvedPath, AssetType::Model3D, AssetManager::Sync);
		if (loadId == 0)
		{
			Logger::Instance().log(Logger::Category::Rendering,
				"openMeshViewer: loadAsset returned 0 for '" + resolvedPath
				+ "' (original='" + assetPath + "'). The asset file may not exist or could not be parsed.",
				Logger::LogLevel::WARNING);
			m_uiManager.showToastMessage("Failed to load " + displayName, UIManager::kToastMedium);
			return nullptr;
		}
		Logger::Instance().log(Logger::Category::Rendering,
			"openMeshViewer: loadAsset returned id=" + std::to_string(loadId) + " for '" + resolvedPath + "'",
			Logger::LogLevel::INFO);
	}
	else
	{
		Logger::Instance().log(Logger::Category::Rendering,
			"openMeshViewer: asset '" + resolvedPath + "' already in memory (id="
			+ std::to_string(existingAsset->getId()) + ")",
			Logger::LogLevel::INFO);
	}

	auto viewer = std::make_unique<MeshViewerWindow>();
	if (!viewer->initialize(resolvedPath, m_resourceManager))
	{
		m_uiManager.showToastMessage("Failed to open " + displayName, UIManager::kToastMedium);
		Logger::Instance().log(Logger::Category::Rendering,
			"Mesh viewer init failed: " + resolvedPath + " (see preceding log lines for the specific step that failed)",
			Logger::LogLevel::WARNING);
		return nullptr;
	}

	if (!viewer->createRuntimeLevel(assetPath))
	{
		m_uiManager.showToastMessage("Failed to create preview level for " + displayName, UIManager::kToastMedium);
		return nullptr;
	}

	MeshViewerWindow* ptr = viewer.get();
	m_meshViewers[assetPath] = std::move(viewer);

	// Create a new editor tab for this mesh viewer
	addTab(assetPath, displayName, true);

	// Register click events for the tab button and close button
	m_uiManager.registerClickEvent("TitleBar.Tab." + assetPath, [this, assetPath]()
	{
		setActiveTab(assetPath);
		m_uiManager.markAllWidgetsDirty();
	});
	m_uiManager.registerClickEvent("TitleBar.TabClose." + assetPath, [this, assetPath]()
	{
		closeMeshViewer(assetPath);
	});

	// Register the details panel widget (tab-scoped – only visible when this tab is active)
	{
		auto propsWidget = std::make_shared<Widget>();
		propsWidget->setName("MeshViewerDetails." + assetPath);
		propsWidget->setAnchor(WidgetAnchor::TopRight);
		propsWidget->setSizePixels(Vec2{ 320.0f, 0.0f });
		propsWidget->setFillY(true);
		propsWidget->setZOrder(1);

		std::vector<WidgetElement> elements;

		WidgetElement root{};
		root.type = WidgetElementType::StackPanel;
		root.id = "MeshViewer.Details.Root";
		root.from = Vec2{ 0.0f, 0.0f };
		root.to = Vec2{ 1.0f, 1.0f };
		root.fillX = true;
		root.fillY = true;
		root.style.color = Vec4{ 0.14f, 0.14f, 0.18f, 0.95f };
		root.padding = Vec2{ 10.0f, 10.0f };
		root.scrollable = true;

		WidgetElement title{};
		title.type = WidgetElementType::Text;
		title.id = "MeshViewer.Details.Title";
		title.text = displayName;
		title.font = EditorTheme::Get().fontDefault;
		title.fontSize = EditorTheme::Get().fontSizeHeading;
		title.style.textColor = EditorTheme::Get().textPrimary;
		title.fillX = true;
		title.minSize = Vec2{ 0.0f, EditorTheme::Scaled(28.0f) };
		title.runtimeOnly = true;
		root.children.push_back(std::move(title));

		WidgetElement pathLine{};
		pathLine.type = WidgetElementType::Text;
		pathLine.id = "MeshViewer.Details.Path";
		pathLine.text = "Path: " + assetPath;
		pathLine.font = EditorTheme::Get().fontDefault;
		pathLine.fontSize = EditorTheme::Get().fontSizeSmall;
		pathLine.style.textColor = EditorTheme::Get().textMuted;
		pathLine.fillX = true;
		pathLine.minSize = Vec2{ 0.0f, EditorTheme::Scaled(20.0f) };
		pathLine.runtimeOnly = true;
		root.children.push_back(std::move(pathLine));

		WidgetElement statsLine{};
		statsLine.type = WidgetElementType::Text;
		statsLine.id = "MeshViewer.Details.Stats";
		statsLine.text = "Vertices: " + std::to_string(ptr->getVertexCount())
			+ "  Indices: " + std::to_string(ptr->getIndexCount());
		statsLine.font = EditorTheme::Get().fontDefault;
		statsLine.fontSize = EditorTheme::Get().fontSizeSmall;
		statsLine.style.textColor = EditorTheme::Get().textMuted;
		statsLine.fillX = true;
		statsLine.minSize = Vec2{ 0.0f, EditorTheme::Scaled(20.0f) };
		statsLine.runtimeOnly = true;
		root.children.push_back(std::move(statsLine));

		// --- Separator ---
		{
			WidgetElement sep{};
			sep.type = WidgetElementType::Panel;
			sep.fillX = true;
			sep.minSize = Vec2{ 0.0f, 1.0f };
			sep.style.color = Vec4{ 0.3f, 0.3f, 0.35f, 0.6f };
			sep.runtimeOnly = true;
			root.children.push_back(std::move(sep));
		}

		// --- Section header: Transform ---
		{
			WidgetElement header{};
			header.type = WidgetElementType::Text;
			header.text = "Transform";
			header.font = EditorTheme::Get().fontDefault;
			header.fontSize = EditorTheme::Get().fontSizeBody;
			header.style.textColor = EditorTheme::Get().textSecondary;
			header.fillX = true;
			header.minSize = Vec2{ 0.0f, EditorTheme::Scaled(24.0f) };
			header.runtimeOnly = true;
			root.children.push_back(std::move(header));
		}

		// Helper to create a label + entry bar row
		const auto makeFloatRow = [&](const std::string& label, const std::string& fieldId, float value,
			std::function<void(const std::string&)> onChange)
		{
			WidgetElement row{};
			row.type = WidgetElementType::StackPanel;
			row.fillX = true;
			row.minSize = Vec2{ 0.0f, EditorTheme::Scaled(26.0f) };
			row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
			row.runtimeOnly = true;
			row.orientation = StackOrientation::Horizontal;

			WidgetElement lbl{};
			lbl.type = WidgetElementType::Text;
			lbl.text = label;
			lbl.font = EditorTheme::Get().fontDefault;
			lbl.fontSize = EditorTheme::Get().fontSizeSmall;
			lbl.style.textColor = EditorTheme::Get().textMuted;
			lbl.minSize = Vec2{ EditorTheme::Scaled(80.0f), EditorTheme::Scaled(26.0f) };
			lbl.textAlignV = TextAlignV::Center;
			lbl.runtimeOnly = true;
			row.children.push_back(std::move(lbl));

			WidgetElement entry{};
			entry.type = WidgetElementType::EntryBar;
			entry.id = fieldId;
			entry.value = std::to_string(value);
			// Trim trailing zeros for cleaner display
			if (entry.value.find('.') != std::string::npos)
			{
				entry.value.erase(entry.value.find_last_not_of('0') + 1, std::string::npos);
				if (entry.value.back() == '.') entry.value.push_back('0');
				}
				entry.font = EditorTheme::Get().fontDefault;
				entry.fontSize = EditorTheme::Get().fontSizeSmall;
				entry.fillX = true;
				entry.minSize = Vec2{ EditorTheme::Scaled(60.0f), EditorTheme::Get().rowHeight };
				entry.style.color = EditorTheme::Get().inputBackground;
				entry.style.textColor = EditorTheme::Get().inputText;
				entry.style.hoverColor = EditorTheme::Get().inputBackgroundHover;
				entry.padding = Vec2{ 6.0f, 4.0f };
				entry.hitTestMode = HitTestMode::Enabled;
				entry.runtimeOnly = true;
				entry.onValueChanged = std::move(onChange);
			row.children.push_back(std::move(entry));

			root.children.push_back(std::move(row));
		};

		// Read current scale from the mesh asset data (defaults to 1.0)
		float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
		std::string currentMaterialPath;
		{
			auto meshAsset = assetMgr.getLoadedAssetByPath(resolvedPath);
			if (meshAsset)
			{
				const auto& data = meshAsset->getData();
				if (data.contains("m_scale") && data["m_scale"].is_array() && data["m_scale"].size() >= 3)
				{
					scaleX = data["m_scale"][0].get<float>();
					scaleY = data["m_scale"][1].get<float>();
					scaleZ = data["m_scale"][2].get<float>();
				}
				if (data.contains("m_materialAssetPaths") && data["m_materialAssetPaths"].is_array()
					&& !data["m_materialAssetPaths"].empty())
				{
					currentMaterialPath = data["m_materialAssetPaths"][0].get<std::string>();
				}
			}
		}

		// Scale X/Y/Z
		const std::string capturedResolved = resolvedPath;
		makeFloatRow("Scale X", "MeshViewer.Details.ScaleX", scaleX,
			[capturedResolved](const std::string& val) {
				auto asset = AssetManager::Instance().getLoadedAssetByPath(capturedResolved);
				if (!asset) return;
				try {
					float v = std::stof(val);
					auto& data = asset->getData();
					if (!data.contains("m_scale")) data["m_scale"] = json::array({1.0f, 1.0f, 1.0f});
					data["m_scale"][0] = v;
					asset->setIsSaved(false);
				} catch (...) {}
			});
		makeFloatRow("Scale Y", "MeshViewer.Details.ScaleY", scaleY,
			[capturedResolved](const std::string& val) {
				auto asset = AssetManager::Instance().getLoadedAssetByPath(capturedResolved);
				if (!asset) return;
				try {
					float v = std::stof(val);
					auto& data = asset->getData();
					if (!data.contains("m_scale")) data["m_scale"] = json::array({1.0f, 1.0f, 1.0f});
					data["m_scale"][1] = v;
					asset->setIsSaved(false);
				} catch (...) {}
			});
		makeFloatRow("Scale Z", "MeshViewer.Details.ScaleZ", scaleZ,
			[capturedResolved](const std::string& val) {
				auto asset = AssetManager::Instance().getLoadedAssetByPath(capturedResolved);
				if (!asset) return;
				try {
					float v = std::stof(val);
					auto& data = asset->getData();
					if (!data.contains("m_scale")) data["m_scale"] = json::array({1.0f, 1.0f, 1.0f});
					data["m_scale"][2] = v;
					asset->setIsSaved(false);
				} catch (...) {}
			});

		// --- Separator ---
		{
			WidgetElement sep{};
			sep.type = WidgetElementType::Panel;
			sep.fillX = true;
			sep.minSize = Vec2{ 0.0f, 1.0f };
			sep.style.color = Vec4{ 0.3f, 0.3f, 0.35f, 0.6f };
			sep.runtimeOnly = true;
			root.children.push_back(std::move(sep));
		}

		// --- Section header: Material ---
		{
			WidgetElement header{};
			header.type = WidgetElementType::Text;
			header.text = "Material";
			header.font = EditorTheme::Get().fontDefault;
			header.fontSize = EditorTheme::Get().fontSizeBody;
			header.style.textColor = EditorTheme::Get().textSecondary;
			header.fillX = true;
			header.minSize = Vec2{ 0.0f, EditorTheme::Scaled(24.0f) };
			header.runtimeOnly = true;
			root.children.push_back(std::move(header));
		}

		// Material path entry
		{
			WidgetElement row{};
			row.type = WidgetElementType::StackPanel;
			row.fillX = true;
			row.minSize = Vec2{ 0.0f, EditorTheme::Scaled(26.0f) };
			row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
			row.runtimeOnly = true;
			row.orientation = StackOrientation::Horizontal;

			WidgetElement lbl{};
			lbl.type = WidgetElementType::Text;
			lbl.text = "Path";
			lbl.font = EditorTheme::Get().fontDefault;
			lbl.fontSize = EditorTheme::Get().fontSizeSmall;
			lbl.style.textColor = EditorTheme::Get().textMuted;
			lbl.minSize = Vec2{ EditorTheme::Scaled(80.0f), EditorTheme::Scaled(26.0f) };
			lbl.textAlignV = TextAlignV::Center;
			lbl.runtimeOnly = true;
			row.children.push_back(std::move(lbl));

			WidgetElement entry{};
			entry.type = WidgetElementType::EntryBar;
			entry.id = "MeshViewer.Details.MaterialPath";
			entry.value = currentMaterialPath;
			entry.font = EditorTheme::Get().fontDefault;
			entry.fontSize = EditorTheme::Get().fontSizeSmall;
			entry.fillX = true;
			entry.minSize = Vec2{ EditorTheme::Scaled(60.0f), EditorTheme::Get().rowHeight };
			entry.style.color = EditorTheme::Get().inputBackground;
			entry.style.textColor = EditorTheme::Get().inputText;
			entry.style.hoverColor = EditorTheme::Get().inputBackgroundHover;
			entry.padding = Vec2{ 6.0f, 4.0f };
			entry.hitTestMode = HitTestMode::Enabled;
			entry.runtimeOnly = true;
			entry.onValueChanged = [capturedResolved](const std::string& val) {
				auto asset = AssetManager::Instance().getLoadedAssetByPath(capturedResolved);
				if (!asset) return;
				auto& data = asset->getData();
				if (!data.contains("m_materialAssetPaths"))
					data["m_materialAssetPaths"] = json::array();
				auto& paths = data["m_materialAssetPaths"];
				if (paths.empty()) paths.push_back(val);
				else paths[0] = val;
				asset->setIsSaved(false);
			};
			row.children.push_back(std::move(entry));
			root.children.push_back(std::move(row));
		}

		WidgetElement content{};
		content.type = WidgetElementType::StackPanel;
		content.id = "MeshViewer.Details.Content";
		content.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
		content.fillX = true;
		content.sizeToContent = true;
		content.padding = Vec2{ 0.0f, 4.0f };
		content.runtimeOnly = true;
		root.children.push_back(std::move(content));

		elements.push_back(std::move(root));
		propsWidget->setElements(std::move(elements));

		// Tab-scoped: only visible/layouted when this tab is active
		m_uiManager.registerWidget("MeshViewerDetails." + assetPath, propsWidget, assetPath);
	}

	// Switch to the new tab (handles level swap, camera save/restore)
	setActiveTab(assetPath);

	m_uiManager.markAllWidgetsDirty();
	m_uiManager.showToastMessage(displayName + " ready", UIManager::kToastMedium);
	Logger::Instance().log(Logger::Category::Rendering,
		"Mesh viewer opened: " + assetPath, Logger::LogLevel::INFO);
	return ptr;
}

void OpenGLRenderer::closeMeshViewer(const std::string& assetPath)
{
	// If this viewer's tab is active, switch back to Viewport first (handles level swap)
	if (m_activeTabId == assetPath)
	{
		setActiveTab("Viewport");
	}

	// Destroy the viewer
	auto it = m_meshViewers.find(assetPath);
	if (it != m_meshViewers.end() && it->second)
	{
		it->second->destroyRuntimeLevel();
	}
	m_meshViewers.erase(assetPath);

	// Clean up per-tab selection state
	m_tabSelectedEntities.erase(assetPath);

	// Unregister the details panel widget
	m_uiManager.unregisterWidget("MeshViewerDetails." + assetPath);

	// Remove the tab button and close button from TitleBar
	{
		auto* tabsStack = m_uiManager.findElementById("TitleBar.Tabs");
		if (tabsStack)
		{
			const std::string tabBtnId = "TitleBar.Tab." + assetPath;
			const std::string closeBtnId = "TitleBar.TabClose." + assetPath;
			tabsStack->children.erase(
				std::remove_if(tabsStack->children.begin(), tabsStack->children.end(),
					[&](const WidgetElement& el) { return el.id == tabBtnId || el.id == closeBtnId; }),
				tabsStack->children.end());
		}
	}

	// Remove the editor tab and its FBO
	removeTab(assetPath);

	m_uiManager.markAllWidgetsDirty();
}

MeshViewerWindow* OpenGLRenderer::getMeshViewer(const std::string& assetPath)
{
	auto it = m_meshViewers.find(assetPath);
	return (it != m_meshViewers.end()) ? it->second.get() : nullptr;
}

// ============================================================================
// Material Editor Tab
// ============================================================================

MaterialEditorWindow* OpenGLRenderer::openMaterialEditorTab(const std::string& assetPath)
{
	// Return existing editor if already open – just switch to its tab.
	{
		auto it = m_materialEditors.find(assetPath);
		if (it != m_materialEditors.end() && it->second)
		{
			setActiveTab(assetPath);
			return it->second.get();
		}
	}

	const std::string displayName = std::filesystem::path(assetPath).stem().string();

	m_uiManager.showToastMessage("Loading " + displayName + "...", UIManager::kToastLong);

	auto& assetMgr = AssetManager::Instance();
	const std::string resolvedPath = m_resourceManager.resolveContentPath(assetPath);

	// Ensure the material asset is loaded
	auto existingAsset = assetMgr.getLoadedAssetByPath(resolvedPath.empty() ? assetPath : resolvedPath);
	if (!existingAsset)
	{
		const std::string loadPath = resolvedPath.empty() ? assetPath : resolvedPath;
		const int loadId = assetMgr.loadAsset(loadPath, AssetType::Material, AssetManager::Sync);
		if (loadId == 0)
		{
			Logger::Instance().log(Logger::Category::Rendering,
				"openMaterialEditorTab: loadAsset returned 0 for '" + loadPath + "'",
				Logger::LogLevel::WARNING);
			m_uiManager.showToastMessage("Failed to load " + displayName, UIManager::kToastMedium);
			return nullptr;
		}
	}

	auto editor = std::make_unique<MaterialEditorWindow>();
	if (!editor->initialize(assetPath))
	{
		m_uiManager.showToastMessage("Failed to open " + displayName, UIManager::kToastMedium);
		return nullptr;
	}

	if (!editor->createRuntimeLevel())
	{
		m_uiManager.showToastMessage("Failed to create preview level for " + displayName, UIManager::kToastMedium);
		return nullptr;
	}

	MaterialEditorWindow* ptr = editor.get();
	m_materialEditors[assetPath] = std::move(editor);

	// Create a new editor tab
	addTab(assetPath, displayName, true);

	// Register click events for tab / close button
	m_uiManager.registerClickEvent("TitleBar.Tab." + assetPath, [this, assetPath]()
	{
		setActiveTab(assetPath);
		m_uiManager.markAllWidgetsDirty();
	});
	m_uiManager.registerClickEvent("TitleBar.TabClose." + assetPath, [this, assetPath]()
	{
		closeMaterialEditorTab(assetPath);
	});

	// ------------------------------------------------------------------
	// Build the properties panel (tab-scoped)
	// ------------------------------------------------------------------
	{
		auto propsWidget = std::make_shared<Widget>();
		propsWidget->setName("MaterialEditorDetails." + assetPath);
		propsWidget->setAnchor(WidgetAnchor::TopRight);
		propsWidget->setSizePixels(Vec2{ 320.0f, 0.0f });
		propsWidget->setFillY(true);
		propsWidget->setZOrder(1);

		std::vector<WidgetElement> elements;

		WidgetElement root{};
		root.type = WidgetElementType::StackPanel;
		root.id = "MatEd.Root";
		root.from = Vec2{ 0.0f, 0.0f };
		root.to = Vec2{ 1.0f, 1.0f };
		root.fillX = true;
		root.fillY = true;
		root.style.color = Vec4{ 0.14f, 0.14f, 0.18f, 0.95f };
		root.padding = Vec2{ 10.0f, 10.0f };
		root.scrollable = true;

		// Title
		root.children.push_back(EditorUIBuilder::makeHeading(displayName));

		// Path subtitle
		root.children.push_back(EditorUIBuilder::makeSecondaryLabel("Path: " + assetPath));

		root.children.push_back(EditorUIBuilder::makeDivider());

		// --- Read current values from the material asset ---
		float shininess = 32.0f;
		float metallic = 0.0f;
		float roughness = 0.5f;
		bool pbrEnabled = false;
		std::string texPaths[5]; // diffuse, specular, normal, emissive, metallicRoughness

		const std::string matLoadPath = resolvedPath.empty() ? assetPath : resolvedPath;
		auto matAsset = assetMgr.getLoadedAssetByPath(matLoadPath);
		if (matAsset)
		{
			const json& d = matAsset->getData();
			shininess  = d.value("m_shininess", 32.0f);
			metallic   = d.value("m_metallic", 0.0f);
			roughness  = d.value("m_roughness", 0.5f);
			pbrEnabled = d.value("m_pbrEnabled", false);

			if (d.contains("m_textureAssetPaths") && d["m_textureAssetPaths"].is_array())
			{
				const auto& arr = d["m_textureAssetPaths"];
				for (int i = 0; i < 5 && i < static_cast<int>(arr.size()); ++i)
				{
					if (arr[i].is_string())
						texPaths[i] = arr[i].get<std::string>();
				}
			}
		}

		// --- PBR section ---
		{
			std::vector<WidgetElement> pbrChildren;

			pbrChildren.push_back(EditorUIBuilder::makeCheckBox(
				"MatEd.PBR", "PBR Enabled", pbrEnabled,
				[matLoadPath](bool val) {
					auto asset = AssetManager::Instance().getLoadedAssetByPath(matLoadPath);
					if (!asset) return;
					asset->getData()["m_pbrEnabled"] = val;
					asset->setIsSaved(false);
				}));

			pbrChildren.push_back(EditorUIBuilder::makeSliderRow(
				"MatEd.Metallic", "Metallic", metallic, 0.0f, 1.0f,
				[matLoadPath](float val) {
					auto asset = AssetManager::Instance().getLoadedAssetByPath(matLoadPath);
					if (!asset) return;
					asset->getData()["m_metallic"] = val;
					asset->setIsSaved(false);
				}));

			pbrChildren.push_back(EditorUIBuilder::makeSliderRow(
				"MatEd.Roughness", "Roughness", roughness, 0.0f, 1.0f,
				[matLoadPath](float val) {
					auto asset = AssetManager::Instance().getLoadedAssetByPath(matLoadPath);
					if (!asset) return;
					asset->getData()["m_roughness"] = val;
					asset->setIsSaved(false);
				}));

			pbrChildren.push_back(EditorUIBuilder::makeSliderRow(
				"MatEd.Shininess", "Shininess", shininess, 1.0f, 256.0f,
				[matLoadPath](float val) {
					auto asset = AssetManager::Instance().getLoadedAssetByPath(matLoadPath);
					if (!asset) return;
					asset->getData()["m_shininess"] = val;
					asset->setIsSaved(false);
				}));

			root.children.push_back(EditorUIBuilder::makeSection("MatEd.PBRSection", "PBR Properties", pbrChildren));
		}

		// --- Texture slots section ---
		{
			std::vector<WidgetElement> texChildren;

			const char* slotNames[] = { "Diffuse", "Specular", "Normal", "Emissive", "MetallicRoughness" };

			for (int i = 0; i < 5; ++i)
			{
				int slot = i;
				texChildren.push_back(EditorUIBuilder::makeStringRow(
					std::string("MatEd.Tex") + slotNames[i], slotNames[i], texPaths[i],
					[matLoadPath, slot](const std::string& val) {
						auto asset = AssetManager::Instance().getLoadedAssetByPath(matLoadPath);
						if (!asset) return;
						auto& data = asset->getData();
						if (!data.contains("m_textureAssetPaths"))
							data["m_textureAssetPaths"] = json::array({"", "", "", "", ""});
						auto& arr = data["m_textureAssetPaths"];
						while (static_cast<int>(arr.size()) <= slot)
							arr.push_back("");
						arr[slot] = val;
						asset->setIsSaved(false);
					}));
			}

			root.children.push_back(EditorUIBuilder::makeSection("MatEd.TexSection", "Textures", texChildren));
		}

		root.children.push_back(EditorUIBuilder::makeDivider());

		// --- Save button ---
		root.children.push_back(EditorUIBuilder::makePrimaryButton(
			"MatEd.Save", "Save Material",
			[matLoadPath, this]() {
				auto& mgr = AssetManager::Instance();
				auto asset = mgr.getLoadedAssetByPath(matLoadPath);
				if (asset)
				{
					Asset a;
					a.type = AssetType::Material;
					a.ID   = asset->getId();
					mgr.saveAsset(a, AssetManager::Sync);
					m_uiManager.showToastMessage("Material saved.", UIManager::kToastShort);
				}
			}));

		elements.push_back(std::move(root));
		propsWidget->setElements(std::move(elements));

		m_uiManager.registerWidget("MaterialEditorDetails." + assetPath, propsWidget, assetPath);
	}

	// Switch to the new tab
	setActiveTab(assetPath);

	m_uiManager.markAllWidgetsDirty();
	m_uiManager.showToastMessage(displayName + " ready", UIManager::kToastMedium);
	Logger::Instance().log(Logger::Category::Rendering,
		"Material editor tab opened: " + assetPath, Logger::LogLevel::INFO);
	return ptr;
}

void OpenGLRenderer::closeMaterialEditorTab(const std::string& assetPath)
{
	if (m_activeTabId == assetPath)
	{
		setActiveTab("Viewport");
	}

	auto it = m_materialEditors.find(assetPath);
	if (it != m_materialEditors.end() && it->second)
	{
		it->second->destroyRuntimeLevel();
	}
	m_materialEditors.erase(assetPath);

	m_tabSelectedEntities.erase(assetPath);

	m_uiManager.unregisterWidget("MaterialEditorDetails." + assetPath);

	{
		auto* tabsStack = m_uiManager.findElementById("TitleBar.Tabs");
		if (tabsStack)
		{
			const std::string tabBtnId = "TitleBar.Tab." + assetPath;
			const std::string closeBtnId = "TitleBar.TabClose." + assetPath;
			tabsStack->children.erase(
				std::remove_if(tabsStack->children.begin(), tabsStack->children.end(),
					[&](const WidgetElement& el) { return el.id == tabBtnId || el.id == closeBtnId; }),
				tabsStack->children.end());
		}
	}

	removeTab(assetPath);
	m_uiManager.markAllWidgetsDirty();
}

MaterialEditorWindow* OpenGLRenderer::getMaterialEditor(const std::string& assetPath)
{
	auto it = m_materialEditors.find(assetPath);
	return (it != m_materialEditors.end()) ? it->second.get() : nullptr;
}

// ============================================================================
// Texture Viewer
// ============================================================================

TextureViewerWindow* OpenGLRenderer::openTextureViewer(const std::string& assetPath)
{
	// Return existing viewer if already open – just switch to its tab.
	{
		auto it = m_textureViewers.find(assetPath);
		if (it != m_textureViewers.end() && it->second)
		{
			setActiveTab(assetPath);
			return it->second.get();
		}
	}

	const std::string displayName = std::filesystem::path(assetPath).stem().string();
	m_uiManager.showToastMessage("Loading " + displayName + "...", UIManager::kToastLong);

	const std::string resolvedPath = m_resourceManager.resolveContentPath(assetPath);
	if (resolvedPath.empty())
	{
		Logger::Instance().log(Logger::Category::Rendering,
			"openTextureViewer: could not resolve content path for '" + assetPath + "'",
			Logger::LogLevel::WARNING);
		m_uiManager.showToastMessage("Failed to resolve " + displayName, UIManager::kToastMedium);
		return nullptr;
	}

	// Ensure the texture asset is loaded into the AssetManager
	auto& assetMgr = AssetManager::Instance();
	auto existingAsset = assetMgr.getLoadedAssetByPath(resolvedPath);
	if (!existingAsset)
	{
		const int loadId = assetMgr.loadAsset(resolvedPath, AssetType::Texture, AssetManager::Sync);
		if (loadId == 0)
		{
			Logger::Instance().log(Logger::Category::Rendering,
				"openTextureViewer: loadAsset returned 0 for '" + resolvedPath + "'",
				Logger::LogLevel::WARNING);
			m_uiManager.showToastMessage("Failed to load " + displayName, UIManager::kToastMedium);
			return nullptr;
		}
	}

	auto viewer = std::make_unique<TextureViewerWindow>();
	if (!viewer->initialize(resolvedPath))
	{
		m_uiManager.showToastMessage("Failed to open " + displayName, UIManager::kToastMedium);
		Logger::Instance().log(Logger::Category::Rendering,
			"Texture viewer init failed: " + resolvedPath, Logger::LogLevel::WARNING);
		return nullptr;
	}

	// Upload the texture to GPU for display via getOrLoadUITexture.
	// Prefer the source image path (actual image file that stbi can decode)
	// over the .asset path to avoid a spurious "Failed to load raw image" log.
	GLuint glTex = 0;
	{
		auto asset = assetMgr.getLoadedAssetByPath(resolvedPath);
		if (asset && asset->getData().contains("m_sourcePath"))
		{
			std::string sourcePath = asset->getData()["m_sourcePath"].get<std::string>();
			if (!sourcePath.empty())
				glTex = getOrLoadUITexture(sourcePath);
		}
	}
	if (glTex == 0)
		glTex = getOrLoadUITexture(resolvedPath);
	viewer->setGLTextureId(glTex);

	TextureViewerWindow* ptr = viewer.get();
	m_textureViewers[assetPath] = std::move(viewer);

	// Create a new editor tab
	addTab(assetPath, displayName, true);

	// Register click events for tab button and close button
	m_uiManager.registerClickEvent("TitleBar.Tab." + assetPath, [this, assetPath]()
	{
		setActiveTab(assetPath);
		m_uiManager.markAllWidgetsDirty();
	});
	m_uiManager.registerClickEvent("TitleBar.TabClose." + assetPath, [this, assetPath]()
	{
		closeTextureViewer(assetPath);
	});

	// Build the details panel widget (tab-scoped)
	{
		auto propsWidget = std::make_shared<Widget>();
		propsWidget->setName("TextureViewerDetails." + assetPath);
		propsWidget->setAnchor(WidgetAnchor::TopRight);
		propsWidget->setSizePixels(Vec2{ 320.0f, 0.0f });
		propsWidget->setFillY(true);
		propsWidget->setZOrder(1);

		std::vector<WidgetElement> elements;

		WidgetElement root{};
		root.type = WidgetElementType::StackPanel;
		root.id = "TexViewer.Details.Root";
		root.from = Vec2{ 0.0f, 0.0f };
		root.to = Vec2{ 1.0f, 1.0f };
		root.fillX = true;
		root.fillY = true;
		root.style.color = Vec4{ 0.14f, 0.14f, 0.18f, 0.95f };
		root.padding = Vec2{ 10.0f, 10.0f };
		root.scrollable = true;

		// Title
		{
			WidgetElement title{};
			title.type = WidgetElementType::Text;
			title.text = displayName;
			title.font = EditorTheme::Get().fontDefault;
			title.fontSize = EditorTheme::Get().fontSizeHeading;
			title.style.textColor = EditorTheme::Get().textPrimary;
			title.fillX = true;
			title.minSize = Vec2{ 0.0f, EditorTheme::Scaled(28.0f) };
			title.runtimeOnly = true;
			root.children.push_back(std::move(title));
		}

		// Path
		{
			WidgetElement pathLine{};
			pathLine.type = WidgetElementType::Text;
			pathLine.text = "Path: " + assetPath;
			pathLine.font = EditorTheme::Get().fontDefault;
			pathLine.fontSize = EditorTheme::Get().fontSizeSmall;
			pathLine.style.textColor = EditorTheme::Get().textMuted;
			pathLine.fillX = true;
			pathLine.minSize = Vec2{ 0.0f, EditorTheme::Scaled(20.0f) };
			pathLine.runtimeOnly = true;
			root.children.push_back(std::move(pathLine));
		}

		// Dimensions
		{
			WidgetElement dims{};
			dims.type = WidgetElementType::Text;
			dims.text = "Size: " + std::to_string(ptr->getWidth()) + " x " + std::to_string(ptr->getHeight())
				+ "  Channels: " + std::to_string(ptr->getChannels());
			dims.font = EditorTheme::Get().fontDefault;
			dims.fontSize = EditorTheme::Get().fontSizeSmall;
			dims.style.textColor = EditorTheme::Get().textMuted;
			dims.fillX = true;
			dims.minSize = Vec2{ 0.0f, EditorTheme::Scaled(20.0f) };
			dims.runtimeOnly = true;
			root.children.push_back(std::move(dims));
		}

		// Format
		{
			WidgetElement fmt{};
			fmt.type = WidgetElementType::Text;
			fmt.text = "Format: " + ptr->getFormatString();
			fmt.font = EditorTheme::Get().fontDefault;
			fmt.fontSize = EditorTheme::Get().fontSizeSmall;
			fmt.style.textColor = EditorTheme::Get().textMuted;
			fmt.fillX = true;
			fmt.minSize = Vec2{ 0.0f, EditorTheme::Scaled(20.0f) };
			fmt.runtimeOnly = true;
			root.children.push_back(std::move(fmt));
		}

		// File size
		if (ptr->getFileSizeBytes() > 0)
		{
			std::string sizeStr;
			const size_t bytes = ptr->getFileSizeBytes();
			if (bytes >= 1024 * 1024)
				sizeStr = std::to_string(bytes / (1024 * 1024)) + " MB";
			else if (bytes >= 1024)
				sizeStr = std::to_string(bytes / 1024) + " KB";
			else
				sizeStr = std::to_string(bytes) + " B";

			WidgetElement fs{};
			fs.type = WidgetElementType::Text;
			fs.text = "File Size: " + sizeStr;
			fs.font = EditorTheme::Get().fontDefault;
			fs.fontSize = EditorTheme::Get().fontSizeSmall;
			fs.style.textColor = EditorTheme::Get().textMuted;
			fs.fillX = true;
			fs.minSize = Vec2{ 0.0f, EditorTheme::Scaled(20.0f) };
			fs.runtimeOnly = true;
			root.children.push_back(std::move(fs));
		}

		// --- Separator ---
		{
			WidgetElement sep{};
			sep.type = WidgetElementType::Panel;
			sep.fillX = true;
			sep.minSize = Vec2{ 0.0f, 1.0f };
			sep.style.color = Vec4{ 0.3f, 0.3f, 0.35f, 0.6f };
			sep.runtimeOnly = true;
			root.children.push_back(std::move(sep));
		}

		// --- Section header: Channels ---
		{
			WidgetElement header{};
			header.type = WidgetElementType::Text;
			header.text = "Channels";
			header.font = EditorTheme::Get().fontDefault;
			header.fontSize = EditorTheme::Get().fontSizeBody;
			header.style.textColor = EditorTheme::Get().textSecondary;
			header.fillX = true;
			header.minSize = Vec2{ 0.0f, EditorTheme::Scaled(24.0f) };
			header.runtimeOnly = true;
			root.children.push_back(std::move(header));
		}

		// Channel toggle buttons (R, G, B, A) in a horizontal row
		{
			WidgetElement row{};
			row.type = WidgetElementType::StackPanel;
			row.orientation = StackOrientation::Horizontal;
			row.fillX = true;
			row.minSize = Vec2{ 0.0f, EditorTheme::Scaled(30.0f) };
			row.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
			row.spacing = 4.0f;
			row.runtimeOnly = true;

			auto makeChannelBtn = [&](const std::string& label, const std::string& btnId, const Vec4& color)
			{
				WidgetElement btn{};
				btn.type = WidgetElementType::Button;
				btn.id = btnId;
				btn.text = label;
				btn.font = EditorTheme::Get().fontDefault;
				btn.fontSize = EditorTheme::Get().fontSizeSmall;
				btn.minSize = Vec2{ EditorTheme::Scaled(40.0f), EditorTheme::Get().rowHeight };
				btn.style.color = color;
				btn.style.textColor = EditorTheme::Get().textPrimary;
				btn.style.hoverColor = Vec4{ color.x + 0.1f, color.y + 0.1f, color.z + 0.1f, color.w };
				btn.hitTestMode = HitTestMode::Enabled;
				btn.runtimeOnly = true;
				btn.style.borderRadius = 4.0f;
				return btn;
			};

			row.children.push_back(makeChannelBtn("R", "TexViewer.Ch.R", Vec4{ 0.6f, 0.15f, 0.15f, 1.0f }));
			row.children.push_back(makeChannelBtn("G", "TexViewer.Ch.G", Vec4{ 0.15f, 0.5f, 0.15f, 1.0f }));
			row.children.push_back(makeChannelBtn("B", "TexViewer.Ch.B", Vec4{ 0.15f, 0.15f, 0.6f, 1.0f }));
			row.children.push_back(makeChannelBtn("A", "TexViewer.Ch.A", Vec4{ 0.4f, 0.4f, 0.4f, 1.0f }));

			root.children.push_back(std::move(row));
		}

		// Checkerboard toggle
		{
			WidgetElement chkRow{};
			chkRow.type = WidgetElementType::StackPanel;
			chkRow.orientation = StackOrientation::Horizontal;
			chkRow.fillX = true;
			chkRow.minSize = Vec2{ 0.0f, EditorTheme::Scaled(26.0f) };
			chkRow.style.color = Vec4{ 0.0f, 0.0f, 0.0f, 0.0f };
			chkRow.runtimeOnly = true;

			WidgetElement chkBtn{};
			chkBtn.type = WidgetElementType::Button;
			chkBtn.id = "TexViewer.Checkerboard";
			chkBtn.text = "Checkerboard";
			chkBtn.font = EditorTheme::Get().fontDefault;
			chkBtn.fontSize = EditorTheme::Get().fontSizeSmall;
			chkBtn.fillX = true;
			chkBtn.minSize = Vec2{ 0.0f, EditorTheme::Get().rowHeight };
			chkBtn.style.color = EditorTheme::Get().inputBackground;
			chkBtn.style.textColor = EditorTheme::Get().textPrimary;
			chkBtn.style.hoverColor = EditorTheme::Get().inputBackgroundHover;
			chkBtn.hitTestMode = HitTestMode::Enabled;
			chkBtn.runtimeOnly = true;
			chkBtn.style.borderRadius = 4.0f;
			chkRow.children.push_back(std::move(chkBtn));

			root.children.push_back(std::move(chkRow));
		}

		elements.push_back(std::move(root));
		propsWidget->setElements(std::move(elements));

		m_uiManager.registerWidget("TextureViewerDetails." + assetPath, propsWidget, assetPath);
	}

	// Register channel toggle click events
	{
		const std::string ap = assetPath;
		const Vec4 colorR{ 0.6f, 0.15f, 0.15f, 1.0f };
		const Vec4 colorG{ 0.15f, 0.5f, 0.15f, 1.0f };
		const Vec4 colorB{ 0.15f, 0.15f, 0.6f, 1.0f };
		const Vec4 colorA{ 0.4f, 0.4f, 0.4f, 1.0f };
		const Vec4 grayedOut{ 0.2f, 0.2f, 0.2f, 0.5f };
		const Vec4 grayedHover{ 0.25f, 0.25f, 0.25f, 0.6f };
		const Vec4 grayedText{ 0.5f, 0.5f, 0.5f, 1.0f };
		const Vec4 activeText = EditorTheme::Get().textPrimary;

		auto updateChannelBtn = [this](const std::string& btnId, bool enabled, const Vec4& activeColor, const Vec4& gray, const Vec4& grayHov, const Vec4& grayTxt, const Vec4& activeTxt)
		{
			if (auto* el = m_uiManager.findElementById(btnId))
			{
				if (enabled)
				{
					el->style.color = activeColor;
					el->style.hoverColor = Vec4{ activeColor.x + 0.1f, activeColor.y + 0.1f, activeColor.z + 0.1f, activeColor.w };
					el->style.textColor = activeTxt;
				}
				else
				{
					el->style.color = gray;
					el->style.hoverColor = grayHov;
					el->style.textColor = grayTxt;
				}
			}
		};

		m_uiManager.registerClickEvent("TexViewer.Ch.R", [this, ap, colorR, grayedOut, grayedHover, grayedText, activeText, updateChannelBtn]()
		{
			auto* v = getTextureViewer(ap);
			if (v) { v->setChannelR(!v->isChannelR()); updateChannelBtn("TexViewer.Ch.R", v->isChannelR(), colorR, grayedOut, grayedHover, grayedText, activeText); m_uiManager.markAllWidgetsDirty(); }
		});
		m_uiManager.registerClickEvent("TexViewer.Ch.G", [this, ap, colorG, grayedOut, grayedHover, grayedText, activeText, updateChannelBtn]()
		{
			auto* v = getTextureViewer(ap);
			if (v) { v->setChannelG(!v->isChannelG()); updateChannelBtn("TexViewer.Ch.G", v->isChannelG(), colorG, grayedOut, grayedHover, grayedText, activeText); m_uiManager.markAllWidgetsDirty(); }
		});
		m_uiManager.registerClickEvent("TexViewer.Ch.B", [this, ap, colorB, grayedOut, grayedHover, grayedText, activeText, updateChannelBtn]()
		{
			auto* v = getTextureViewer(ap);
			if (v) { v->setChannelB(!v->isChannelB()); updateChannelBtn("TexViewer.Ch.B", v->isChannelB(), colorB, grayedOut, grayedHover, grayedText, activeText); m_uiManager.markAllWidgetsDirty(); }
		});
		m_uiManager.registerClickEvent("TexViewer.Ch.A", [this, ap, colorA, grayedOut, grayedHover, grayedText, activeText, updateChannelBtn]()
		{
			auto* v = getTextureViewer(ap);
			if (v) { v->setChannelA(!v->isChannelA()); updateChannelBtn("TexViewer.Ch.A", v->isChannelA(), colorA, grayedOut, grayedHover, grayedText, activeText); m_uiManager.markAllWidgetsDirty(); }
		});
		m_uiManager.registerClickEvent("TexViewer.Checkerboard", [this, ap]()
		{
			auto* v = getTextureViewer(ap);
			if (v)
			{
				v->setCheckerboard(!v->isCheckerboard());
				if (auto* el = m_uiManager.findElementById("TexViewer.Checkerboard"))
				{
					if (v->isCheckerboard())
					{
						el->style.color = EditorTheme::Get().inputBackground;
						el->style.textColor = EditorTheme::Get().textPrimary;
					}
					else
					{
						el->style.color = Vec4{ 0.2f, 0.2f, 0.2f, 0.5f };
						el->style.textColor = Vec4{ 0.5f, 0.5f, 0.5f, 1.0f };
					}
				}
				m_uiManager.markAllWidgetsDirty();
			}
		});
	}

	// Switch to the new tab
	setActiveTab(assetPath);

	m_uiManager.markAllWidgetsDirty();
	m_uiManager.showToastMessage(displayName + " ready", UIManager::kToastMedium);
	Logger::Instance().log(Logger::Category::Rendering,
		"Texture viewer opened: " + assetPath, Logger::LogLevel::INFO);
	return ptr;
}

void OpenGLRenderer::closeTextureViewer(const std::string& assetPath)
{
	// If this viewer's tab is active, switch back to Viewport first
	if (m_activeTabId == assetPath)
	{
		setActiveTab("Viewport");
	}

	// Destroy the viewer
	m_textureViewers.erase(assetPath);

	// Unregister the details panel widget
	m_uiManager.unregisterWidget("TextureViewerDetails." + assetPath);

	// Remove the tab button and close button from TitleBar
	{
		auto* tabsStack = m_uiManager.findElementById("TitleBar.Tabs");
		if (tabsStack)
		{
			const std::string tabBtnId = "TitleBar.Tab." + assetPath;
			const std::string closeBtnId = "TitleBar.TabClose." + assetPath;
			tabsStack->children.erase(
				std::remove_if(tabsStack->children.begin(), tabsStack->children.end(),
					[&](const WidgetElement& el) { return el.id == tabBtnId || el.id == closeBtnId; }),
				tabsStack->children.end());
		}
	}

	// Remove the editor tab and its FBO
	removeTab(assetPath);

	m_uiManager.markAllWidgetsDirty();
}

TextureViewerWindow* OpenGLRenderer::getTextureViewer(const std::string& assetPath)
{
	auto it = m_textureViewers.find(assetPath);
	return (it != m_textureViewers.end()) ? it->second.get() : nullptr;
}
#endif // ENGINE_EDITOR

void OpenGLRenderer::renderViewportUI()
{
	if (!m_viewportUIManager.isVisible())
	{
		return;
	}

	if (!m_viewportUIManager.hasWidgets())
	{
		return;
	}

	const Vec4 vpRect = m_viewportUIManager.getViewportRect();
	if (vpRect.z <= 0.0f || vpRect.w <= 0.0f)
	{
		return;
	}

	if (!m_textRenderer)
	{
		m_textRenderer = std::static_pointer_cast<OpenGLTextRenderer>(m_resourceManager.prepareTextRenderer());
	}

	if (!m_textRenderer || !ensureUIQuadRenderer())
	{
		return;
	}

	if (m_viewportUIManager.needsLayoutUpdate())
	{
		m_viewportUIManager.updateLayout([this](const std::string& text, float scale)
			{
				return m_textRenderer ? m_textRenderer->measureText(text, scale) : Vec2{};
			});
	}

	ensureUIShaderDefaults();

	const GLint viewportX = static_cast<GLint>(vpRect.x);
	const GLint viewportY = static_cast<GLint>(m_cachedWindowHeight - vpRect.y - vpRect.w);
	const GLsizei viewportW = static_cast<GLsizei>(vpRect.z);
	const GLsizei viewportH = static_cast<GLsizei>(vpRect.w);

	GLint previousViewport[4]{};
	glGetIntegerv(GL_VIEWPORT, previousViewport);

	const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
	const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
	const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	// Use the full FBO viewport (same as drawUIWidgetsToFramebuffer) to avoid
	// any driver-specific quirks with offset glViewport + text rendering.
	// Clip to the viewport area via scissor test.
	glEnable(GL_SCISSOR_TEST);
	glScissor(viewportX, viewportY, viewportW, viewportH);
	glViewport(0, 0, m_cachedWindowWidth, m_cachedWindowHeight);

	// Build a projection that maps viewport-local coordinates (0..vpW, 0..vpH)
	// to the correct pixel position within the full-size FBO. The negative offsets
	// shift the coordinate origin so that viewport-local (0,0) maps to
	// FBO pixel (vpRect.x, vpRect.y), i.e. the viewport top-left corner.
	// This matches the drawUIWidgetsToFramebuffer rendering path exactly, avoiding
	// driver-specific issues with offset glViewport + per-glyph text rendering.
	glm::mat4 uiProjection = glm::ortho(
		-vpRect.x,                                              // left
		static_cast<float>(m_cachedWindowWidth) - vpRect.x,     // right
		static_cast<float>(m_cachedWindowHeight) - vpRect.y,    // bottom
		-vpRect.y);                                             // top
	m_textRenderer->setScreenSize(m_cachedWindowWidth, m_cachedWindowHeight);
	m_textRenderer->setProjectionMatrix(uiProjection);


	UIRenderContext ctx;
	ctx.projection = uiProjection;
	ctx.screenHeight = m_cachedWindowHeight;
	ctx.debugEnabled = false;
	ctx.scissorOffset = Vec2{vpRect.x, vpRect.y};

	// Render all widgets sorted by z-order (ascending = back to front)
	for (const auto& entry : m_viewportUIManager.getSortedWidgets())
	{
		if (!entry.widget)
			continue;

		Vec2 widgetSize = entry.widget->getSizePixels();
		if (widgetSize.x <= 0.0f)
			widgetSize.x = vpRect.z;
		if (widgetSize.y <= 0.0f)
			widgetSize.y = vpRect.w;

		for (const auto& element : entry.widget->getElements())
		{
			renderWidgetElement(element, 0.0f, 0.0f, widgetSize.x, widgetSize.y, 1.0f, ctx);
		}
	}

	// --- Selection highlight (orange outline around selected element) ---
	{
		const std::string& selId = m_viewportUIManager.getSelectedElementId();
		if (!selId.empty())
		{
			// Find the element and its computed bounds
			const auto findBounds = [&](const auto& self, const WidgetElement& element,
				float parentX, float parentY, float parentW, float parentH,
				Vec4& outBounds) -> bool
			{
				const float ex = element.hasComputedPosition ? element.computedPositionPixels.x : (parentX + element.from.x * parentW);
				const float ey = element.hasComputedPosition ? element.computedPositionPixels.y : (parentY + element.from.y * parentH);
				const float ew = element.hasComputedSize ? element.computedSizePixels.x : (parentW * (element.to.x - element.from.x));
				const float eh = element.hasComputedSize ? element.computedSizePixels.y : (parentH * (element.to.y - element.from.y));

				if (element.id == selId)
				{
					outBounds = Vec4{ ex, ey, ew, eh };
					return true;
				}
				for (const auto& child : element.children)
				{
					if (self(self, child, ex, ey, ew, eh, outBounds))
						return true;
				}
				return false;
			};

			Vec4 bounds{};
			bool found = false;
			for (const auto& entry2 : m_viewportUIManager.getSortedWidgets())
			{
				if (!entry2.widget) continue;
				Vec2 ws = entry2.widget->getSizePixels();
				if (ws.x <= 0.0f) ws.x = vpRect.z;
				if (ws.y <= 0.0f) ws.y = vpRect.w;
				for (const auto& el : entry2.widget->getElements())
				{
					if (findBounds(findBounds, el, 0.0f, 0.0f, ws.x, ws.y, bounds))
					{
						found = true;
						break;
					}
				}
				if (found) break;
			}

			if (found)
			{
				const float bx = bounds.x;
				const float by = bounds.y;
				const float bw = bounds.z;
				const float bh = bounds.w;
				constexpr float t = 2.0f;
				const Vec4 hlColor{ 1.0f, 0.55f, 0.10f, 0.9f };
				const std::string& vp2 = resolveUIShaderPath("", m_defaultPanelVertex);
				const std::string& fp2 = resolveUIShaderPath("", m_defaultPanelFragment);
				const GLuint hlProg = getUIQuadProgram(vp2, fp2);
				// Top
				drawUIPanel(bx, by, bx + bw, by + t, hlColor, uiProjection, hlProg, hlColor, false);
				// Bottom
				drawUIPanel(bx, by + bh - t, bx + bw, by + bh, hlColor, uiProjection, hlProg, hlColor, false);
				// Left
				drawUIPanel(bx, by, bx + t, by + bh, hlColor, uiProjection, hlProg, hlColor, false);
				// Right
				drawUIPanel(bx + bw - t, by, bx + bw, by + bh, hlColor, uiProjection, hlProg, hlColor, false);
			}
		}
	}

	m_viewportUIManager.clearRenderDirty();

	if (!scissorEnabled)
	{
		glDisable(GL_SCISSOR_TEST);
	}
	if (!blendEnabled)
	{
		glDisable(GL_BLEND);
	}
	if (depthEnabled)
	{
		glEnable(GL_DEPTH_TEST);
	}
	glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
}

void OpenGLRenderer::renderUI()
{
#if ENGINE_EDITOR
	// Skip editor UI rendering while a DPI rebuild is in progress
	if (m_uiManager.isUIRenderingPaused())
		return;
#endif

	const uint64_t freq = SDL_GetPerformanceFrequency();
	const uint64_t uiStart = SDL_GetPerformanceCounter();
	const uint64_t drawStart = uiStart;
	glDisable(GL_DEPTH_TEST);

	if (!m_textRenderer)
	{
		m_textRenderer = std::static_pointer_cast<OpenGLTextRenderer>(m_resourceManager.prepareTextRenderer());
	}

	if (!m_textRenderer)
	{
		m_textQueue.clear();
		return;
	}

	int width = m_cachedWindowWidth;
	int height = m_cachedWindowHeight;
	if (width <= 0 || height <= 0)
	{
		SDL_GetWindowSizeInPixels(m_window, &width, &height);
	}
	m_textRenderer->setScreenSize(width, height);
	m_uiManager.setAvailableViewportSize(Vec2{ static_cast<float>(width), static_cast<float>(height) });
	m_currentRenderViewportSize = Vec2{ static_cast<float>(width), static_cast<float>(height) };

	const bool layoutDirty = m_uiManager.needsLayoutUpdate();
	if (layoutDirty)
	{
		m_uiManager.updateLayouts([this](const std::string& text, float scale)
			{
				return m_textRenderer ? m_textRenderer->measureText(text, scale) : Vec2{};
			});
	}

	// Cache the viewport content rect (area not covered by editor panels) for
	// use in renderWorld() on the next frame so projection uses the correct
	// aspect ratio and the scene is not distorted by editor panel sizes.
	m_cachedViewportContentRect = m_uiManager.getViewportContentRect();
	m_viewportUIManager.setViewportRect(
		m_cachedViewportContentRect.x,
		m_cachedViewportContentRect.y,
		m_cachedViewportContentRect.z,
		m_cachedViewportContentRect.w);

	const bool debugToggled = (m_uiDebugEnabled != m_uiDebugEnabledPrev);
	m_uiDebugEnabledPrev = m_uiDebugEnabled;

	const bool sizeChanged = (width != m_uiFboWidth || height != m_uiFboHeight);
	const bool needsRedraw = layoutDirty || m_uiManager.isRenderDirty() || debugToggled || sizeChanged || !m_uiFboCacheValid;

	const auto& ordered = m_uiManager.getWidgetsOrderedByZ();
	if (!ordered.empty() && ensureUIQuadRenderer() && ensureUiFbo(width, height))
	{
		if (needsRedraw)
		{
			m_uiManager.clearRenderDirty();

			glBindFramebuffer(GL_FRAMEBUFFER, m_uiFbo);
			glViewport(0, 0, width, height);
			glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			glEnable(GL_BLEND);
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		ensureUIShaderDefaults();
		glm::mat4 uiProjection = glm::ortho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);


		m_deferredDropDownsScratch.clear();
		UIRenderContext ctx;
		ctx.projection = uiProjection;
		ctx.screenHeight = height;
		ctx.debugEnabled = m_uiDebugEnabled;
		ctx.deferredDropdowns = &m_deferredDropDownsScratch;

		for (const auto* entry : ordered)
		{
			if (!entry || !entry->widget)
			{
				continue;
			}

			const auto& widget = entry->widget;
			Vec2 widgetSize = widget->getSizePixels();
			Vec2 widgetPosition{};
			if (widget->hasComputedSize())
			{
				widgetSize = widget->getComputedSizePixels();
			}
			if (widget->hasComputedPosition())
			{
				widgetPosition = widget->getComputedPositionPixels();
			}
			if (widgetSize.x <= 0.0f)
			{
				widgetSize.x = static_cast<float>(width);
			}
			if (widgetSize.y <= 0.0f)
			{
				widgetSize.y = static_cast<float>(height);
			}

			for (const auto& element : widget->getElements())
			{
				renderWidgetElement(element, widgetPosition.x, widgetPosition.y, widgetSize.x, widgetSize.y, 1.0f, ctx);
			}
		}

		// Deferred pass: draw expanded DropDown items on top of everything
		for (const auto& dd : m_deferredDropDownsScratch)
		{
			const std::string& vp = resolveUIShaderPath("", m_defaultPanelVertex);
			const std::string& fp = resolveUIShaderPath("", m_defaultPanelFragment);
			const GLuint prog = getUIQuadProgram(vp, fp);
			const float scale = dd.fontSize / 48.0f;
			const float heightPx = dd.y1 - dd.y0;
			const float itemHeight = std::max(EditorTheme::Scaled(20.0f), heightPx);
			const float contentX0 = dd.x0 + dd.padding.x;
			// Background panel behind all items
			if (!dd.items.empty())
			{
				const float totalH = static_cast<float>(dd.items.size()) * itemHeight;
				const auto& theme = EditorTheme::Get();
				drawUIShadow(dd.x0, dd.y1, dd.x1, dd.y1 + totalH, theme.shadowColor, theme.shadowOffset, uiProjection, prog, theme.borderRadius);
				drawUIPanel(dd.x0, dd.y1, dd.x1, dd.y1 + totalH, Vec4{0.12f,0.12f,0.15f,0.98f}, uiProjection, prog, Vec4{0,0,0,0}, false);
			}
			for (size_t i = 0; i < dd.items.size(); ++i)
			{
				const float iy0 = dd.y1 + static_cast<float>(i) * itemHeight;
				const float iy1 = iy0 + itemHeight;
				const bool isSelected = (static_cast<int>(i) == dd.selectedIndex);
				const Vec4 itemColor = isSelected
					? Vec4{ 0.22f, 0.22f, 0.28f, 0.98f }
					: Vec4{ 0.14f, 0.14f, 0.18f, 0.98f };
				drawUIPanel(dd.x0, iy0, dd.x1, iy1, itemColor, uiProjection, prog, dd.hoverColor, false);
				const Vec2 itemTextSize = m_textRenderer->measureText(dd.items[i], scale);
				const float itemTextY = iy0 + (itemHeight - itemTextSize.y) * 0.5f;
				m_textRenderer->drawText(dd.items[i], Vec2{ contentX0, itemTextY }, scale, dd.textColor);
			}
		}

		#if ENGINE_EDITOR
				// ---- Widget Editor FBO preview rendering ----
				{
					UIManager::WidgetEditorPreviewInfo previewInfo;
			if (m_uiManager.getWidgetEditorPreviewInfo(previewInfo) && previewInfo.editedWidget)
			{
				const auto& widget = previewInfo.editedWidget;
				Vec2 wSize = widget->getSizePixels();
				if (wSize.x <= 0.0f) wSize.x = 400.0f;
				if (wSize.y <= 0.0f) wSize.y = 300.0f;
				const int fboW = static_cast<int>(wSize.x);
				const int fboH = static_cast<int>(wSize.y);

				if (fboW > 0 && fboH > 0)
				{
					auto& previewFbo = m_widgetEditorPreviews[previewInfo.tabId];
					if (!previewFbo)
						previewFbo = std::make_unique<OpenGLRenderTarget>();
					previewFbo->resize(fboW, fboH);

					// Save current state
					const glm::mat4 savedProjection = uiProjection;

					// Render widget into preview FBO
					previewFbo->bind();
					glViewport(0, 0, fboW, fboH);
					glClearColor(0.18f, 0.19f, 0.23f, 1.0f);
					glClear(GL_COLOR_BUFFER_BIT);
					glEnable(GL_BLEND);
					glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

					uiProjection = glm::ortho(0.0f, static_cast<float>(fboW), static_cast<float>(fboH), 0.0f);
					m_textRenderer->setScreenSize(fboW, fboH);
					m_currentRenderViewportSize = Vec2{ static_cast<float>(fboW), static_cast<float>(fboH) };

					UIRenderContext previewCtx;
					previewCtx.projection = uiProjection;
					previewCtx.screenHeight = fboH;
					previewCtx.debugEnabled = false;
					for (const auto& element : widget->getElements())
					{
						renderWidgetElement(element, 0.0f, 0.0f, static_cast<float>(fboW), static_cast<float>(fboH), 1.0f, previewCtx);
					}

					// Helper to find an element by id in the widget tree
					const std::function<const WidgetElement*(const std::vector<WidgetElement>&, const std::string&)> findEl =
						[&](const std::vector<WidgetElement>& elements, const std::string& id) -> const WidgetElement*
					{
						for (const auto& el : elements)
						{
							if (el.id == id) return &el;
							if (!el.children.empty())
							{
								if (const auto* found = findEl(el.children, id))
									return found;
							}
						}
						return nullptr;
					};

					// Draw hover outline in the FBO (light blue, below selection)
					if (!previewInfo.hoveredElementId.empty() &&
						previewInfo.hoveredElementId != previewInfo.selectedElementId)
					{
						const WidgetElement* hoverEl = findEl(widget->getElements(), previewInfo.hoveredElementId);
						if (hoverEl && hoverEl->hasComputedPosition && hoverEl->hasComputedSize)
						{
							const std::string& vp2 = resolveUIShaderPath("", m_defaultPanelVertex);
							const std::string& fp2 = resolveUIShaderPath("", m_defaultPanelFragment);
							const GLuint outlineProg = getUIQuadProgram(vp2, fp2);
							const Vec4 hoverColor{ 0.3f, 0.7f, 1.0f, 0.7f };
							drawUIOutline(hoverEl->computedPositionPixels.x, hoverEl->computedPositionPixels.y,
								hoverEl->computedPositionPixels.x + hoverEl->computedSizePixels.x,
								hoverEl->computedPositionPixels.y + hoverEl->computedSizePixels.y,
								hoverColor, uiProjection, outlineProg);
						}
					}

					// Draw selection outline in the FBO (orange, on top of hover)
					if (!previewInfo.selectedElementId.empty())
					{
						const WidgetElement* selEl = findEl(widget->getElements(), previewInfo.selectedElementId);
						if (selEl && selEl->hasComputedPosition && selEl->hasComputedSize)
						{
							const std::string& vp2 = resolveUIShaderPath("", m_defaultPanelVertex);
							const std::string& fp2 = resolveUIShaderPath("", m_defaultPanelFragment);
							const GLuint outlineProg = getUIQuadProgram(vp2, fp2);
							const Vec4 outlineColor{ 1.0f, 0.6f, 0.0f, 0.9f };
							drawUIOutline(selEl->computedPositionPixels.x, selEl->computedPositionPixels.y,
								selEl->computedPositionPixels.x + selEl->computedSizePixels.x,
								selEl->computedPositionPixels.y + selEl->computedSizePixels.y,
								outlineColor, uiProjection, outlineProg);
						}
					}

					previewFbo->unbind();

					// Restore state — rebind main UI FBO
					uiProjection = savedProjection;
					m_textRenderer->setScreenSize(width, height);
					m_currentRenderViewportSize = Vec2{ static_cast<float>(width), static_cast<float>(height) };
					glBindFramebuffer(GL_FRAMEBUFFER, m_uiFbo);
					glViewport(0, 0, width, height);
					glEnable(GL_BLEND);
					glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

					// Draw the FBO texture as a quad in the canvas area
					Vec4 canvasRect{};
					if (m_uiManager.getWidgetEditorCanvasRect(canvasRect))
					{
						const float displayW = static_cast<float>(fboW) * previewInfo.zoom;
						const float displayH = static_cast<float>(fboH) * previewInfo.zoom;
						const float cx = canvasRect.x + canvasRect.z * 0.5f;
						const float cy = canvasRect.y + canvasRect.w * 0.5f;
						const float dx0 = cx - displayW * 0.5f + previewInfo.panOffset.x;
						const float dy0 = cy - displayH * 0.5f + previewInfo.panOffset.y;

						glEnable(GL_SCISSOR_TEST);
						glScissor(static_cast<int>(canvasRect.x),
							height - static_cast<int>(canvasRect.y + canvasRect.w),
							static_cast<int>(canvasRect.z),
							static_cast<int>(canvasRect.w));

						drawUIImage(dx0, dy0, dx0 + displayW, dy0 + displayH,
							previewFbo->getColorTextureId(), uiProjection,
							Vec4{ 1.0f, 1.0f, 1.0f, 1.0f }, false, true);

						glDisable(GL_SCISSOR_TEST);
					}

					m_uiManager.clearWidgetEditorPreviewDirty();
				}
			}
		}
#endif // ENGINE_EDITOR

			glDisable(GL_BLEND);
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, width, height);
			m_uiFboCacheValid = true;

			const uint64_t drawEnd = SDL_GetPerformanceCounter();
			m_cpuUiDrawMs = (freq > 0) ? (static_cast<double>(drawEnd - drawStart) * 1000.0 / static_cast<double>(freq)) : 0.0;
		}
		else
		{
			m_cpuUiDrawMs = 0.0;
		}

		blitUiCache(width, height);
	}
	else
	{
		m_cpuUiDrawMs = 0.0;
	}

	if (!m_textQueue.empty())
	{
		Vec2 viewportSize = m_uiManager.getAvailableViewportSize();
		if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
		{
			viewportSize = Vec2{ static_cast<float>(width), static_cast<float>(height) };
		}

		float contentLeft = 0.0f;
		float contentTop = 0.0f;
		float contentRight = viewportSize.x;
		float contentBottom = viewportSize.y;

		const auto& uiWidgets = m_uiManager.getRegisteredWidgets();
		for (const auto& entry : uiWidgets)
		{
			if (!entry.widget)
			{
				continue;
			}

			if (!entry.widget->hasComputedSize() || !entry.widget->hasComputedPosition())
			{
				continue;
			}

			const Vec2 widgetPos = entry.widget->getComputedPositionPixels();
			const Vec2 widgetSize = entry.widget->getComputedSizePixels();
			const bool spansWidth = widgetSize.x >= viewportSize.x * 0.8f;
			const bool spansHeight = widgetSize.y >= viewportSize.y * 0.5f;

			if (widgetPos.y <= 0.0f && spansWidth)
			{
				contentTop = std::max(contentTop, widgetPos.y + widgetSize.y);
			}
			if (widgetPos.x <= 0.0f && spansHeight)
			{
				contentLeft = std::max(contentLeft, widgetPos.x + widgetSize.x);
			}
			if (widgetPos.y + widgetSize.y >= viewportSize.y * 0.98f && spansWidth)
			{
				contentBottom = std::min(contentBottom, widgetPos.y);
			}
			if (widgetPos.x + widgetSize.x >= viewportSize.x * 0.98f && spansHeight)
			{
				contentRight = std::min(contentRight, widgetPos.x);
			}
		}

		const float contentWidth = std::max(0.0f, contentRight - contentLeft);
		const float contentHeight = std::max(0.0f, contentBottom - contentTop);

		m_textRenderer->setScreenSize(static_cast<int>(viewportSize.x), static_cast<int>(viewportSize.y));

		for (const auto& command : m_textQueue)
		{
			Vec2 pixelPos{
				contentLeft + command.screenPos.x * contentWidth,
				contentTop + command.screenPos.y * contentHeight
			};
			m_textRenderer->drawText(command.text, pixelPos, command.scale, command.color);
		}
	}
	m_textQueue.clear();

	// Render drag indicator when the user is dragging a content browser asset
	if (m_textRenderer && m_uiManager.isDragging())
	{
		const Vec2 mousePos = m_uiManager.getMousePosition();
		const std::string& payload = m_uiManager.getDragPayload();
		// Extract the label from payload ("typeInt|relPath" → stem of relPath)
		std::string dragLabel = "Dragging...";
		const auto sep = payload.find('|');
		if (sep != std::string::npos)
		{
			const std::string relPath = payload.substr(sep + 1);
			const auto lastSlash = relPath.find_last_of('/');
			const auto dot = relPath.find_last_of('.');
			if (dot != std::string::npos)
			{
				const size_t nameStart = (lastSlash != std::string::npos) ? lastSlash + 1 : 0;
				dragLabel = relPath.substr(nameStart, dot - nameStart);
			}
		}
		const Vec2 labelPos{ mousePos.x + 16.0f, mousePos.y - 8.0f };
		m_textRenderer->drawText(dragLabel, labelPos, 0.42f, Vec4{ 1.0f, 1.0f, 1.0f, 0.9f });
	}

	glEnable(GL_DEPTH_TEST);

	const uint64_t uiEnd = SDL_GetPerformanceCounter();
	m_cpuRenderUiMs = (freq > 0) ? (static_cast<double>(uiEnd - uiStart) * 1000.0 / static_cast<double>(freq)) : 0.0;
}

void OpenGLRenderer::moveCamera(float forward, float right, float up)
{
	if (!m_camera || m_cameraTransition.active || m_cameraPathActive)
		return;

	// Multi-viewport: route input to active sub-viewport camera if not the main one
#if ENGINE_EDITOR
	const int subCount = getSubViewportCount();
	if (subCount > 1 && m_activeSubViewport > 0 && m_activeSubViewport < kMaxSubViewports)
	{
		ensureSubViewportCameras();
		auto& cam = m_subViewportCameras[m_activeSubViewport];
		// Build a local front/right/up from yaw/pitch
		const float yawRad  = glm::radians(cam.yawDeg);
		const float pitchRad = glm::radians(cam.pitchDeg);
		glm::vec3 front;
		front.x = std::cos(pitchRad) * std::cos(yawRad);
		front.y = std::sin(pitchRad);
		front.z = std::cos(pitchRad) * std::sin(yawRad);
		front = glm::normalize(front);
		const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
		const glm::vec3 r = glm::normalize(glm::cross(front, worldUp));
		const glm::vec3 u = glm::normalize(glm::cross(r, front));
		cam.position.x += front.x * forward + r.x * right + u.x * up;
		cam.position.y += front.y * forward + r.y * right + u.y * up;
		cam.position.z += front.z * forward + r.z * right + u.z * up;
		return;
	}
#endif // ENGINE_EDITOR — multi-viewport routing

	m_camera->moveRelative(forward, right, up);
}

void OpenGLRenderer::queueText(const std::string& text, const Vec2& screenPos, float scale, const Vec4& color)
{
	TextCommand command;
	command.text = text;
	command.screenPos = screenPos;
	command.scale = scale;
	command.color = color;
	m_textQueue.push_back(std::move(command));
}

bool OpenGLRenderer::ensureHzbResources(int width, int height)
{
	if (m_hzbResourcesReady && m_hzbWidth == width && m_hzbHeight == height)
	{
		return true;
	}

	releaseHzbResources();

	m_hzbWidth = width;
	m_hzbHeight = height;
	m_hzbMipLevels = 1 + static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(width, height)))));

	// Depth texture to receive a copy of the default framebuffer depth
	glGenTextures(1, &m_hzbDepthTexture);
	glBindTexture(GL_TEXTURE_2D, m_hzbDepthTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

	glGenFramebuffers(1, &m_hzbDepthCopyFbo);
	glBindFramebuffer(GL_FRAMEBUFFER, m_hzbDepthCopyFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_hzbDepthTexture, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// R32F texture for HZB mip chain
	glGenTextures(1, &m_hzbTexture);
	glBindTexture(GL_TEXTURE_2D, m_hzbTexture);
	glTexStorage2D(GL_TEXTURE_2D, m_hzbMipLevels, GL_R32F, width, height);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// FBO for writing to HZB mip levels
	glGenFramebuffers(1, &m_hzbFbo);

	// Empty VAO for fullscreen triangle (positions generated from gl_VertexID)
	glGenVertexArrays(1, &m_hzbFullscreenVao);

	// Shared vertex shader for fullscreen triangle
	const std::string fullscreenVert =
		"#version 460 core\n"
		"out vec2 vTexCoord;\n"
		"void main() {\n"
		"    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
		"    vTexCoord = pos;\n"
		"    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";

	// Copy shader: reads depth texture, writes to R32F mip 0
	const std::string copyFrag =
		"#version 460 core\n"
		"uniform sampler2D uDepthTexture;\n"
		"in vec2 vTexCoord;\n"
		"out float FragColor;\n"
		"void main() {\n"
		"    FragColor = texture(uDepthTexture, vTexCoord).r;\n"
		"}\n";

	// Downsample shader: reads from previous HZB mip, writes max to current mip
	const std::string downsampleFrag =
		"#version 460 core\n"
		"uniform sampler2D uHzbTexture;\n"
		"uniform int uPrevMipLevel;\n"
		"in vec2 vTexCoord;\n"
		"out float FragColor;\n"
		"void main() {\n"
		"    ivec2 coord = ivec2(gl_FragCoord.xy) * 2;\n"
		"    ivec2 prevSize = textureSize(uHzbTexture, uPrevMipLevel);\n"
		"    ivec2 c00 = min(coord, prevSize - 1);\n"
		"    ivec2 c10 = min(coord + ivec2(1, 0), prevSize - 1);\n"
		"    ivec2 c01 = min(coord + ivec2(0, 1), prevSize - 1);\n"
		"    ivec2 c11 = min(coord + ivec2(1, 1), prevSize - 1);\n"
		"    float d0 = texelFetch(uHzbTexture, c00, uPrevMipLevel).r;\n"
		"    float d1 = texelFetch(uHzbTexture, c10, uPrevMipLevel).r;\n"
		"    float d2 = texelFetch(uHzbTexture, c01, uPrevMipLevel).r;\n"
		"    float d3 = texelFetch(uHzbTexture, c11, uPrevMipLevel).r;\n"
		"    FragColor = max(max(d0, d1), max(d2, d3));\n"
		"}\n";

	OpenGLShader vertShader;
	OpenGLShader copyFragShader;
	OpenGLShader downsampleFragShader;

	if (!vertShader.loadFromSource(Shader::Type::Vertex, fullscreenVert) ||
		!copyFragShader.loadFromSource(Shader::Type::Fragment, copyFrag) ||
		!downsampleFragShader.loadFromSource(Shader::Type::Fragment, downsampleFrag))
	{
		releaseHzbResources();
		return false;
	}

	auto linkProgram = [](GLuint vs, GLuint fs) -> GLuint
	{
		GLuint prog = glCreateProgram();
		glAttachShader(prog, vs);
		glAttachShader(prog, fs);
		glLinkProgram(prog);
		GLint linked = 0;
		glGetProgramiv(prog, GL_LINK_STATUS, &linked);
		if (!linked)
		{
			glDeleteProgram(prog);
			return 0;
		}
		return prog;
	};

	m_hzbCopyProgram = linkProgram(vertShader.id(), copyFragShader.id());
	m_hzbDownsampleProgram = linkProgram(vertShader.id(), downsampleFragShader.id());
	if (!m_hzbCopyProgram || !m_hzbDownsampleProgram)
	{
		releaseHzbResources();
		return false;
	}

	m_hzbPrevMipLoc = glGetUniformLocation(m_hzbDownsampleProgram, "uPrevMipLevel");
	m_hzbCopyDepthTexLoc = glGetUniformLocation(m_hzbCopyProgram, "uDepthTexture");
	m_hzbDownsampleTexLoc = glGetUniformLocation(m_hzbDownsampleProgram, "uHzbTexture");

	// Allocate CPU readback storage (only for mip levels >= kMinReadbackMip)
	constexpr int kMinReadbackMip = 3;
	const int readbackLevels = std::max(0, m_hzbMipLevels - kMinReadbackMip);
	m_hzbCpuData.resize(readbackLevels);

	// Create PBOs for async readback (double-buffered)
	size_t totalPixels = 0;
	for (int i = 0; i < readbackLevels; ++i)
	{
		const int mip = kMinReadbackMip + i;
		const int mipW = std::max(1, width >> mip);
		const int mipH = std::max(1, height >> mip);
		totalPixels += static_cast<size_t>(mipW) * mipH;
	}
	m_hzbPboSize = totalPixels * sizeof(float);
	if (m_hzbPboSize > 0)
	{
		glGenBuffers(kHzbPboCount, m_hzbPbos.data());
		for (int i = 0; i < kHzbPboCount; ++i)
		{
			glBindBuffer(GL_PIXEL_PACK_BUFFER, m_hzbPbos[i]);
			glBufferData(GL_PIXEL_PACK_BUFFER, static_cast<GLsizeiptr>(m_hzbPboSize), nullptr, GL_STREAM_READ);
		}
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}
	m_hzbPboIndex = 0;
	m_hzbPboReady = false;

	m_hzbResourcesReady = true;
	return true;
}

void OpenGLRenderer::releaseHzbResources()
{
	if (m_hzbCopyProgram)
	{
		glDeleteProgram(m_hzbCopyProgram);
		m_hzbCopyProgram = 0;
	}
	if (m_hzbDownsampleProgram)
	{
		glDeleteProgram(m_hzbDownsampleProgram);
		m_hzbDownsampleProgram = 0;
	}
	m_hzbPrevMipLoc = -1;
	m_hzbCopyDepthTexLoc = -1;
	m_hzbDownsampleTexLoc = -1;
	if (m_hzbFbo)
	{
		glDeleteFramebuffers(1, &m_hzbFbo);
		m_hzbFbo = 0;
	}
	if (m_hzbDepthCopyFbo)
	{
		glDeleteFramebuffers(1, &m_hzbDepthCopyFbo);
		m_hzbDepthCopyFbo = 0;
	}
	if (m_hzbTexture)
	{
		glDeleteTextures(1, &m_hzbTexture);
		m_hzbTexture = 0;
	}
	if (m_hzbDepthTexture)
	{
		glDeleteTextures(1, &m_hzbDepthTexture);
		m_hzbDepthTexture = 0;
	}
	if (m_hzbFullscreenVao)
	{
		glDeleteVertexArrays(1, &m_hzbFullscreenVao);
		m_hzbFullscreenVao = 0;
	}
	m_hzbCpuData.clear();
	for (auto& pbo : m_hzbPbos)
	{
		if (pbo)
		{
			glDeleteBuffers(1, &pbo);
			pbo = 0;
		}
	}
	m_hzbPboSize = 0;
	m_hzbPboIndex = 0;
	m_hzbPboReady = false;
	m_hzbWidth = 0;
	m_hzbHeight = 0;
	m_hzbMipLevels = 0;
	m_hzbResourcesReady = false;
}

bool OpenGLRenderer::ensureBoundsDebugResources()
{
	if (m_boundsDebug.program != 0 && m_boundsDebug.vao != 0 && m_boundsDebug.vbo != 0)
	{
		return true;
	}

	OpenGLShader vertex;
	OpenGLShader fragment;

	const std::string vertexSource =
		"#version 460 core\n"
		"layout(location = 0) in vec3 aPos;\n"
		"uniform mat4 uViewProj;\n"
		"uniform mat4 uModel;\n"
		"void main() {\n"
		"    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);\n"
		"}\n";

	const std::string fragmentSource =
		"#version 460 core\n"
		"out vec4 FragColor;\n"
		"uniform vec4 uColor;\n"
		"void main() {\n"
		"    FragColor = uColor;\n"
		"}\n";

	if (!vertex.loadFromSource(Shader::Type::Vertex, vertexSource) ||
		!fragment.loadFromSource(Shader::Type::Fragment, fragmentSource))
	{
		return false;
	}

	GLuint program = glCreateProgram();
	glAttachShader(program, vertex.id());
	glAttachShader(program, fragment.id());
	glLinkProgram(program);

	GLint linked = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		glDeleteProgram(program);
		return false;
	}

	std::vector<float> vertices;
	vertices.reserve(24 * 3);

	const glm::vec3 corners[8] = {
		glm::vec3(-1.0f, -1.0f, -1.0f),
		glm::vec3(1.0f, -1.0f, -1.0f),
		glm::vec3(1.0f, 1.0f, -1.0f),
		glm::vec3(-1.0f, 1.0f, -1.0f),
		glm::vec3(-1.0f, -1.0f, 1.0f),
		glm::vec3(1.0f, -1.0f, 1.0f),
		glm::vec3(1.0f, 1.0f, 1.0f),
		glm::vec3(-1.0f, 1.0f, 1.0f)
	};

	const int edges[24] = {
		0, 1, 1, 2, 2, 3, 3, 0,
		4, 5, 5, 6, 6, 7, 7, 4,
		0, 4, 1, 5, 2, 6, 3, 7
	};

	for (int i = 0; i < 24; ++i)
	{
		const glm::vec3& p = corners[edges[i]];
		vertices.push_back(p.x);
		vertices.push_back(p.y);
		vertices.push_back(p.z);
	}

	glGenVertexArrays(1, &m_boundsDebug.vao);
	glGenBuffers(1, &m_boundsDebug.vbo);
	glBindVertexArray(m_boundsDebug.vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_boundsDebug.vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	m_boundsDebug.program = program;
	m_boundsDebug.vertexCount = static_cast<GLsizei>(vertices.size() / 3);
	return true;
}

void OpenGLRenderer::releaseBoundsDebugResources()
{
	if (m_boundsDebug.program)
	{
		glDeleteProgram(m_boundsDebug.program);
		m_boundsDebug.program = 0;
	}
	if (m_boundsDebug.vbo)
	{
		glDeleteBuffers(1, &m_boundsDebug.vbo);
		m_boundsDebug.vbo = 0;
	}
	if (m_boundsDebug.vao)
	{
		glDeleteVertexArrays(1, &m_boundsDebug.vao);
		m_boundsDebug.vao = 0;
	}
	m_boundsDebug.vertexCount = 0;
}

// ---- HeightField Debug Wireframe ----

void OpenGLRenderer::rebuildHeightFieldDebugMesh()
{
	// Release previous mesh
	releaseHeightFieldDebugResources();

	auto& ecs = ECS::ECSManager::Instance();

	// Find the first entity with a HeightFieldComponent
	ECS::Schema hfSchema;
	hfSchema.require<ECS::HeightFieldComponent>();
	const auto hfEntities = ecs.getEntitiesMatchingSchema(hfSchema);
	ECS::Entity hfEntity = hfEntities.empty() ? 0 : hfEntities.front();
	if (hfEntity == 0)
	{
		return;
	}

	const auto* hfc = ecs.getComponent<ECS::HeightFieldComponent>(hfEntity);
	if (!hfc || hfc->sampleCount < 2 || hfc->heights.empty())
	{
		return;
	}

	const int sc = hfc->sampleCount;
	const auto expectedSize = static_cast<size_t>(sc) * sc;
	if (hfc->heights.size() < expectedSize)
	{
		return;
	}

	// Get entity transform offset
	glm::vec3 entityOffset(0.0f);
	if (const auto* tc = ecs.getComponent<ECS::TransformComponent>(hfEntity))
	{
		entityOffset = glm::vec3(tc->position[0], tc->position[1], tc->position[2]);
	}

	// Build world-space vertex positions from HeightFieldComponent data
	const int vertexCount = sc * sc;
	std::vector<float> vertices;
	vertices.reserve(static_cast<size_t>(vertexCount) * 3);

	for (int r = 0; r < sc; ++r)
	{
		for (int c = 0; c < sc; ++c)
		{
			const float x = hfc->offsetX + static_cast<float>(c) * hfc->scaleX + entityOffset.x;
			const float y = hfc->heights[r * sc + c] * hfc->scaleY + hfc->offsetY + entityOffset.y;
			const float z = hfc->offsetZ + static_cast<float>(r) * hfc->scaleZ + entityOffset.z;
			vertices.push_back(x);
			vertices.push_back(y);
			vertices.push_back(z);
		}
	}

	// Build line indices: horizontal + vertical grid lines
	std::vector<GLuint> indices;
	indices.reserve(static_cast<size_t>((sc - 1) * sc * 2 + sc * (sc - 1) * 2));

	// Horizontal lines (along columns for each row)
	for (int r = 0; r < sc; ++r)
	{
		for (int c = 0; c < sc - 1; ++c)
		{
			indices.push_back(static_cast<GLuint>(r * sc + c));
			indices.push_back(static_cast<GLuint>(r * sc + c + 1));
		}
	}
	// Vertical lines (along rows for each column)
	for (int c = 0; c < sc; ++c)
	{
		for (int r = 0; r < sc - 1; ++r)
		{
			indices.push_back(static_cast<GLuint>(r * sc + c));
			indices.push_back(static_cast<GLuint>((r + 1) * sc + c));
		}
	}

	if (indices.empty())
	{
		return;
	}

	glGenVertexArrays(1, &m_hfDebug.vao);
	glGenBuffers(1, &m_hfDebug.vbo);
	glGenBuffers(1, &m_hfDebug.ibo);

	glBindVertexArray(m_hfDebug.vao);

	glBindBuffer(GL_ARRAY_BUFFER, m_hfDebug.vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_hfDebug.ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	m_hfDebug.indexCount = static_cast<GLsizei>(indices.size());
	m_hfDebug.version = ecs.getComponentVersion();
}

void OpenGLRenderer::renderHeightFieldDebug(const glm::mat4& viewProj)
{
	if (!m_hfDebug.enabled)
	{
		return;
	}

	// Rebuild mesh when ECS data changes
	auto& ecs = ECS::ECSManager::Instance();
	if (m_hfDebug.vao == 0 || ecs.getComponentVersion() != m_hfDebug.version)
	{
		rebuildHeightFieldDebugMesh();
	}

	if (m_hfDebug.vao == 0 || m_hfDebug.indexCount == 0)
	{
		return;
	}

	// Reuse the bounds debug shader program
	if (!ensureBoundsDebugResources())
	{
		return;
	}

	const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);

	glUseProgram(m_boundsDebug.program);
	glBindVertexArray(m_hfDebug.vao);

	// Identity model matrix – vertices are already in world space
	const glm::mat4 identity(1.0f);

	const GLint viewProjLoc = glGetUniformLocation(m_boundsDebug.program, "uViewProj");
	if (viewProjLoc >= 0)
	{
		glUniformMatrix4fv(viewProjLoc, 1, GL_FALSE, &viewProj[0][0]);
	}
	const GLint modelLoc = glGetUniformLocation(m_boundsDebug.program, "uModel");
	if (modelLoc >= 0)
	{
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &identity[0][0]);
	}
	const GLint colorLoc = glGetUniformLocation(m_boundsDebug.program, "uColor");
	if (colorLoc >= 0)
	{
		glUniform4f(colorLoc, 0.1f, 0.85f, 0.3f, 1.0f); // green wireframe
	}

	glDrawElements(GL_LINES, m_hfDebug.indexCount, GL_UNSIGNED_INT, nullptr);

	glBindVertexArray(0);
	glUseProgram(0);
	if (cullWasEnabled)
	{
		glEnable(GL_CULL_FACE);
	}
}

void OpenGLRenderer::releaseHeightFieldDebugResources()
{
	if (m_hfDebug.ibo)
	{
		glDeleteBuffers(1, &m_hfDebug.ibo);
		m_hfDebug.ibo = 0;
	}
	if (m_hfDebug.vbo)
	{
		glDeleteBuffers(1, &m_hfDebug.vbo);
		m_hfDebug.vbo = 0;
	}
	if (m_hfDebug.vao)
	{
		glDeleteVertexArrays(1, &m_hfDebug.vao);
		m_hfDebug.vao = 0;
	}
	m_hfDebug.indexCount = 0;
}

// ---- Displacement Mapping (Tessellation) ----

bool OpenGLRenderer::ensureDisplacementResources()
{
	if (m_displacementProgram != 0)
		return true;

	auto& logger = Logger::Instance();
	const auto shaderDir = std::filesystem::current_path() / "shaders";

	OpenGLShader vertShader;
	OpenGLShader tescShader;
	OpenGLShader teseShader;
	OpenGLShader fragShader;

	if (!vertShader.loadFromFile(Shader::Type::Vertex, (shaderDir / "vertex.glsl").string()))
	{
		logger.log(Logger::Category::Rendering, "Displacement: failed to load vertex.glsl", Logger::LogLevel::ERROR);
		return false;
	}
	if (!tescShader.loadFromFile(Shader::Type::Hull, (shaderDir / "displacement_tesc.glsl").string()))
	{
		logger.log(Logger::Category::Rendering, "Displacement: failed to load displacement_tesc.glsl", Logger::LogLevel::ERROR);
		return false;
	}
	if (!teseShader.loadFromFile(Shader::Type::Domain, (shaderDir / "displacement_tese.glsl").string()))
	{
		logger.log(Logger::Category::Rendering, "Displacement: failed to load displacement_tese.glsl", Logger::LogLevel::ERROR);
		return false;
	}
	if (!fragShader.loadFromFile(Shader::Type::Fragment, (shaderDir / "fragment.glsl").string()))
	{
		logger.log(Logger::Category::Rendering, "Displacement: failed to load fragment.glsl", Logger::LogLevel::ERROR);
		return false;
	}

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vertShader.id());
	glAttachShader(prog, tescShader.id());
	glAttachShader(prog, teseShader.id());
	glAttachShader(prog, fragShader.id());
	glLinkProgram(prog);

	GLint linked = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		char buf[1024]{};
		glGetProgramInfoLog(prog, sizeof(buf), nullptr, buf);
		logger.log(Logger::Category::Rendering, std::string("Displacement program link error: ") + buf, Logger::LogLevel::ERROR);
		glDeleteProgram(prog);
		return false;
	}

	glDetachShader(prog, vertShader.id());
	glDetachShader(prog, tescShader.id());
	glDetachShader(prog, teseShader.id());
	glDetachShader(prog, fragShader.id());

	m_displacementProgram = prog;
	logger.log(Logger::Category::Rendering, "Displacement mapping: tessellation program built successfully.", Logger::LogLevel::INFO);
	return true;
}

void OpenGLRenderer::releaseDisplacementResources()
{
	if (m_displacementProgram)
	{
		glDeleteProgram(m_displacementProgram);
		m_displacementProgram = 0;
	}
}

// ---- Skybox ----

static const float kSkyboxVertices[] = {
	-1, 1,-1, -1,-1,-1, 1,-1,-1,  1,-1,-1,  1, 1,-1, -1, 1,-1,
	-1,-1, 1, -1,-1,-1, -1, 1,-1, -1, 1,-1, -1, 1, 1, -1,-1, 1,
	 1,-1,-1,  1,-1, 1,  1, 1, 1,  1, 1, 1,  1, 1,-1,  1,-1,-1,
	-1,-1, 1, -1, 1, 1,  1, 1, 1,  1, 1, 1,  1,-1, 1, -1,-1, 1,
	-1, 1,-1,  1, 1,-1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1, 1,-1,
	-1,-1,-1, -1,-1, 1,  1,-1,-1,  1,-1,-1, -1,-1, 1,  1,-1, 1
};

bool OpenGLRenderer::ensureSkyboxResources()
{
	if (m_skybox.program != 0)
		return true;

	const char* vs = R"(
#version 460 core
layout(location=0) in vec3 aPos;
out vec3 vTexCoord;
uniform mat4 uProjection;
uniform mat4 uView;
void main(){
	vTexCoord = aPos;
	vec4 pos = uProjection * uView * vec4(aPos, 1.0);
	gl_Position = pos.xyww;
})";
	const char* fs = R"(
#version 460 core
in vec3 vTexCoord;
out vec4 FragColor;
uniform samplerCube uSkybox;
void main(){
	FragColor = texture(uSkybox, vTexCoord);
})";

	GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vsh, 1, &vs, nullptr);
	glCompileShader(vsh);
	GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fsh, 1, &fs, nullptr);
	glCompileShader(fsh);
	m_skybox.program = glCreateProgram();
	glAttachShader(m_skybox.program, vsh);
	glAttachShader(m_skybox.program, fsh);
	glLinkProgram(m_skybox.program);
	glDeleteShader(vsh);
	glDeleteShader(fsh);

	m_skybox.locProjection = glGetUniformLocation(m_skybox.program, "uProjection");
	m_skybox.locView = glGetUniformLocation(m_skybox.program, "uView");
	m_skybox.locSampler = glGetUniformLocation(m_skybox.program, "uSkybox");

	glGenVertexArrays(1, &m_skybox.vao);
	glGenBuffers(1, &m_skybox.vbo);
	glBindVertexArray(m_skybox.vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_skybox.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(kSkyboxVertices), kSkyboxVertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glBindVertexArray(0);

	return true;
}

void OpenGLRenderer::releaseSkyboxResources()
{
	if (m_skybox.program) { glDeleteProgram(m_skybox.program); m_skybox.program = 0; }
	if (m_skybox.vbo)     { glDeleteBuffers(1, &m_skybox.vbo); m_skybox.vbo = 0; }
	if (m_skybox.vao)     { glDeleteVertexArrays(1, &m_skybox.vao); m_skybox.vao = 0; }
	if (m_skybox.cubemap) { glDeleteTextures(1, &m_skybox.cubemap); m_skybox.cubemap = 0; }
	m_skybox.loadedPath.clear();
}

bool OpenGLRenderer::loadSkyboxCubemap(const std::string& folderPath)
{
	if (folderPath.empty())
	{
		if (m_skybox.cubemap) { glDeleteTextures(1, &m_skybox.cubemap); m_skybox.cubemap = 0; }
		m_skybox.loadedPath.clear();
		return false;
	}
	if (folderPath == m_skybox.loadedPath && m_skybox.cubemap != 0)
		return true;

	// Each face slot has a list of alternative file names (e.g. top/up, bottom/down)
	// Standard cubemap order: +X, -X, +Y, -Y, +Z, -Z
	struct FaceSlot { std::vector<std::string> names; GLenum target; };
	const FaceSlot faceSlots[6] = {
		{ { "right" },          GL_TEXTURE_CUBE_MAP_POSITIVE_X },
		{ { "left" },           GL_TEXTURE_CUBE_MAP_NEGATIVE_X },
		{ { "top", "up" },     GL_TEXTURE_CUBE_MAP_POSITIVE_Y },
		{ { "bottom", "down" },GL_TEXTURE_CUBE_MAP_NEGATIVE_Y },
		{ { "front" },          GL_TEXTURE_CUBE_MAP_NEGATIVE_Z },
		{ { "back" },           GL_TEXTURE_CUBE_MAP_POSITIVE_Z }
	};
	const std::string extensions[3] = { ".jpg", ".png", ".bmp" };

	if (m_skybox.cubemap) { glDeleteTextures(1, &m_skybox.cubemap); m_skybox.cubemap = 0; }

	glGenTextures(1, &m_skybox.cubemap);
	glBindTexture(GL_TEXTURE_CUBE_MAP, m_skybox.cubemap);

	auto& assetMgr = AssetManager::Instance();
	bool allLoaded = true;

	for (int i = 0; i < 6; ++i)
	{
		bool loaded = false;
		for (const auto& name : faceSlots[i].names)
		{
			for (const auto& ext : extensions)
			{
				const std::string filePath = (std::filesystem::path(folderPath) / (name + ext)).string();
				if (!std::filesystem::exists(filePath))
					continue;
				int w = 0, h = 0, ch = 0;
				unsigned char* data = assetMgr.loadRawImageData(filePath, w, h, ch);
				if (data && w > 0 && h > 0)
				{
					GLenum format = (ch == 4) ? GL_RGBA : GL_RGB;
					glTexImage2D(faceSlots[i].target, 0, GL_RGBA8, w, h, 0, format, GL_UNSIGNED_BYTE, data);
					assetMgr.freeRawImageData(data);
					loaded = true;
					break;
				}
				if (data) assetMgr.freeRawImageData(data);
			}
			if (loaded) break;
		}
		if (!loaded)
		{
			Logger::Instance().log(Logger::Category::Rendering,
				"Skybox: missing face '" + faceSlots[i].names.front() + "' in " + folderPath, Logger::LogLevel::WARNING);
			allLoaded = false;
		}
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	if (!allLoaded)
	{
		glDeleteTextures(1, &m_skybox.cubemap);
		m_skybox.cubemap = 0;
		return false;
	}

	return true;
}

void OpenGLRenderer::setSkyboxPath(const std::string& pathOrFolder)
{
	if (pathOrFolder.empty())
	{
		loadSkyboxCubemap("");
		m_skybox.loadedPath.clear();
		return;
	}

	if (pathOrFolder == m_skybox.loadedPath && m_skybox.cubemap != 0)
		return;

	// If the path ends with .asset, resolve to the folder path from the asset data
	if (pathOrFolder.size() > 6 && pathOrFolder.substr(pathOrFolder.size() - 6) == ".asset")
	{
		auto& assetMgr = AssetManager::Instance();
		const std::string absPath = assetMgr.getAbsoluteContentPath(pathOrFolder);
		if (absPath.empty())
		{
			Logger::Instance().log(Logger::Category::Rendering,
				"Skybox: failed to resolve asset path '" + pathOrFolder + "'", Logger::LogLevel::WARNING);
			loadSkyboxCubemap("");
			m_skybox.loadedPath.clear();
			return;
		}

		std::ifstream in(absPath, std::ios::binary);
		if (!in.is_open())
		{
			Logger::Instance().log(Logger::Category::Rendering,
				"Skybox: cannot open asset file '" + absPath + "'", Logger::LogLevel::WARNING);
			loadSkyboxCubemap("");
			m_skybox.loadedPath.clear();
			return;
		}

		json fileJson;
		try { fileJson = json::parse(in); } catch (...) { loadSkyboxCubemap(""); m_skybox.loadedPath.clear(); return; }
		in.close();

		if (!fileJson.is_object() || !fileJson.contains("data"))
		{
			loadSkyboxCubemap("");
			m_skybox.loadedPath.clear();
			return;
		}
		const auto& data = fileJson.at("data");

		// Try folderPath first (absolute or project-relative)
		if (data.is_object() && data.contains("folderPath") && data.at("folderPath").is_string())
		{
			const std::string folder = data.at("folderPath").get<std::string>();
			auto& diagnostics = DiagnosticsManager::Instance();
			const std::string absFolder = (std::filesystem::path(diagnostics.getProjectInfo().projectPath) / folder).lexically_normal().string();
			if (loadSkyboxCubemap(absFolder))
			{
				m_skybox.loadedPath = pathOrFolder;
			}
			else
			{
				Logger::Instance().log(Logger::Category::Rendering,
					"Skybox: failed to load cubemap from '" + absFolder + "'", Logger::LogLevel::WARNING);
				m_skybox.loadedPath.clear();
			}
			return;
		}

		// Fallback: try to resolve individual face paths
		if (data.is_object() && data.contains("faces") && data.at("faces").is_object())
		{
			const auto& faces = data.at("faces");
			for (const auto& [key, val] : faces.items())
			{
				if (val.is_string() && !val.get<std::string>().empty())
				{
					std::filesystem::path facePath(val.get<std::string>());
					std::string folder = facePath.parent_path().string();
					std::string absFolder = assetMgr.getAbsoluteContentPath(folder);
					if (!absFolder.empty())
					{
						absFolder = std::filesystem::path(absFolder).lexically_normal().string();
						if (loadSkyboxCubemap(absFolder))
						{
							m_skybox.loadedPath = pathOrFolder;
						}
						else
						{
							m_skybox.loadedPath.clear();
						}
						return;
					}
				}
			}
		}

		loadSkyboxCubemap("");
		m_skybox.loadedPath.clear();
		return;
	}

	// Direct folder path — try as-is first, then resolve via Content path
	std::string resolvedFolder = std::filesystem::path(pathOrFolder).lexically_normal().string();
	if (loadSkyboxCubemap(resolvedFolder))
	{
		m_skybox.loadedPath = pathOrFolder;
		return;
	}

	// Path may be content-relative — resolve through AssetManager
	const std::string contentResolved = AssetManager::Instance().getAbsoluteContentPath(pathOrFolder);
	if (!contentResolved.empty() && loadSkyboxCubemap(std::filesystem::path(contentResolved).lexically_normal().string()))
	{
		m_skybox.loadedPath = pathOrFolder;
	}
	else
	{
		Logger::Instance().log(Logger::Category::Rendering,
			"Skybox: failed to load from folder '" + pathOrFolder + "'", Logger::LogLevel::WARNING);
		m_skybox.loadedPath.clear();
	}
}

void OpenGLRenderer::renderSkybox(const glm::mat4& view, const glm::mat4& projection)
{
	if (m_skybox.cubemap == 0 || !ensureSkyboxResources())
		return;

	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);

	glUseProgram(m_skybox.program);
	// Strip translation from view matrix
	glm::mat4 skyView = glm::mat4(glm::mat3(view));
	glUniformMatrix4fv(m_skybox.locProjection, 1, GL_FALSE, &projection[0][0]);
	glUniformMatrix4fv(m_skybox.locView, 1, GL_FALSE, &skyView[0][0]);
	glUniform1i(m_skybox.locSampler, 0);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, m_skybox.cubemap);

	glBindVertexArray(m_skybox.vao);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);

	glDepthMask(GL_TRUE);
	glDepthFunc(GL_LESS);
}

// ---- GPU Instanced Rendering (SSBO) ----

void OpenGLRenderer::uploadInstanceData(const glm::mat4* data, size_t count)
{
	if (m_instanceSSBO == 0)
	{
		glGenBuffers(1, &m_instanceSSBO);
	}
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_instanceSSBO);
	const GLsizeiptr bytes = static_cast<GLsizeiptr>(count * sizeof(glm::mat4));
	if (count > m_instanceSSBOCapacity)
	{
		m_instanceSSBOCapacity = std::max(count, m_instanceSSBOCapacity * 2);
	}
	// Orphan the entire buffer before writing to prevent GPU read/write hazards.
	// glBufferData(nullptr) tells the driver to allocate new storage so the GPU
	// can safely finish reading the previous contents.
	glBufferData(GL_SHADER_STORAGE_BUFFER,
				 static_cast<GLsizeiptr>(m_instanceSSBOCapacity * sizeof(glm::mat4)),
				 nullptr, GL_DYNAMIC_DRAW);
	glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bytes, data);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_instanceSSBO);
}

void OpenGLRenderer::releaseInstanceResources()
{
	if (m_instanceSSBO)
	{
		glDeleteBuffers(1, &m_instanceSSBO);
		m_instanceSSBO = 0;
	}
	m_instanceSSBOCapacity = 0;
	m_instanceMatrixBuffer.clear();
	m_instanceMatrixBuffer.shrink_to_fit();
}

// ---- Order-Independent Transparency (Weighted Blended OIT) ----

bool OpenGLRenderer::ensureOitResources(int width, int height)
{
	if (width <= 0 || height <= 0)
		return false;

	// Resize existing FBO if dimensions changed
	if (m_oit.fbo != 0 && (m_oit.width != width || m_oit.height != height))
	{
		releaseOitResources();
	}

	if (m_oit.fbo != 0)
		return true;

	m_oit.width = width;
	m_oit.height = height;

	// Accumulation texture (RGBA16F) – additive blend of premultiplied colour * weight
	glGenTextures(1, &m_oit.accumTex);
	glBindTexture(GL_TEXTURE_2D, m_oit.accumTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// Revealage texture (R8) – product of (1 – alpha)
	glGenTextures(1, &m_oit.revealageTex);
	glBindTexture(GL_TEXTURE_2D, m_oit.revealageTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// Depth RBO (shared with opaque pass via blit)
	glGenRenderbuffers(1, &m_oit.depthRbo);
	glBindRenderbuffer(GL_RENDERBUFFER, m_oit.depthRbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);

	glGenFramebuffers(1, &m_oit.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, m_oit.fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_oit.accumTex, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_oit.revealageTex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_oit.depthRbo);

	GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers(2, drawBuffers);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		Logger::Instance().log(Logger::Category::Rendering,
			"OIT: FBO incomplete", Logger::LogLevel::ERROR);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		releaseOitResources();
		return false;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Compile composite program (uses resolve_vertex.glsl for fullscreen triangle)
	if (m_oit.compositeProgram == 0)
	{
		const std::filesystem::path shadersDir = std::filesystem::current_path() / "shaders";
		const std::string vertPath = (shadersDir / "resolve_vertex.glsl").string();
		const std::string fragPath = (shadersDir / "oit_composite_fragment.glsl").string();

		OpenGLShader vertShader;
		OpenGLShader fragShader;
		if (!vertShader.loadFromFile(Shader::Type::Vertex, vertPath) ||
			!fragShader.loadFromFile(Shader::Type::Fragment, fragPath))
		{
			Logger::Instance().log(Logger::Category::Rendering,
				"OIT: failed to load composite shaders", Logger::LogLevel::ERROR);
			return false;
		}

		m_oit.compositeProgram = glCreateProgram();
		glAttachShader(m_oit.compositeProgram, vertShader.id());
		glAttachShader(m_oit.compositeProgram, fragShader.id());
		glLinkProgram(m_oit.compositeProgram);

		GLint linked = 0;
		glGetProgramiv(m_oit.compositeProgram, GL_LINK_STATUS, &linked);
		if (!linked)
		{
			Logger::Instance().log(Logger::Category::Rendering,
				"OIT: composite program link failed", Logger::LogLevel::ERROR);
			glDeleteProgram(m_oit.compositeProgram);
			m_oit.compositeProgram = 0;
			return false;
		}

		m_oit.compLocAccum = glGetUniformLocation(m_oit.compositeProgram, "uAccumTexture");
		m_oit.compLocRevealage = glGetUniformLocation(m_oit.compositeProgram, "uRevealageTexture");
	}

	return true;
}

void OpenGLRenderer::releaseOitResources()
{
	if (m_oit.fbo)           { glDeleteFramebuffers(1, &m_oit.fbo);       m_oit.fbo = 0; }
	if (m_oit.accumTex)      { glDeleteTextures(1, &m_oit.accumTex);      m_oit.accumTex = 0; }
	if (m_oit.revealageTex)  { glDeleteTextures(1, &m_oit.revealageTex);  m_oit.revealageTex = 0; }
	if (m_oit.depthRbo)      { glDeleteRenderbuffers(1, &m_oit.depthRbo); m_oit.depthRbo = 0; }
	if (m_oit.compositeProgram) { glDeleteProgram(m_oit.compositeProgram); m_oit.compositeProgram = 0; }
	if (m_oit.compositeVao)  { glDeleteVertexArrays(1, &m_oit.compositeVao); m_oit.compositeVao = 0; }
	m_oit.width = 0;
	m_oit.height = 0;
}

void OpenGLRenderer::renderOitTransparentPass(const glm::mat4& view,
	const glm::vec3& lightPosition, const glm::vec3& lightColor, float lightIntensity,
	const glm::vec3& fogColor, int debugMode, float activeNear, float activeFar)
{
	if (m_transparentDrawList.empty())
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, m_oit.fbo);

	// Clear accumulation to (0,0,0,0) and revealage to 1.0
	const GLfloat clearAccum[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	const GLfloat clearReveal[] = { 1.0f, 0.0f, 0.0f, 0.0f };
	glClearBufferfv(GL_COLOR, 0, clearAccum);
	glClearBufferfv(GL_COLOR, 1, clearReveal);

	// Depth test ON but depth write OFF – transparent objects read opaque depth
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);

	// Per-attachment blending:
	// Attachment 0 (accumulation): additive
	// Attachment 1 (revealage):    multiply by (1 - src_alpha)
	glEnable(GL_BLEND);
	glBlendFunci(0, GL_ONE, GL_ONE);
	glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);

	// Render all transparent objects with OIT enabled
	size_t di = 0;
	while (di < m_transparentDrawList.size())
	{
		const auto& first = m_transparentDrawList[di];

		first.obj->setMatrices(first.modelMatrix, view, m_projectionMatrix);
		first.obj->setShadowData(m_shadow.depthArray, m_shadow.lightSpaceMatrices, m_shadow.lightIndices, m_shadow.count);
		first.obj->setPointShadowData(m_pointShadow.cubeArray, m_pointShadow.positions, m_pointShadow.farPlanes, m_pointShadow.lightIndices, m_pointShadow.count);
		first.obj->setFogData(m_fogEnabled, fogColor, m_fogParams.z);
		first.obj->setCsmData(m_csm.depthArray, m_csm.matrices, m_csm.splits, m_csm.lightIndex, m_csm.enabled, view);
		first.obj->setLightData(lightPosition, lightColor, lightIntensity);
		first.obj->setLights(m_sceneLights);
		first.obj->setDebugMode(debugMode);
		first.obj->setNearFarPlanes(activeNear, activeFar);

		// Enable OIT mode on the material
		if (first.material)
			first.material->setOitEnabled(true);

		// Find batch
		size_t batchEnd = di + 1;
		while (batchEnd < m_transparentDrawList.size() &&
			   m_transparentDrawList[batchEnd].obj == first.obj &&
			   m_transparentDrawList[batchEnd].material == first.material)
		{
			++batchEnd;
		}
		const size_t batchSize = batchEnd - di;

		if (batchSize > 1 && first.material)
		{
			m_instanceMatrixBuffer.clear();
			for (size_t j = di; j < batchEnd; ++j)
				m_instanceMatrixBuffer.push_back(m_transparentDrawList[j].modelMatrix);
			uploadInstanceData(m_instanceMatrixBuffer.data(), batchSize);
			first.material->renderInstanced(static_cast<int>(batchSize));
		}
		else
		{
			first.obj->render();
		}

		// Disable OIT mode
		if (first.material)
			first.material->setOitEnabled(false);

		di = batchEnd;
	}

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void OpenGLRenderer::compositeOit(GLuint dstFbo, int vpX, int vpY, int vpW, int vpH)
{
	if (m_transparentDrawList.empty() || m_oit.compositeProgram == 0)
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
	glViewport(vpX, vpY, vpW, vpH);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST);

	glUseProgram(m_oit.compositeProgram);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_oit.accumTex);
	if (m_oit.compLocAccum >= 0) glUniform1i(m_oit.compLocAccum, 0);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, m_oit.revealageTex);
	if (m_oit.compLocRevealage >= 0) glUniform1i(m_oit.compLocRevealage, 1);

	// Fullscreen triangle – vertex positions generated from gl_VertexID in resolve_vertex.glsl
	if (m_oit.compositeVao == 0)
		glGenVertexArrays(1, &m_oit.compositeVao);
	glBindVertexArray(m_oit.compositeVao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);

	glUseProgram(0);
	glActiveTexture(GL_TEXTURE0);
	glEnable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
}

// ---- Shadow Mapping ----

bool OpenGLRenderer::ensureShadowResources()
{
	if (m_shadow.program != 0)
		return true;

	// Compile shadow depth shader (supports optional skeletal skinning)
	const char* vs = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 5) in vec4 aBoneIdsF;
layout(location = 6) in vec4 aBoneWeights;
layout(std430, binding = 0) buffer InstanceModelMatrices {
	mat4 instanceModels[];
};
uniform mat4 uLightSpaceMatrix;
uniform mat4 uModel;
uniform bool uInstanced;
uniform bool uSkinned;
#define MAX_BONES 128
uniform mat4 uBoneMatrices[MAX_BONES];
void main() {
	mat4 model;
	if (uInstanced) {
		model = instanceModels[gl_InstanceID];
	} else {
		model = uModel;
	}
	vec4 localPos;
	if (uSkinned) {
		ivec4 boneIds = ivec4(aBoneIdsF);
		mat4 boneTransform = mat4(0.0);
		for (int i = 0; i < 4; ++i) {
			if (boneIds[i] >= 0 && boneIds[i] < MAX_BONES) {
				boneTransform += uBoneMatrices[boneIds[i]] * aBoneWeights[i];
			}
		}
		localPos = boneTransform * vec4(aPos, 1.0);
	} else {
		localPos = vec4(aPos, 1.0);
	}
	gl_Position = uLightSpaceMatrix * model * localPos;
}
)";
	const char* fs = R"(
#version 460 core
void main() {}
)";

	GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vsh, 1, &vs, nullptr);
	glCompileShader(vsh);
	GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fsh, 1, &fs, nullptr);
	glCompileShader(fsh);
	m_shadow.program = glCreateProgram();
	glAttachShader(m_shadow.program, vsh);
	glAttachShader(m_shadow.program, fsh);
	glLinkProgram(m_shadow.program);
	glDeleteShader(vsh);
	glDeleteShader(fsh);

	m_shadow.locModel = glGetUniformLocation(m_shadow.program, "uModel");
	m_shadow.locLightSpace = glGetUniformLocation(m_shadow.program, "uLightSpaceMatrix");
	m_shadow.locInstanced = glGetUniformLocation(m_shadow.program, "uInstanced");
	m_shadow.locSkinned = glGetUniformLocation(m_shadow.program, "uSkinned");
	m_shadow.locBoneMatrices = glGetUniformLocation(m_shadow.program, "uBoneMatrices[0]");

	// Create shadow FBO + depth texture array (one layer per shadow-casting light)
	glGenTextures(1, &m_shadow.depthArray);
	glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadow.depthArray);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
				 kShadowMapSize, kShadowMapSize, kMaxShadowLights,
				 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE);

	glGenFramebuffers(1, &m_shadow.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, m_shadow.fbo);
	glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadow.depthArray, 0, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return true;
}

void OpenGLRenderer::releaseShadowResources()
{
	if (m_shadow.program)    { glDeleteProgram(m_shadow.program); m_shadow.program = 0; }
	if (m_shadow.depthArray) { glDeleteTextures(1, &m_shadow.depthArray); m_shadow.depthArray = 0; }
	if (m_shadow.fbo)        { glDeleteFramebuffers(1, &m_shadow.fbo); m_shadow.fbo = 0; }
}

// ---- Cascaded Shadow Maps ----

bool OpenGLRenderer::ensureCsmResources()
{
	if (m_csm.fbo != 0)
		return true;

	glGenTextures(1, &m_csm.depthArray);
	glBindTexture(GL_TEXTURE_2D_ARRAY, m_csm.depthArray);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
				 kCsmMapSize, kCsmMapSize, kNumCsmCascades,
				 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE);

	glGenFramebuffers(1, &m_csm.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, m_csm.fbo);
	glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_csm.depthArray, 0, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return true;
}

void OpenGLRenderer::releaseCsmResources()
{
	if (m_csm.depthArray) { glDeleteTextures(1, &m_csm.depthArray); m_csm.depthArray = 0; }
	if (m_csm.fbo)        { glDeleteFramebuffers(1, &m_csm.fbo); m_csm.fbo = 0; }
}

void OpenGLRenderer::computeCsmMatrices(const OpenGLMaterial::LightData& light,
										const glm::mat4& view, const glm::mat4& proj,
										float nearPlane, float farPlane)
{
	const glm::vec3 lightDir = glm::normalize(light.direction);

	// Choose stable up vector
	glm::vec3 up(0.0f, 1.0f, 0.0f);
	if (std::abs(glm::dot(lightDir, up)) > 0.99f)
		up = glm::vec3(0.0f, 0.0f, 1.0f);

	// Compute cascade split distances (practical split scheme: blend of log and uniform)
	constexpr float lambda = 0.75f;
	float splits[kNumCsmCascades];
	for (int i = 0; i < kNumCsmCascades; ++i)
	{
		float p = static_cast<float>(i + 1) / static_cast<float>(kNumCsmCascades);
		float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
		float uniSplit = nearPlane + (farPlane - nearPlane) * p;
		splits[i] = lambda * logSplit + (1.0f - lambda) * uniSplit;
		m_csm.splits[i] = splits[i];
	}

	// Inverse view-projection to go from NDC back to world space
	const glm::mat4 invViewProj = glm::inverse(proj * view);

	for (int c = 0; c < kNumCsmCascades; ++c)
	{
		float cascadeNear = (c == 0) ? nearPlane : splits[c - 1];
		float cascadeFar = splits[c];

		// Map near/far to NDC z (perspective projection)
		// NDC_z = (2*near*far / (far-near)) / depth + (far+near)/(far-near)
		// Simpler: use linearized approach — project corners at cascadeNear/cascadeFar
		float nearNdc = (cascadeNear - nearPlane) / (farPlane - nearPlane) * 2.0f - 1.0f;
		float farNdc  = (cascadeFar  - nearPlane) / (farPlane - nearPlane) * 2.0f - 1.0f;

		// Use proper perspective z mapping
		// z_ndc = (far+near)/(far-near) + (2*far*near)/((far-near)*z_eye) ... but this is non-linear
		// Better approach: just compute the 8 frustum corners directly
		// Frustum corners in NDC: (-1,-1,-1) to (1,1,1), we remap z for the sub-frustum

		// Build sub-projection that covers [cascadeNear, cascadeFar]
		// Extract FOV and aspect from projection matrix
		float tanHalfFovY = 1.0f / proj[1][1];
		float aspect = proj[1][1] / proj[0][0];

		// Compute frustum corners in view space
		float nearH = cascadeNear * tanHalfFovY;
		float nearW = nearH * aspect;
		float farH  = cascadeFar * tanHalfFovY;
		float farW  = farH * aspect;

		glm::mat4 invView = glm::inverse(view);

		glm::vec3 corners[8] = {
			// Near plane (z = -cascadeNear in view space)
			glm::vec3(invView * glm::vec4(-nearW, -nearH, -cascadeNear, 1.0f)),
			glm::vec3(invView * glm::vec4( nearW, -nearH, -cascadeNear, 1.0f)),
			glm::vec3(invView * glm::vec4( nearW,  nearH, -cascadeNear, 1.0f)),
			glm::vec3(invView * glm::vec4(-nearW,  nearH, -cascadeNear, 1.0f)),
			// Far plane (z = -cascadeFar in view space)
			glm::vec3(invView * glm::vec4(-farW, -farH, -cascadeFar, 1.0f)),
			glm::vec3(invView * glm::vec4( farW, -farH, -cascadeFar, 1.0f)),
			glm::vec3(invView * glm::vec4( farW,  farH, -cascadeFar, 1.0f)),
			glm::vec3(invView * glm::vec4(-farW,  farH, -cascadeFar, 1.0f)),
		};

		// Compute center of the frustum slice
		glm::vec3 center(0.0f);
		for (int i = 0; i < 8; ++i)
			center += corners[i];
		center /= 8.0f;

		// Light view matrix looking at the center
		const glm::mat4 lightView = glm::lookAt(center - lightDir * 50.0f, center, up);

		// Find bounding box in light space
		float minX =  std::numeric_limits<float>::max();
		float maxX = -std::numeric_limits<float>::max();
		float minY =  std::numeric_limits<float>::max();
		float maxY = -std::numeric_limits<float>::max();
		float minZ =  std::numeric_limits<float>::max();
		float maxZ = -std::numeric_limits<float>::max();

		for (int i = 0; i < 8; ++i)
		{
			glm::vec4 lsCorner = lightView * glm::vec4(corners[i], 1.0f);
			minX = std::min(minX, lsCorner.x);
			maxX = std::max(maxX, lsCorner.x);
			minY = std::min(minY, lsCorner.y);
			maxY = std::max(maxY, lsCorner.y);
			minZ = std::min(minZ, lsCorner.z);
			maxZ = std::max(maxZ, lsCorner.z);
		}

		// Extend the depth range to catch shadow casters behind the camera
		constexpr float zMult = 3.0f;
		if (minZ < 0.0f)
			minZ *= zMult;
		else
			minZ /= zMult;

		const glm::mat4 lightProj = glm::ortho(minX, maxX, minY, maxY, minZ, maxZ);
		m_csm.matrices[c] = lightProj * lightView;
	}
}

void OpenGLRenderer::renderCsmShadowMaps(const std::vector<DrawCmd>& drawList)
{
	GLint prevFbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);
	glUseProgram(m_shadow.program);

	for (int c = 0; c < kNumCsmCascades; ++c)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_csm.fbo);
		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_csm.depthArray, 0, c);
		glViewport(0, 0, kCsmMapSize, kCsmMapSize);
		glClear(GL_DEPTH_BUFFER_BIT);

		glUniformMatrix4fv(m_shadow.locLightSpace, 1, GL_FALSE, &m_csm.matrices[c][0][0]);

		// Instanced CSM rendering: draw list is already sorted by (material, obj)
		size_t i = 0;
		while (i < drawList.size())
		{
			auto* mat = drawList[i].material;
			if (!mat) { mat = drawList[i].obj->getMaterial(); }
			if (!mat) { ++i; continue; }
			auto* batchObj = drawList[i].obj;

			// Skinned meshes: individual draw with bone matrices
			if (drawList[i].isSkinned)
			{
				glBindVertexArray(mat->getVao());
				glUniform1i(m_shadow.locInstanced, 0);
				glUniformMatrix4fv(m_shadow.locModel, 1, GL_FALSE, &drawList[i].modelMatrix[0][0]);
				auto ait = m_entityAnimators.find(drawList[i].entityId);
				if (ait != m_entityAnimators.end() && !ait->second->getBoneMatrices().empty())
				{
					glUniform1i(m_shadow.locSkinned, 1);
					const auto& bones = ait->second->getBoneMatrices();
					glUniformMatrix4fv(m_shadow.locBoneMatrices, static_cast<GLsizei>(bones.size()), GL_TRUE, bones[0].m);
				}
				else
				{
					glUniform1i(m_shadow.locSkinned, 0);
				}
				if (mat->getIndexCount() > 0)
					glDrawElements(GL_TRIANGLES, mat->getIndexCount(), GL_UNSIGNED_INT, nullptr);
				else
					glDrawArrays(GL_TRIANGLES, 0, mat->getVertexCount());
				glUniform1i(m_shadow.locSkinned, 0);
				++i;
				continue;
			}

			size_t batchEnd = i + 1;
			while (batchEnd < drawList.size() && drawList[batchEnd].material == mat && drawList[batchEnd].obj == batchObj && !drawList[batchEnd].isSkinned)
				++batchEnd;
			const size_t batchSize = batchEnd - i;

			glBindVertexArray(mat->getVao());
			glUniform1i(m_shadow.locSkinned, 0);

			if (batchSize > 1)
			{
				m_instanceMatrixBuffer.clear();
				for (size_t j = i; j < batchEnd; ++j)
					m_instanceMatrixBuffer.push_back(drawList[j].modelMatrix);
				uploadInstanceData(m_instanceMatrixBuffer.data(), batchSize);
				glUniform1i(m_shadow.locInstanced, 1);
				if (mat->getIndexCount() > 0)
					glDrawElementsInstanced(GL_TRIANGLES, mat->getIndexCount(), GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(batchSize));
				else
					glDrawArraysInstanced(GL_TRIANGLES, 0, mat->getVertexCount(), static_cast<GLsizei>(batchSize));
				glUniform1i(m_shadow.locInstanced, 0);
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
			}
			else
			{
				glUniform1i(m_shadow.locInstanced, 0);
				glUniformMatrix4fv(m_shadow.locModel, 1, GL_FALSE, &drawList[i].modelMatrix[0][0]);
				if (mat->getIndexCount() > 0)
					glDrawElements(GL_TRIANGLES, mat->getIndexCount(), GL_UNSIGNED_INT, nullptr);
				else
					glDrawArrays(GL_TRIANGLES, 0, mat->getVertexCount());
			}

			i = batchEnd;
		}
	}

	glCullFace(GL_BACK);
	glDisable(GL_CULL_FACE);
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

void OpenGLRenderer::findShadowLightIndices()
{
	m_shadow.count = 0;
	m_csm.lightIndex = -1;
	for (int i = 0; i < static_cast<int>(m_sceneLights.size()) && m_shadow.count < kMaxShadowLights; ++i)
	{
		const int type = m_sceneLights[i].type;
		if (type == 1) // directional — use CSM for the first one
		{
			if (m_csm.lightIndex < 0)
			{
				m_csm.lightIndex = i; // handled by CSM, skip regular shadow map
				continue;
			}
		}
		if (type == 1 || type == 2) // directional or spot
		{
			m_shadow.lightIndices[m_shadow.count] = i;
			++m_shadow.count;
		}
	}
}

glm::mat4 OpenGLRenderer::computeLightSpaceMatrix(const OpenGLMaterial::LightData& light) const
{
	const glm::vec3 lightDir = glm::normalize(light.direction);

	// Choose a stable up vector that isn't parallel to lightDir
	glm::vec3 up(0.0f, 1.0f, 0.0f);
	if (std::abs(glm::dot(lightDir, up)) > 0.99f)
	{
		up = glm::vec3(0.0f, 0.0f, 1.0f);
	}

	if (light.type == 2) // LIGHT_SPOT
	{
		float fov = 2.0f * std::acos(light.spotOuterCutoff);
		if (fov <= 0.0f || fov > glm::radians(179.0f))
			fov = glm::radians(90.0f);

		const float farPlane = light.range > 0.0f ? light.range : 50.0f;
		const glm::mat4 lightView = glm::lookAt(light.position, light.position + lightDir, up);
		const glm::mat4 lightProj = glm::perspective(fov, 1.0f, 0.1f, farPlane);
		return lightProj * lightView;
	}

	// Directional light: orthographic projection centered on the camera
	glm::vec3 center(0.0f);
	if (m_camera)
	{
		const Vec3& camPos = m_camera->getPosition();
		center = glm::vec3(camPos.x, camPos.y, camPos.z);
	}

	const float shadowRange = 15.0f;
	const float shadowDepth = 60.0f;
	const glm::vec3 lightPos = center - lightDir * (shadowDepth * 0.5f);
	const glm::mat4 lightView = glm::lookAt(lightPos, center, up);
	const glm::mat4 lightProj = glm::ortho(-shadowRange, shadowRange, -shadowRange, shadowRange,
											0.1f, shadowDepth);
	return lightProj * lightView;
}

void OpenGLRenderer::renderShadowMap(const std::vector<DrawCmd>& drawList)
{
	GLint prevFbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);
	glUseProgram(m_shadow.program);

	for (int s = 0; s < m_shadow.count; ++s)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_shadow.fbo);
		glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadow.depthArray, 0, s);
		glViewport(0, 0, kShadowMapSize, kShadowMapSize);
		glClear(GL_DEPTH_BUFFER_BIT);

		glUniformMatrix4fv(m_shadow.locLightSpace, 1, GL_FALSE, &m_shadow.lightSpaceMatrices[s][0][0]);

		// Instanced shadow rendering: draw list is already sorted by (material, obj)
		size_t i = 0;
		while (i < drawList.size())
		{
			auto* mat = drawList[i].material;
			if (!mat) { mat = drawList[i].obj->getMaterial(); }
			if (!mat) { ++i; continue; }
			auto* batchObj = drawList[i].obj;

			// Skinned meshes must be drawn individually with bone matrices
			if (drawList[i].isSkinned)
			{
				glBindVertexArray(mat->getVao());
				glUniform1i(m_shadow.locInstanced, 0);
				glUniformMatrix4fv(m_shadow.locModel, 1, GL_FALSE, &drawList[i].modelMatrix[0][0]);
				// Upload bone matrices for shadow pass
				auto ait = m_entityAnimators.find(drawList[i].entityId);
				if (ait != m_entityAnimators.end() && !ait->second->getBoneMatrices().empty())
				{
					glUniform1i(m_shadow.locSkinned, 1);
					const auto& bones = ait->second->getBoneMatrices();
					glUniformMatrix4fv(m_shadow.locBoneMatrices, static_cast<GLsizei>(bones.size()), GL_TRUE, bones[0].m);
				}
				else
				{
					glUniform1i(m_shadow.locSkinned, 0);
				}
				if (mat->getIndexCount() > 0)
					glDrawElements(GL_TRIANGLES, mat->getIndexCount(), GL_UNSIGNED_INT, nullptr);
				else
					glDrawArrays(GL_TRIANGLES, 0, mat->getVertexCount());
				glUniform1i(m_shadow.locSkinned, 0);
				++i;
				continue;
			}

			size_t batchEnd = i + 1;
			while (batchEnd < drawList.size() && drawList[batchEnd].material == mat && drawList[batchEnd].obj == batchObj && !drawList[batchEnd].isSkinned)
				++batchEnd;
			const size_t batchSize = batchEnd - i;

			glBindVertexArray(mat->getVao());
			glUniform1i(m_shadow.locSkinned, 0);

			if (batchSize > 1)
			{
				m_instanceMatrixBuffer.clear();
				for (size_t j = i; j < batchEnd; ++j)
					m_instanceMatrixBuffer.push_back(drawList[j].modelMatrix);
				uploadInstanceData(m_instanceMatrixBuffer.data(), batchSize);
				glUniform1i(m_shadow.locInstanced, 1);
				if (mat->getIndexCount() > 0)
					glDrawElementsInstanced(GL_TRIANGLES, mat->getIndexCount(), GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(batchSize));
				else
					glDrawArraysInstanced(GL_TRIANGLES, 0, mat->getVertexCount(), static_cast<GLsizei>(batchSize));
				glUniform1i(m_shadow.locInstanced, 0);
				glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
			}
			else
			{
				glUniform1i(m_shadow.locInstanced, 0);
				glUniformMatrix4fv(m_shadow.locModel, 1, GL_FALSE, &drawList[i].modelMatrix[0][0]);
				if (mat->getIndexCount() > 0)
					glDrawElements(GL_TRIANGLES, mat->getIndexCount(), GL_UNSIGNED_INT, nullptr);
				else
					glDrawArrays(GL_TRIANGLES, 0, mat->getVertexCount());
			}

			i = batchEnd;
		}
	}

	glCullFace(GL_BACK);
	glDisable(GL_CULL_FACE);
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

// ---- Point Light Shadow Mapping (Cube Maps) ----

bool OpenGLRenderer::ensurePointShadowResources()
{
	if (m_pointShadow.program != 0)
		return true;

	// Vertex shader: transform to world space
	const char* vs = R"(
#version 460 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
void main() {
	gl_Position = uModel * vec4(aPos, 1.0);
}
)";

	// Geometry shader: render to all 6 cube faces in one pass using layered rendering
	const char* gs = R"(
#version 460 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 18) out;

uniform mat4 uShadowMatrices[6];
uniform int uLayerOffset;

out vec4 FragPos;

void main() {
	for (int face = 0; face < 6; ++face) {
		gl_Layer = uLayerOffset + face;
		for (int i = 0; i < 3; ++i) {
			FragPos = gl_in[i].gl_Position;
			gl_Position = uShadowMatrices[face] * FragPos;
			EmitVertex();
		}
		EndPrimitive();
	}
}
)";

	// Fragment shader: write linear depth
	const char* fs = R"(
#version 460 core
in vec4 FragPos;
uniform vec3 uLightPos;
uniform float uFarPlane;
void main() {
	float dist = length(FragPos.xyz - uLightPos);
	gl_FragDepth = dist / uFarPlane;
}
)";

	GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vsh, 1, &vs, nullptr);
	glCompileShader(vsh);
	GLuint gsh = glCreateShader(GL_GEOMETRY_SHADER);
	glShaderSource(gsh, 1, &gs, nullptr);
	glCompileShader(gsh);
	GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fsh, 1, &fs, nullptr);
	glCompileShader(fsh);

	m_pointShadow.program = glCreateProgram();
	glAttachShader(m_pointShadow.program, vsh);
	glAttachShader(m_pointShadow.program, gsh);
	glAttachShader(m_pointShadow.program, fsh);
	glLinkProgram(m_pointShadow.program);
	glDeleteShader(vsh);
	glDeleteShader(gsh);
	glDeleteShader(fsh);

	m_pointShadow.locModel = glGetUniformLocation(m_pointShadow.program, "uModel");
	m_pointShadow.locLightPos = glGetUniformLocation(m_pointShadow.program, "uLightPos");
	m_pointShadow.locFarPlane = glGetUniformLocation(m_pointShadow.program, "uFarPlane");
	m_pointShadow.locShadowMatrices = glGetUniformLocation(m_pointShadow.program, "uShadowMatrices[0]");

	// Create cube map array texture
	glGenTextures(1, &m_pointShadow.cubeArray);
	glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, m_pointShadow.cubeArray);
	glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT24,
				 kPointShadowMapSize, kPointShadowMapSize, kMaxPointShadowLights * 6,
				 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_NONE);

	// Create FBO (layered attachment – geometry shader selects the layer)
	glGenFramebuffers(1, &m_pointShadow.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, m_pointShadow.fbo);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_pointShadow.cubeArray, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	return true;
}

void OpenGLRenderer::releasePointShadowResources()
{
	if (m_pointShadow.program)   { glDeleteProgram(m_pointShadow.program); m_pointShadow.program = 0; }
	if (m_pointShadow.cubeArray) { glDeleteTextures(1, &m_pointShadow.cubeArray); m_pointShadow.cubeArray = 0; }
	if (m_pointShadow.fbo)       { glDeleteFramebuffers(1, &m_pointShadow.fbo); m_pointShadow.fbo = 0; }
}

void OpenGLRenderer::findPointShadowLightIndices()
{
	m_pointShadow.count = 0;
	for (int i = 0; i < static_cast<int>(m_sceneLights.size()) && m_pointShadow.count < kMaxPointShadowLights; ++i)
	{
		if (m_sceneLights[i].type == 0) // point light
		{
			m_pointShadow.lightIndices[m_pointShadow.count] = i;
			m_pointShadow.positions[m_pointShadow.count] = m_sceneLights[i].position;
			m_pointShadow.farPlanes[m_pointShadow.count] = m_sceneLights[i].range > 0.0f ? m_sceneLights[i].range : 25.0f;
			++m_pointShadow.count;
		}
	}
}

void OpenGLRenderer::renderPointShadowMaps(const std::vector<DrawCmd>& drawList)
{
	GLint prevFbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_FRONT);
	glUseProgram(m_pointShadow.program);

	glBindFramebuffer(GL_FRAMEBUFFER, m_pointShadow.fbo);
	glViewport(0, 0, kPointShadowMapSize, kPointShadowMapSize);

	// Clear all layers once
	glClear(GL_DEPTH_BUFFER_BIT);

	const GLint locLayerOffset = glGetUniformLocation(m_pointShadow.program, "uLayerOffset");

	for (int s = 0; s < m_pointShadow.count; ++s)
	{
		const glm::vec3& lightPos = m_pointShadow.positions[s];
		const float farPlane = m_pointShadow.farPlanes[s];
		const float nearPlane = 0.1f;

		const glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);
		glm::mat4 shadowViews[6] = {
			shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)),
			shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)),
			shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)),
			shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
			shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)),
			shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0)),
		};

		// Upload per-light uniforms
		if (m_pointShadow.locLightPos >= 0)
			glUniform3fv(m_pointShadow.locLightPos, 1, &lightPos[0]);
		if (m_pointShadow.locFarPlane >= 0)
			glUniform1f(m_pointShadow.locFarPlane, farPlane);
		if (m_pointShadow.locShadowMatrices >= 0)
			glUniformMatrix4fv(m_pointShadow.locShadowMatrices, 6, GL_FALSE, &shadowViews[0][0][0]);
		if (locLayerOffset >= 0)
			glUniform1i(locLayerOffset, s * 6);

		for (const auto& cmd : drawList)
		{
			if (m_pointShadow.locModel >= 0)
				glUniformMatrix4fv(m_pointShadow.locModel, 1, GL_FALSE, &cmd.modelMatrix[0][0]);

			auto* mat = cmd.obj->getMaterial();
			if (!mat)
				continue;
			glBindVertexArray(mat->getVao());
			if (mat->getIndexCount() > 0)
				glDrawElements(GL_TRIANGLES, mat->getIndexCount(), GL_UNSIGNED_INT, nullptr);
			else
				glDrawArrays(GL_TRIANGLES, 0, mat->getVertexCount());
		}
	}

	glCullFace(GL_BACK);
	glDisable(GL_CULL_FACE);
	glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
}

void OpenGLRenderer::drawBoundsDebugBox(const glm::vec3& center, const glm::vec3& extent, const glm::mat4& viewProj)
{
	if (!m_boundsDebug.enabled)
	{
		return;
	}
	if (!ensureBoundsDebugResources() || m_boundsDebug.vertexCount == 0)
	{
		return;
	}

	const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
	glDisable(GL_CULL_FACE);

	glUseProgram(m_boundsDebug.program);
	glBindVertexArray(m_boundsDebug.vao);

	glm::mat4 model(1.0f);
	model = glm::translate(model, center);
	model = glm::scale(model, extent);

	const GLint viewProjLoc = glGetUniformLocation(m_boundsDebug.program, "uViewProj");
	if (viewProjLoc >= 0)
	{
		glUniformMatrix4fv(viewProjLoc, 1, GL_FALSE, &viewProj[0][0]);
	}
	const GLint modelLoc = glGetUniformLocation(m_boundsDebug.program, "uModel");
	if (modelLoc >= 0)
	{
		glUniformMatrix4fv(modelLoc, 1, GL_FALSE, &model[0][0]);
	}
	const GLint colorLoc = glGetUniformLocation(m_boundsDebug.program, "uColor");
	if (colorLoc >= 0)
	{
		glUniform4f(colorLoc, 0.95f, 0.4f, 0.2f, 1.0f);
	}

	glDrawArrays(GL_LINES, 0, m_boundsDebug.vertexCount);

	glBindVertexArray(0);
	glUseProgram(0);
	if (cullWasEnabled)
	{
		glEnable(GL_CULL_FACE);
	}
}

void OpenGLRenderer::buildHzb()
{
	const Vec2 viewportSize = getViewportSize();
	const int width = static_cast<int>(viewportSize.x);
	const int height = static_cast<int>(viewportSize.y);
	if (width <= 0 || height <= 0)
	{
		return;
	}
	if (!ensureHzbResources(width, height))
	{
		return;
	}

	constexpr int kMinReadbackMip = 3;
	const int readbackLevels = static_cast<int>(m_hzbCpuData.size());

	// Map the previous frame's PBO to retrieve async readback data (non-blocking)
	if (m_hzbPboReady && m_hzbPboSize > 0)
	{
		const int readPbo = (m_hzbPboIndex + 1) % kHzbPboCount;
		glBindBuffer(GL_PIXEL_PACK_BUFFER, m_hzbPbos[readPbo]);
		const float* mapped = static_cast<const float*>(glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
		if (mapped)
		{
			size_t offset = 0;
			for (int i = 0; i < readbackLevels; ++i)
			{
				const int mip = kMinReadbackMip + i;
				if (mip >= m_hzbMipLevels)
				{
					break;
				}
				const int mipW = std::max(1, width >> mip);
				const int mipH = std::max(1, height >> mip);
				const size_t count = static_cast<size_t>(mipW) * mipH;
				auto& level = m_hzbCpuData[i];
				level.width = mipW;
				level.height = mipH;
				level.data.resize(count);
				std::memcpy(level.data.data(), mapped + offset, count * sizeof(float));
				offset += count;
			}
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		}
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}

	// Copy viewport tab FBO depth into the depth texture via blit
	GLuint viewportFbo = 0;
#if ENGINE_EDITOR
	for (const auto& tab : m_editorTabs)
	{
		if (tab.id == "Viewport" && tab.renderTarget && tab.renderTarget->isValid())
		{
			viewportFbo = static_cast<const OpenGLRenderTarget*>(tab.renderTarget.get())->getGLFramebuffer();
			break;
		}
	}
#endif
	glBindFramebuffer(GL_READ_FRAMEBUFFER, viewportFbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_hzbDepthCopyFbo);
	glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glDisable(GL_DEPTH_TEST);
	glBindVertexArray(m_hzbFullscreenVao);

	// Mip 0: copy depth texture into HZB R32F texture
	glBindFramebuffer(GL_FRAMEBUFFER, m_hzbFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hzbTexture, 0);
	glViewport(0, 0, width, height);
	glUseProgram(m_hzbCopyProgram);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_hzbDepthTexture);
	if (m_hzbCopyDepthTexLoc >= 0)
	{
		glUniform1i(m_hzbCopyDepthTexLoc, 0);
	}
	glDrawArrays(GL_TRIANGLES, 0, 3);

	// Mip 1+: downsample with max operator
	glUseProgram(m_hzbDownsampleProgram);
	if (m_hzbDownsampleTexLoc >= 0)
	{
		glUniform1i(m_hzbDownsampleTexLoc, 0);
	}
	glBindTexture(GL_TEXTURE_2D, m_hzbTexture);

	for (int mip = 1; mip < m_hzbMipLevels; ++mip)
	{
		const int mipW = std::max(1, width >> mip);
		const int mipH = std::max(1, height >> mip);

		glTextureBarrier();

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hzbTexture, mip);
		glViewport(0, 0, mipW, mipH);

		if (m_hzbPrevMipLoc >= 0)
		{
			glUniform1i(m_hzbPrevMipLoc, mip - 1);
		}

		glDrawArrays(GL_TRIANGLES, 0, 3);
	}

	// Initiate async readback into the current PBO (GPU will fill it in the background)
	if (m_hzbPboSize > 0)
	{
		glBindBuffer(GL_PIXEL_PACK_BUFFER, m_hzbPbos[m_hzbPboIndex]);
		size_t offset = 0;
		for (int i = 0; i < readbackLevels; ++i)
		{
			const int mip = kMinReadbackMip + i;
			if (mip >= m_hzbMipLevels)
			{
				break;
			}
			const int mipW = std::max(1, width >> mip);
			const int mipH = std::max(1, height >> mip);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hzbTexture, mip);
			glReadPixels(0, 0, mipW, mipH, GL_RED, GL_FLOAT, reinterpret_cast<void*>(offset * sizeof(float)));
			offset += static_cast<size_t>(mipW) * mipH;
		}
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		m_hzbPboIndex = (m_hzbPboIndex + 1) % kHzbPboCount;
		m_hzbPboReady = true;
	}

	// Restore viewport tab FBO so subsequent passes (pick, outline) draw into it
	glBindFramebuffer(GL_FRAMEBUFFER, viewportFbo);
	glBindVertexArray(0);
	glUseProgram(0);

	glEnable(GL_DEPTH_TEST);
	glViewport(0, 0, width, height);
}

bool OpenGLRenderer::testAabbAgainstHzb(const glm::vec3& boundsMin, const glm::vec3& boundsMax, const glm::mat4& viewProj) const
{
	if (m_hzbCpuData.empty() || m_hzbWidth <= 0 || m_hzbHeight <= 0)
	{
		return true;
	}

	constexpr int kMinReadbackMip = 3;

	const glm::vec3 corners[8] = {
		{boundsMin.x, boundsMin.y, boundsMin.z},
		{boundsMax.x, boundsMin.y, boundsMin.z},
		{boundsMin.x, boundsMax.y, boundsMin.z},
		{boundsMax.x, boundsMax.y, boundsMin.z},
		{boundsMin.x, boundsMin.y, boundsMax.z},
		{boundsMax.x, boundsMin.y, boundsMax.z},
		{boundsMin.x, boundsMax.y, boundsMax.z},
		{boundsMax.x, boundsMax.y, boundsMax.z}
	};

	float sMinX = 1.0f, sMaxX = 0.0f;
	float sMinY = 1.0f, sMaxY = 0.0f;
	float minZ = 1.0f;

	for (int i = 0; i < 8; ++i)
	{
		const glm::vec4 clip = viewProj * glm::vec4(corners[i], 1.0f);
		if (clip.w <= 0.0f)
		{
			return true;
		}
		const float invW = 1.0f / clip.w;
		const float sx = clip.x * invW * 0.5f + 0.5f;
		const float sy = clip.y * invW * 0.5f + 0.5f;
		const float sz = clip.z * invW * 0.5f + 0.5f;

		sMinX = std::min(sMinX, sx);
		sMaxX = std::max(sMaxX, sx);
		sMinY = std::min(sMinY, sy);
		sMaxY = std::max(sMaxY, sy);
		minZ = std::min(minZ, sz);
	}

	sMinX = std::max(0.0f, sMinX);
	sMaxX = std::min(1.0f, sMaxX);
	sMinY = std::max(0.0f, sMinY);
	sMaxY = std::min(1.0f, sMaxY);

	if (sMinX >= sMaxX || sMinY >= sMaxY)
	{
		return true;
	}

	const float screenW = (sMaxX - sMinX) * static_cast<float>(m_hzbWidth);
	const float screenH = (sMaxY - sMinY) * static_cast<float>(m_hzbHeight);
	const float maxDim = std::max(screenW, screenH);
	int mipLevel = (maxDim > 1.0f) ? static_cast<int>(std::ceil(std::log2(maxDim))) : 0;

	// Clamp to available readback range
	if (mipLevel < kMinReadbackMip)
	{
		return true;
	}

	const int idx = mipLevel - kMinReadbackMip;
	if (idx >= static_cast<int>(m_hzbCpuData.size()))
	{
		return true;
	}

	const auto& level = m_hzbCpuData[idx];
	if (level.width <= 0 || level.height <= 0 || level.data.empty())
	{
		return true;
	}

	const int x0 = std::max(0, static_cast<int>(sMinX * level.width));
	const int y0 = std::max(0, static_cast<int>(sMinY * level.height));
	const int x1 = std::min(level.width - 1, static_cast<int>(sMaxX * level.width));
	const int y1 = std::min(level.height - 1, static_cast<int>(sMaxY * level.height));

	float maxHzbDepth = 0.0f;
	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			const float d = level.data[y * level.width + x];
			maxHzbDepth = std::max(maxHzbDepth, d);
		}
	}

	return minZ <= maxHzbDepth;
}


void OpenGLRenderer::ensureUIShaderDefaults()
{
	if (m_uiShaderDefaultsInitialized)
	{
		return;
	}

	const std::filesystem::path shadersDir = std::filesystem::current_path() / "shaders";
	m_uiShaderBaseDir = shadersDir.string();
	m_defaultPanelVertex = (shadersDir / "panel_vertex.glsl").string();
	m_defaultPanelFragment = (shadersDir / "panel_fragment.glsl").string();
	m_defaultButtonVertex = (shadersDir / "button_vertex.glsl").string();
	m_defaultButtonFragment = (shadersDir / "button_fragment.glsl").string();
	m_defaultTextVertex = (shadersDir / "text_vertex.glsl").string();
	m_defaultTextFragment = (shadersDir / "text_fragment.glsl").string();
	m_uiShaderDefaultsInitialized = true;
}

const std::string& OpenGLRenderer::resolveUIShaderPath(const std::string& value, const std::string& fallback)
{
	if (value.empty())
	{
		return fallback;
	}

	auto it = m_uiShaderPathCache.find(value);
	if (it != m_uiShaderPathCache.end())
	{
		return it->second;
	}

	std::filesystem::path resolved(value);
	if (resolved.is_relative())
	{
		resolved = std::filesystem::path(m_uiShaderBaseDir) / resolved;
	}

	auto [insertedIt, inserted] = m_uiShaderPathCache.emplace(value, resolved.string());
	return insertedIt->second;
}

bool OpenGLRenderer::ensureUIQuadRenderer()
{
	if (m_uiQuadProgram != 0)
	{
		return true;
	}

	glGenVertexArrays(1, &m_uiQuadVao);
	glGenBuffers(1, &m_uiQuadVbo);
	glBindVertexArray(m_uiQuadVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_uiQuadVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 2, nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<void*>(0));
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	return true;
}

GLuint OpenGLRenderer::getUIQuadProgram(const std::string& vertexShaderPath, const std::string& fragmentShaderPath)
{
	if (vertexShaderPath.empty() || fragmentShaderPath.empty())
	{
		return m_uiQuadProgram;
	}

	const std::string key = vertexShaderPath + "|" + fragmentShaderPath;
	auto it = m_uiQuadPrograms.find(key);
	if (it != m_uiQuadPrograms.end())
	{
		return it->second;
	}

	OpenGLShader vertex;
	OpenGLShader fragment;

	if (!vertex.loadFromFile(Shader::Type::Vertex, vertexShaderPath) ||
		!fragment.loadFromFile(Shader::Type::Fragment, fragmentShaderPath))
	{
		return 0;
	}

	GLuint program = glCreateProgram();
	glAttachShader(program, vertex.id());
	glAttachShader(program, fragment.id());
	glLinkProgram(program);

	GLint linked = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		Logger::Instance().log("OpenGLRenderer: Failed to link UI quad shader", Logger::LogLevel::ERROR);
		glDeleteProgram(program);
		return 0;
	}

	m_uiQuadPrograms[key] = program;
	if (m_uiQuadProgram == 0)
	{
		m_uiQuadProgram = program;
	}
	return program;
}

void OpenGLRenderer::drawUIPanel(float x0, float y0, float x1, float y1, const Vec4& color, const glm::mat4& projection, GLuint program,
	const Vec4& hoverColor, float hoverT, float borderRadius)
{
	if (program == 0 || m_uiQuadVao == 0)
	{
		return;
	}

	// Cache uniform locations per program
	auto it = m_uiPanelUniformCache.find(program);
	if (it == m_uiPanelUniformCache.end())
	{
		UIPanelUniforms u{};
		u.projection  = glGetUniformLocation(program, "uProjection");
		u.color       = glGetUniformLocation(program, "uColor");
		u.borderColor = glGetUniformLocation(program, "uBorderColor");
		u.borderSize  = glGetUniformLocation(program, "uBorderSize");
		u.rect        = glGetUniformLocation(program, "uRect");
		u.viewportSize = glGetUniformLocation(program, "uViewportSize");
		u.hoverColor  = glGetUniformLocation(program, "uHoverColor");
		u.isHovered   = glGetUniformLocation(program, "uIsHovered");
		u.borderRadiusLoc = glGetUniformLocation(program, "uBorderRadius");
		it = m_uiPanelUniformCache.emplace(program, u).first;
	}
	const auto& u = it->second;

	const float ht = std::clamp(hoverT, 0.0f, 1.0f);
	const glm::vec4 glColor{ color.x, color.y, color.z, color.w };
	const glm::vec4 glHoverColor{ hoverColor.x, hoverColor.y, hoverColor.z, hoverColor.w };
	const Vec4 baseColor{
		color.x + (hoverColor.x - color.x) * ht,
		color.y + (hoverColor.y - color.y) * ht,
		color.z + (hoverColor.z - color.z) * ht,
		color.w + (hoverColor.w - color.w) * ht
	};
	const glm::vec4 glBorderColor{
		std::min(1.0f, baseColor.x + 0.1f),
		std::min(1.0f, baseColor.y + 0.1f),
		std::min(1.0f, baseColor.z + 0.1f),
		baseColor.w
	};
	const Vec2 viewportSize = (m_currentRenderViewportSize.x > 0.0f && m_currentRenderViewportSize.y > 0.0f)
		? m_currentRenderViewportSize
		: getViewportSize();

	float vertices[6][2] = {
		{ x0, y1 },
		{ x0, y0 },
		{ x1, y0 },
		{ x0, y1 },
		{ x1, y0 },
		{ x1, y1 }
	};

	glUseProgram(program);
	glUniformMatrix4fv(u.projection, 1, GL_FALSE, &projection[0][0]);
	glUniform4fv(u.color, 1, &glColor[0]);
	if (u.borderColor >= 0) glUniform4fv(u.borderColor, 1, &glBorderColor[0]);
	if (u.borderSize >= 0)  glUniform1f(u.borderSize, 1.0f);
	if (u.rect >= 0)        glUniform4f(u.rect, x0, y0, x1, y1);
	if (u.viewportSize >= 0) glUniform2f(u.viewportSize, viewportSize.x, viewportSize.y);
	if (u.hoverColor >= 0)  glUniform4fv(u.hoverColor, 1, &glHoverColor[0]);
	if (u.isHovered >= 0)   glUniform1f(u.isHovered, ht);
	if (u.borderRadiusLoc >= 0) glUniform1f(u.borderRadiusLoc, borderRadius);

	glBindVertexArray(m_uiQuadVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_uiQuadVbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
}

void OpenGLRenderer::drawUIOutline(float x0, float y0, float x1, float y1, const Vec4& color, const glm::mat4& projection, GLuint program)
{
	if (program == 0 || m_uiQuadVao == 0)
	{
		return;
	}

	const float t = 1.0f; // outline thickness in pixels
	// Top edge
	drawUIPanel(x0, y0, x1, y0 + t, color, projection, program, color, false);
	// Bottom edge
	drawUIPanel(x0, y1 - t, x1, y1, color, projection, program, color, false);
	// Left edge
	drawUIPanel(x0, y0 + t, x0 + t, y1 - t, color, projection, program, color, false);
	// Right edge
	drawUIPanel(x1 - t, y0 + t, x1, y1 - t, color, projection, program, color, false);
}

void OpenGLRenderer::drawUIShadow(float x0, float y0, float x1, float y1,
	const Vec4& shadowColor, const Vec2& shadowOffset,
	const glm::mat4& projection, GLuint program, float borderRadius, float blurRadius)
{
	if (shadowColor.w < 0.001f || program == 0)
		return;

	// Draw 3 expanding layers with decreasing alpha for a soft-shadow effect
	// Scale expansion by blurRadius (default 6.0 gives the original 1/3/6 spread)
	constexpr int kLayers = 3;
	const float kExpand[] = { blurRadius * 0.167f, blurRadius * 0.5f, blurRadius };
	constexpr float kAlpha[]  = { 0.55f, 0.30f, 0.12f };

	for (int i = 0; i < kLayers; ++i)
	{
		const float e = kExpand[i];
		const float sx0 = x0 + shadowOffset.x - e;
		const float sy0 = y0 + shadowOffset.y - e;
		const float sx1 = x1 + shadowOffset.x + e;
		const float sy1 = y1 + shadowOffset.y + e;
		const Vec4 layerColor{ shadowColor.x, shadowColor.y, shadowColor.z, shadowColor.w * kAlpha[i] };
		drawUIPanel(sx0, sy0, sx1, sy1, layerColor, projection, program, layerColor, false, borderRadius + e);
	}
}

void OpenGLRenderer::setVSyncEnabled(bool enabled)
{
	m_vsyncEnabled = enabled;
	if (m_window)
	{
		SDL_GL_SetSwapInterval(enabled ? 1 : 0);
	}
}

GLuint OpenGLRenderer::getOrLoadUITexture(const std::string& path)
{
	if (path.empty())
	{
		return 0;
	}
	auto it = m_uiTextureCache.find(path);
	if (it != m_uiTextureCache.end())
	{
		return it->second;
	}

	std::string resolved = path;
	{
		const std::filesystem::path p(resolved);
		if (p.is_relative())
		{
			const std::filesystem::path editorPath = std::filesystem::current_path() / "Editor" / "Textures" / path;
			if (std::filesystem::exists(editorPath))
			{
				resolved = editorPath.string();
			}
		}
	}

	int w = 0;
	int h = 0;
	int ch = 0;
	auto* data = AssetManager::Instance().loadRawImageData(resolved, w, h, ch);
	if (!data || w <= 0 || h <= 0)
	{
		m_uiTextureCache[path] = 0;
		return 0;
	}

	GLenum format = GL_RGBA;
	GLenum internalFormat = GL_RGBA8;
	if (ch == 3)
	{
		format = GL_RGB;
		internalFormat = GL_RGB8;
	}
	else if (ch == 1)
	{
		format = GL_RED;
		internalFormat = GL_R8;
	}

	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), w, h, 0, format, GL_UNSIGNED_BYTE, data);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);

	AssetManager::Instance().freeRawImageData(data);

	m_uiTextureCache[path] = tex;
	return tex;
}

unsigned int OpenGLRenderer::preloadUITexture(const std::string& path)
{
	return getOrLoadUITexture(path);
}

// ---------------------------------------------------------------------------
// Thumbnail FBO helpers
// ---------------------------------------------------------------------------
bool OpenGLRenderer::ensureThumbnailFbo(int size)
{
	if (m_thumb.fbo != 0 && m_thumb.size == size)
		return true;

	releaseThumbnailFbo();

	glGenFramebuffers(1, &m_thumb.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, m_thumb.fbo);

	glGenTextures(1, &m_thumb.colorTex);
	glBindTexture(GL_TEXTURE_2D, m_thumb.colorTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_thumb.colorTex, 0);

	glGenRenderbuffers(1, &m_thumb.depthRbo);
	glBindRenderbuffer(GL_RENDERBUFFER, m_thumb.depthRbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_thumb.depthRbo);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		releaseThumbnailFbo();
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		return false;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	m_thumb.size = size;
	return true;
}

void OpenGLRenderer::releaseThumbnailFbo()
{
	if (m_thumb.fbo) { glDeleteFramebuffers(1, &m_thumb.fbo); m_thumb.fbo = 0; }
	if (m_thumb.colorTex) { glDeleteTextures(1, &m_thumb.colorTex); m_thumb.colorTex = 0; }
	if (m_thumb.depthRbo) { glDeleteRenderbuffers(1, &m_thumb.depthRbo); m_thumb.depthRbo = 0; }
	m_thumb.size = 0;
}

// ---------------------------------------------------------------------------
// generateAssetThumbnail — render a Model3D or Material into a small FBO
// ---------------------------------------------------------------------------
unsigned int OpenGLRenderer::generateAssetThumbnail(const std::string& assetPath, int assetType)
{
	// Check cache first
	auto cacheIt = m_thumbnailCache.find(assetPath);
	if (cacheIt != m_thumbnailCache.end())
		return cacheIt->second;

	constexpr int kThumbSize = 128;
	if (!ensureThumbnailFbo(kThumbSize))
		return 0;

	auto& assetMgr = AssetManager::Instance();

	// AssetType values: Texture=1, Material=2, Model2D=3, Model3D=4
	const bool isModel3D  = (assetType == 4);
	const bool isMaterial = (assetType == 2);
	if (!isModel3D && !isMaterial)
		return 0;

	// Resolve path
	const std::string resolvedPath = m_resourceManager.resolveContentPath(assetPath);
	const std::string loadPath = resolvedPath.empty() ? assetPath : resolvedPath;

	// ── Load or create the mesh object ──
	std::shared_ptr<IRenderObject3D> meshObj;
	std::vector<std::shared_ptr<Texture>> textures;
	float shininess = 32.0f;
	float metallic = 0.0f;
	float roughness = 0.5f;
	bool pbrEnabled = false;

	if (isModel3D)
	{
		int id = assetMgr.loadAsset(loadPath, AssetType::Model3D, AssetManager::Sync);
		if (id == 0) return 0;
		auto asset = assetMgr.getLoadedAssetByID(static_cast<unsigned int>(id));
		if (!asset) return 0;

		// Read material from mesh asset data
		const auto& meshData = asset->getData();
		if (meshData.is_object() && meshData.contains("m_materialAssetPaths") &&
			meshData["m_materialAssetPaths"].is_array() && !meshData["m_materialAssetPaths"].empty())
		{
			std::string matPath = meshData["m_materialAssetPaths"][0].get<std::string>();
			std::string resolvedMatPath = m_resourceManager.resolveContentPath(matPath);
			if (resolvedMatPath.empty()) resolvedMatPath = matPath;
			int matId = assetMgr.loadAsset(resolvedMatPath, AssetType::Material, AssetManager::Sync);
			if (matId != 0)
			{
				auto matAsset = assetMgr.getLoadedAssetByID(static_cast<unsigned int>(matId));
				if (matAsset)
				{
					const auto& matData = matAsset->getData();
					if (matData.is_object())
					{
						if (matData.contains("m_shininess")) shininess = matData["m_shininess"].get<float>();
						if (matData.contains("m_metallic"))  metallic  = matData["m_metallic"].get<float>();
						if (matData.contains("m_roughness")) roughness = matData["m_roughness"].get<float>();
						if (matData.contains("m_pbrEnabled")) pbrEnabled = matData["m_pbrEnabled"].get<bool>();
						if (matData.contains("m_textureAssetPaths") && matData["m_textureAssetPaths"].is_array())
						{
							for (const auto& texVal : matData["m_textureAssetPaths"])
							{
								if (!texVal.is_string()) { textures.push_back(nullptr); continue; }
								std::string texPath = m_resourceManager.resolveContentPath(texVal.get<std::string>());
								if (texPath.empty()) texPath = texVal.get<std::string>();
								int texId = assetMgr.loadAsset(texPath, AssetType::Texture, AssetManager::Sync);
								if (texId == 0) { textures.push_back(nullptr); continue; }
								auto texAsset = assetMgr.getLoadedAssetByID(static_cast<unsigned int>(texId));
								if (!texAsset) { textures.push_back(nullptr); continue; }
								const auto& texData = texAsset->getData();
								if (!texData.is_object() || !texData.contains("m_width") || !texData.contains("m_data"))
								{ textures.push_back(nullptr); continue; }
								auto tex = std::make_shared<Texture>();
								tex->setWidth(texData["m_width"].get<int>());
								tex->setHeight(texData["m_height"].get<int>());
								tex->setChannels(texData["m_channels"].get<int>());
								tex->setData(texData["m_data"].get<std::vector<unsigned char>>());
								textures.push_back(std::move(tex));
							}
						}
					}
				}
			}
		}

		meshObj = m_resourceManager.getOrCreateObject3D(asset, textures);
	}
	else if (isMaterial)
	{
		// Load material properties
		int matId = assetMgr.loadAsset(loadPath, AssetType::Material, AssetManager::Sync);
		if (matId == 0) return 0;
		auto matAsset = assetMgr.getLoadedAssetByID(static_cast<unsigned int>(matId));
		if (!matAsset) return 0;
		const auto& matData = matAsset->getData();
		if (matData.is_object())
		{
			if (matData.contains("m_shininess")) shininess = matData["m_shininess"].get<float>();
			if (matData.contains("m_metallic"))  metallic  = matData["m_metallic"].get<float>();
			if (matData.contains("m_roughness")) roughness = matData["m_roughness"].get<float>();
			if (matData.contains("m_pbrEnabled")) pbrEnabled = matData["m_pbrEnabled"].get<bool>();
			if (matData.contains("m_textureAssetPaths") && matData["m_textureAssetPaths"].is_array())
			{
				for (const auto& texVal : matData["m_textureAssetPaths"])
				{
					if (!texVal.is_string()) { textures.push_back(nullptr); continue; }
					std::string texPath = m_resourceManager.resolveContentPath(texVal.get<std::string>());
					if (texPath.empty()) texPath = texVal.get<std::string>();
					int texId = assetMgr.loadAsset(texPath, AssetType::Texture, AssetManager::Sync);
					if (texId == 0) { textures.push_back(nullptr); continue; }
					auto texAsset = assetMgr.getLoadedAssetByID(static_cast<unsigned int>(texId));
					if (!texAsset) { textures.push_back(nullptr); continue; }
					const auto& texData = texAsset->getData();
					if (!texData.is_object() || !texData.contains("m_width") || !texData.contains("m_data"))
					{ textures.push_back(nullptr); continue; }
					auto tex = std::make_shared<Texture>();
					tex->setWidth(texData["m_width"].get<int>());
					tex->setHeight(texData["m_height"].get<int>());
					tex->setChannels(texData["m_channels"].get<int>());
					tex->setData(texData["m_data"].get<std::vector<unsigned char>>());
					textures.push_back(std::move(tex));
				}
			}
		}

		// Build a procedural UV sphere (16 slices × 12 stacks)
		// Vertex layout: pos3 + uv2 (5 floats per vertex)
		constexpr int kSlices = 16;
		constexpr int kStacks = 12;
		constexpr float kRadius = 1.0f;
		constexpr float kPi = 3.14159265358979323846f;
		std::vector<float> sphereVerts;
		std::vector<uint32_t> sphereIndices;
		sphereVerts.reserve((kSlices + 1) * (kStacks + 1) * 5);
		for (int st = 0; st <= kStacks; ++st)
		{
			float phi = kPi * static_cast<float>(st) / static_cast<float>(kStacks);
			float sinPhi = std::sin(phi);
			float cosPhi = std::cos(phi);
			for (int sl = 0; sl <= kSlices; ++sl)
			{
				float theta = 2.0f * kPi * static_cast<float>(sl) / static_cast<float>(kSlices);
				float x = kRadius * sinPhi * std::cos(theta);
				float y = kRadius * cosPhi;
				float z = kRadius * sinPhi * std::sin(theta);
				float u = static_cast<float>(sl) / static_cast<float>(kSlices);
				float v = static_cast<float>(st) / static_cast<float>(kStacks);
				sphereVerts.insert(sphereVerts.end(), { x, y, z, u, v });
			}
		}
		for (int st = 0; st < kStacks; ++st)
		{
			for (int sl = 0; sl < kSlices; ++sl)
			{
				uint32_t a = static_cast<uint32_t>(st * (kSlices + 1) + sl);
				uint32_t b = a + static_cast<uint32_t>(kSlices + 1);
				sphereIndices.insert(sphereIndices.end(), { a, b, a + 1, a + 1, b, b + 1 });
			}
		}

		auto sphereAsset = std::make_shared<AssetData>();
		sphereAsset->setPath("__thumbnail_sphere__");
		sphereAsset->setType(AssetType::Model3D);
		json sphereData;
		sphereData["m_vertices"] = sphereVerts;
		sphereData["m_indices"] = sphereIndices;
		sphereAsset->setData(std::move(sphereData));

		meshObj = std::make_shared<OpenGLObject3D>(sphereAsset);
		if (!std::static_pointer_cast<OpenGLObject3D>(meshObj)->prepare())
			return 0;
		meshObj->setTextures(textures);
	}

	if (!meshObj)
		return 0;

	auto obj3d = std::static_pointer_cast<OpenGLObject3D>(meshObj);
	obj3d->setShininess(shininess);
	obj3d->setPbrData(pbrEnabled, metallic, roughness);

	// ── Compute camera from AABB ──
	glm::vec3 center(0.0f);
	float dist = 3.0f;
	if (meshObj->hasLocalBounds())
	{
		auto bmin = meshObj->getLocalBoundsMin();
		auto bmax = meshObj->getLocalBoundsMax();
		center = glm::vec3((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f, (bmin.z + bmax.z) * 0.5f);
		float maxExt = std::max({ bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z, 0.1f });
		dist = maxExt * 1.8f;
	}

	glm::vec3 eye = center + glm::vec3(dist * 0.5f, dist * 0.4f, dist * 0.5f);
	glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.01f, dist * 10.0f);
	glm::mat4 model(1.0f);

	// ── Set up lighting ──
	OpenGLMaterial::LightData light;
	light.type = 1; // directional
	light.direction = glm::normalize(glm::vec3(-0.4f, -0.7f, -0.5f));
	light.color = glm::vec3(0.95f, 0.9f, 0.85f);
	light.intensity = 1.0f;
	light.range = 100.0f;
	std::vector<OpenGLMaterial::LightData> lights = { light };

	// ── Save current GL state ──
	GLint prevFbo = 0;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
	GLint prevViewport[4];
	glGetIntegerv(GL_VIEWPORT, prevViewport);

	// ── Render into thumbnail FBO ──
	glBindFramebuffer(GL_FRAMEBUFFER, m_thumb.fbo);
	glViewport(0, 0, kThumbSize, kThumbSize);
	glClearColor(0.18f, 0.18f, 0.20f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);

	obj3d->setMatrices(model, view, proj);
	obj3d->setLightData(eye, light.color, light.intensity);
	obj3d->setLights(lights);
	obj3d->setFogData(false, glm::vec3(0.0f), 0.0f);
	obj3d->setDebugMode(0); // Lit
	obj3d->setNearFarPlanes(0.01f, dist * 10.0f);
	obj3d->render();

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// ── Copy the rendered pixels into a new persistent texture ──
	GLuint thumbTex = 0;
	glGenTextures(1, &thumbTex);
	glBindTexture(GL_TEXTURE_2D, thumbTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kThumbSize, kThumbSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_thumb.fbo);
	glBindTexture(GL_TEXTURE_2D, thumbTex);
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, kThumbSize, kThumbSize);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	// ── Restore GL state ──
	glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
	glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

	if (thumbTex != 0)
		m_thumbnailCache[assetPath] = thumbTex;

	return thumbTex;
}

void OpenGLRenderer::drawUIImage(float x0, float y0, float x1, float y1, GLuint textureId, const glm::mat4& projection, const Vec4& tintColor, bool invertRGB, bool flipY)
{
	if (textureId == 0 || m_uiQuadVao == 0)
	{
		return;
	}

	if (m_uiImageProgram == 0)
	{
		const char* vsSource = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vTexCoord;
uniform mat4 uProjection;
uniform vec4 uRect;
uniform float uFlipY;
void main() {
	gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
	vec2 normalised = (aPos - uRect.xy) / max(uRect.zw - uRect.xy, vec2(1.0));
	vTexCoord = vec2(normalised.x, mix(normalised.y, 1.0 - normalised.y, uFlipY));
}
)";
		const char* fsSource = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec4 uTintColor;
uniform float uInvertRGB;
void main() {
	vec4 texel = texture(uTexture, vTexCoord);
	texel.rgb = mix(texel.rgb, vec3(1.0) - texel.rgb, uInvertRGB);
	FragColor = texel * uTintColor;
}
)";
		GLuint vs = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vs, 1, &vsSource, nullptr);
		glCompileShader(vs);
		GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fs, 1, &fsSource, nullptr);
		glCompileShader(fs);
		m_uiImageProgram = glCreateProgram();
		glAttachShader(m_uiImageProgram, vs);
		glAttachShader(m_uiImageProgram, fs);
		glLinkProgram(m_uiImageProgram);
		glDeleteShader(vs);
		glDeleteShader(fs);

		// Cache uniform locations once
		m_uiImageUniforms.projection = glGetUniformLocation(m_uiImageProgram, "uProjection");
		m_uiImageUniforms.rect       = glGetUniformLocation(m_uiImageProgram, "uRect");
		m_uiImageUniforms.tintColor  = glGetUniformLocation(m_uiImageProgram, "uTintColor");
		m_uiImageUniforms.invertRGB  = glGetUniformLocation(m_uiImageProgram, "uInvertRGB");
		m_uiImageUniforms.flipY      = glGetUniformLocation(m_uiImageProgram, "uFlipY");
		m_uiImageUniforms.texture    = glGetUniformLocation(m_uiImageProgram, "uTexture");
	}

	float vertices[6][2] = {
		{ x0, y1 },
		{ x0, y0 },
		{ x1, y0 },
		{ x0, y1 },
		{ x1, y0 },
		{ x1, y1 }
	};

	glUseProgram(m_uiImageProgram);
	glUniformMatrix4fv(m_uiImageUniforms.projection, 1, GL_FALSE, &projection[0][0]);
	glUniform4f(m_uiImageUniforms.rect, x0, y0, x1, y1);
	glUniform4f(m_uiImageUniforms.tintColor, tintColor.x, tintColor.y, tintColor.z, tintColor.w);
	glUniform1f(m_uiImageUniforms.invertRGB, invertRGB ? 1.0f : 0.0f);
	glUniform1f(m_uiImageUniforms.flipY, flipY ? 1.0f : 0.0f);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, textureId);
	glUniform1i(m_uiImageUniforms.texture, 0);

	glBindVertexArray(m_uiQuadVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_uiQuadVbo);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderer::drawUIBrush(float x0, float y0, float x1, float y1, const UIBrush& brush, const glm::mat4& projection, float opacity, float hoverT, const UIBrush* hoverBrush, float borderRadius)
{
	const UIBrush& activeBrush = (hoverT > 0.5f && hoverBrush && hoverBrush->isVisible()) ? *hoverBrush : brush;
	if (!activeBrush.isVisible()) return;

	// Apply opacity to the brush color alpha
	auto applyOpacity = [opacity](const Vec4& c) -> Vec4 {
		return Vec4{ c.x, c.y, c.z, c.w * opacity };
	};

	switch (activeBrush.type)
	{
	case BrushType::SolidColor:
	{
		ensureUIShaderDefaults();
		const std::string& vp = m_defaultPanelVertex;
		const std::string& fp = m_defaultPanelFragment;
		const GLuint prog = getUIQuadProgram(vp, fp);
		const Vec4 col = applyOpacity(activeBrush.color);
		drawUIPanel(x0, y0, x1, y1, col, projection, prog, col, false, borderRadius);
		break;
	}
	case BrushType::Image:
	{
		GLuint tex = (activeBrush.textureId != 0)
			? static_cast<GLuint>(activeBrush.textureId)
			: getOrLoadUITexture(activeBrush.imagePath);
		if (tex != 0)
		{
			const Vec4 tint = applyOpacity(activeBrush.color);
			drawUIImage(x0, y0, x1, y1, tex, projection, tint, false, false);
		}
		break;
	}
	case BrushType::NineSlice:
	{
		// 9-slice rendering: corners stay fixed, edges and center stretch
		GLuint tex = (activeBrush.textureId != 0)
			? static_cast<GLuint>(activeBrush.textureId)
			: getOrLoadUITexture(activeBrush.imagePath);
		if (tex != 0)
		{
			const Vec4 tint = applyOpacity(activeBrush.color);
			const float ml = activeBrush.imageMargin.x; // left
			const float mt = activeBrush.imageMargin.y; // top
			const float mr = activeBrush.imageMargin.z; // right
			const float mb = activeBrush.imageMargin.w; // bottom
			// For now, render as a simple image (full 9-slice shader deferred to later)
			drawUIImage(x0, y0, x1, y1, tex, projection, tint, false, false);
		}
		break;
	}
	case BrushType::LinearGradient:
	{
		if (m_uiQuadVao == 0) break;
		// Lazy-init gradient shader
		if (m_uiGradientProgram == 0)
		{
			const char* vsSource = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vLocalPos;
uniform mat4 uProjection;
uniform vec4 uRect;
void main() {
	gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
	vLocalPos = (aPos - uRect.xy) / max(uRect.zw - uRect.xy, vec2(1.0));
}
)";
			const char* fsSource = R"(
#version 330 core
in vec2 vLocalPos;
out vec4 FragColor;
uniform vec4 uColorStart;
uniform vec4 uColorEnd;
uniform float uAngle;
void main() {
	float rad = radians(uAngle);
	vec2 dir = vec2(sin(rad), cos(rad));
	float t = clamp(dot(vLocalPos - vec2(0.5), dir) + 0.5, 0.0, 1.0);
	FragColor = mix(uColorStart, uColorEnd, t);
}
)";
			GLuint vs = glCreateShader(GL_VERTEX_SHADER);
			glShaderSource(vs, 1, &vsSource, nullptr);
			glCompileShader(vs);
			GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
			glShaderSource(fs, 1, &fsSource, nullptr);
			glCompileShader(fs);
			m_uiGradientProgram = glCreateProgram();
			glAttachShader(m_uiGradientProgram, vs);
			glAttachShader(m_uiGradientProgram, fs);
			glLinkProgram(m_uiGradientProgram);
			glDeleteShader(vs);
			glDeleteShader(fs);

			m_uiGradientUniforms.projection = glGetUniformLocation(m_uiGradientProgram, "uProjection");
			m_uiGradientUniforms.rect       = glGetUniformLocation(m_uiGradientProgram, "uRect");
			m_uiGradientUniforms.colorStart  = glGetUniformLocation(m_uiGradientProgram, "uColorStart");
			m_uiGradientUniforms.colorEnd    = glGetUniformLocation(m_uiGradientProgram, "uColorEnd");
			m_uiGradientUniforms.angle       = glGetUniformLocation(m_uiGradientProgram, "uAngle");
		}

		const Vec4 colStart = applyOpacity(activeBrush.color);
		const Vec4 colEnd = applyOpacity(activeBrush.colorEnd);

		float vertices[6][2] = {
			{ x0, y1 }, { x0, y0 }, { x1, y0 },
			{ x0, y1 }, { x1, y0 }, { x1, y1 }
		};

		glUseProgram(m_uiGradientProgram);
		glUniformMatrix4fv(m_uiGradientUniforms.projection, 1, GL_FALSE, &projection[0][0]);
		glUniform4f(m_uiGradientUniforms.rect, x0, y0, x1, y1);
		glUniform4f(m_uiGradientUniforms.colorStart, colStart.x, colStart.y, colStart.z, colStart.w);
		glUniform4f(m_uiGradientUniforms.colorEnd, colEnd.x, colEnd.y, colEnd.z, colEnd.w);
		glUniform1f(m_uiGradientUniforms.angle, activeBrush.gradientAngle);

		glBindVertexArray(m_uiQuadVao);
		glBindBuffer(GL_ARRAY_BUFFER, m_uiQuadVbo);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glBindVertexArray(0);
		break;
	}
	case BrushType::None:
	default:
		break;
	}
}

Vec2 OpenGLRenderer::getViewportSize() const
{
	if (m_cachedWindowWidth > 0 && m_cachedWindowHeight > 0)
	{
		return Vec2{ static_cast<float>(m_cachedWindowWidth), static_cast<float>(m_cachedWindowHeight) };
	}
	int width = 0;
	int height = 0;
	if (m_window)
	{
		SDL_GetWindowSizeInPixels(m_window, &width, &height);
	}

	return Vec2{ static_cast<float>(width), static_cast<float>(height) };
}

UIManager& OpenGLRenderer::getUIManager()
{
	return m_uiManager;
}

const UIManager& OpenGLRenderer::getUIManager() const
{
	return m_uiManager;
}

std::shared_ptr<Widget> OpenGLRenderer::createWidgetFromAsset(const std::shared_ptr<AssetData>& asset)
{
	auto widget = m_resourceManager.buildWidgetAsset(asset);
	if (!widget || !asset)
	{
		return widget;
	}

	if (asset->getName() == "WorldSettings")
	{
		const auto findById = [](std::vector<WidgetElement>& elements, const std::string& id) -> WidgetElement*
		{
			const std::function<WidgetElement*(WidgetElement&)> find = [&](WidgetElement& element) -> WidgetElement*
				{
					if (element.id == id)
					{
						return &element;
					}
					for (auto& child : element.children)
					{
						if (auto* match = find(child))
						{
							return match;
						}
					}
					return nullptr;
				};
			for (auto& element : elements)
			{
				if (auto* match = find(element))
				{
					return match;
				}
			}
			return nullptr;
		};

		if (auto* picker = findById(widget->getElementsMutable(), "WorldSettings.ClearColor"))
		{
			picker->style.color = m_clearColor;
			picker->onColorChanged = [this](const Vec4& color)
				{
					setClearColor(color);
				};
		}
	}

	return widget;
}

bool OpenGLRenderer::isRenderEntryRelevant(const RenderEntry& entry) const
{
	(void)entry;
	return true;
}

void OpenGLRenderer::rotateCamera(float yawDeltaDegrees, float pitchDeltaDegrees)
{
	if (!m_camera || m_cameraTransition.active || m_cameraPathActive)
		return;

	// Multi-viewport: route rotation to active sub-viewport camera if not the main one
#if ENGINE_EDITOR
	const int subCount = getSubViewportCount();
	if (subCount > 1 && m_activeSubViewport > 0 && m_activeSubViewport < kMaxSubViewports)
	{
		ensureSubViewportCameras();
		auto& cam = m_subViewportCameras[m_activeSubViewport];
		cam.yawDeg += yawDeltaDegrees;
		cam.pitchDeg += pitchDeltaDegrees;
		if (cam.pitchDeg > 89.0f)  cam.pitchDeg = 89.0f;
		if (cam.pitchDeg < -89.0f) cam.pitchDeg = -89.0f;
		return;
	}
#endif // ENGINE_EDITOR — multi-viewport routing

	m_camera->rotate(yawDeltaDegrees, pitchDeltaDegrees);
}

Vec3 OpenGLRenderer::getCameraPosition() const
{
	if (m_camera)
	{
		return m_camera->getPosition();
	}
	return Vec3{};
}

void OpenGLRenderer::setCameraPosition(const Vec3& position)
{
	if (m_camera)
	{
		m_camera->setPosition(position);
	}
}

Vec2 OpenGLRenderer::getCameraRotationDegrees() const
{
	if (m_camera)
	{
		return m_camera->getRotationDegrees();
	}
	return Vec2{};
}

void OpenGLRenderer::setCameraRotationDegrees(float yawDegrees, float pitchDegrees)
{
	if (m_camera)
	{
		m_camera->setRotationDegrees(yawDegrees, pitchDegrees);
	}
}

void OpenGLRenderer::setActiveCameraEntity(unsigned int entity)
{
	m_activeCameraEntity = entity;
}

unsigned int OpenGLRenderer::getActiveCameraEntity() const
{
	return m_activeCameraEntity;
}

void OpenGLRenderer::clearActiveCameraEntity()
{
	m_activeCameraEntity = 0;
}

void OpenGLRenderer::startCameraTransition(const Vec3& targetPos, float targetYaw, float targetPitch, float durationSec)
{
	if (!m_camera || durationSec <= 0.0f)
	{
		// Instant snap
		if (m_camera)
		{
			m_camera->setPosition(targetPos);
			m_camera->setRotationDegrees(targetYaw, targetPitch);
		}
		m_cameraTransition.active = false;
		return;
	}
	m_cameraTransition.startPos = m_camera->getPosition();
	const Vec2 rot = m_camera->getRotationDegrees();
	m_cameraTransition.startYaw = rot.x;
	m_cameraTransition.startPitch = rot.y;
	m_cameraTransition.endPos = targetPos;
	m_cameraTransition.endYaw = targetYaw;
	m_cameraTransition.endPitch = targetPitch;
	m_cameraTransition.duration = durationSec;
	m_cameraTransition.elapsed = 0.0f;
	m_cameraTransition.active = true;
}

bool OpenGLRenderer::isCameraTransitioning() const
{
	return m_cameraTransition.active;
}

void OpenGLRenderer::cancelCameraTransition()
{
	m_cameraTransition.active = false;
}

#if ENGINE_EDITOR
void OpenGLRenderer::focusOnSelectedEntity()
{
	const unsigned int primaryEntity = m_selectedEntities.empty() ? 0u : *m_selectedEntities.begin();
	if (!m_camera || primaryEntity == 0)
		return;

	// Find the render entry for the primary selected entity to get its AABB
	auto findEntry = [&](const std::vector<RenderEntry>& entries) -> const RenderEntry*
	{
		for (auto& e : entries)
			if (e.entity == primaryEntity)
				return &e;
		return nullptr;
	};

	const RenderEntry* entry = findEntry(m_renderEntries);
	if (!entry)
		entry = findEntry(m_meshEntries);

	// Compute world-space AABB center and size
	Vec3 center{};
	float radius = 2.0f;

	auto& ecs = ECS::ECSManager::Instance();
	auto* tc = ecs.getComponent<ECS::TransformComponent>(primaryEntity);
	if (!tc)
		return;

	center = { tc->position[0], tc->position[1], tc->position[2] };

	if (entry && entry->object3D && entry->object3D->hasLocalBounds())
	{
		Vec3 lo = entry->object3D->getLocalBoundsMin();
		Vec3 hi = entry->object3D->getLocalBoundsMax();
		float sx = tc->scale[0], sy = tc->scale[1], sz = tc->scale[2];
		Vec3 scaledHalf{
			(hi.x - lo.x) * 0.5f * std::abs(sx),
			(hi.y - lo.y) * 0.5f * std::abs(sy),
			(hi.z - lo.z) * 0.5f * std::abs(sz)
		};
		Vec3 localCenter{
			(lo.x + hi.x) * 0.5f * sx,
			(lo.y + hi.y) * 0.5f * sy,
			(lo.z + hi.z) * 0.5f * sz
		};
		center.x += localCenter.x;
		center.y += localCenter.y;
		center.z += localCenter.z;
		radius = std::sqrt(scaledHalf.x * scaledHalf.x + scaledHalf.y * scaledHalf.y + scaledHalf.z * scaledHalf.z);
		if (radius < 0.1f) radius = 0.1f;
	}

	// Camera offset: approach from current direction
	const float distance = radius * 2.5f;
	Vec2 rot = m_camera->getRotationDegrees();
	float yaw = rot.x;
	float pitch = rot.y;

	constexpr float kDegToRad = 3.14159265f / 180.0f;
	float yawRad = yaw * kDegToRad;
	float pitchRad = pitch * kDegToRad;

	Vec3 targetPos{
		center.x - std::cos(pitchRad) * std::sin(yawRad) * distance,
		center.y - std::sin(pitchRad) * distance,
		center.z + std::cos(pitchRad) * std::cos(yawRad) * distance
	};

	startCameraTransition(targetPos, yaw, pitch, 0.3f);
}
#endif // ENGINE_EDITOR — focusOnSelectedEntity

// ─── Cinematic camera path (Catmull-Rom spline) ─────────────────────────────

void OpenGLRenderer::startCameraPath(const std::vector<CameraPathPoint>& points, float duration, bool loop)
{
	if (!m_camera || points.size() < 2 || duration <= 0.0f)
		return;

	// Cancel any active single-point transition
	m_cameraTransition.active = false;

	m_cameraPath.points = points;
	m_cameraPath.duration = duration;
	m_cameraPath.loop = loop;
	m_cameraPathElapsed = 0.0f;
	m_cameraPathActive = true;
	m_cameraPathPaused = false;
	m_lastPathTick = 0;
}

bool OpenGLRenderer::isCameraPathPlaying() const
{
	return m_cameraPathActive && !m_cameraPathPaused;
}

void OpenGLRenderer::pauseCameraPath()
{
	m_cameraPathPaused = true;
}

void OpenGLRenderer::resumeCameraPath()
{
	if (m_cameraPathActive)
	{
		m_cameraPathPaused = false;
		m_lastPathTick = 0; // reset tick to avoid time jump
	}
}

void OpenGLRenderer::stopCameraPath()
{
	m_cameraPathActive = false;
	m_cameraPathPaused = false;
	m_lastPathTick = 0;
}

float OpenGLRenderer::getCameraPathProgress() const
{
	if (!m_cameraPathActive || m_cameraPath.duration <= 0.0f)
		return 0.0f;
	float t = m_cameraPathElapsed / m_cameraPath.duration;
	if (t > 1.0f) t = 1.0f;
	return t;
}

std::vector<CameraPathPoint> OpenGLRenderer::getCameraPathPoints() const
{
	return m_cameraPath.points;
}

void OpenGLRenderer::setCameraPathPoints(const std::vector<CameraPathPoint>& pts)
{
	m_cameraPath.points = pts;
}

float OpenGLRenderer::getCameraPathDuration() const
{
	return m_cameraPath.duration;
}

void OpenGLRenderer::setCameraPathDuration(float d)
{
	if (d > 0.0f) m_cameraPath.duration = d;
}

bool OpenGLRenderer::getCameraPathLoop() const
{
	return m_cameraPath.loop;
}

void OpenGLRenderer::setCameraPathLoop(bool l)
{
	m_cameraPath.loop = l;
}

// ─── Multi-Viewport sub-cameras (Phase 11.1) ───────────────────────────────

void OpenGLRenderer::ensureSubViewportCameras()
{
	if (m_subViewportCamerasInitialized) return;
	m_subViewportCamerasInitialized = true;

	// Default camera 0: Perspective (copy from main editor camera)
	if (m_camera)
	{
		m_subViewportCameras[0].position = m_camera->getPosition();
		const Vec2 rot = m_camera->getRotationDegrees();
		m_subViewportCameras[0].yawDeg = rot.x;
		m_subViewportCameras[0].pitchDeg = rot.y;
	}
	m_subViewportCameras[0].preset = SubViewportPreset::Perspective;

	// Camera 1: Top (looking down -Y)
	m_subViewportCameras[1].position = { 0.0f, 20.0f, 0.0f };
	m_subViewportCameras[1].yawDeg = -90.0f;
	m_subViewportCameras[1].pitchDeg = -89.0f;
	m_subViewportCameras[1].preset = SubViewportPreset::Top;

	// Camera 2: Front (looking down -Z)
	m_subViewportCameras[2].position = { 0.0f, 2.0f, 15.0f };
	m_subViewportCameras[2].yawDeg = -90.0f;
	m_subViewportCameras[2].pitchDeg = 0.0f;
	m_subViewportCameras[2].preset = SubViewportPreset::Front;

	// Camera 3: Right (looking down -X)
	m_subViewportCameras[3].position = { 15.0f, 2.0f, 0.0f };
	m_subViewportCameras[3].yawDeg = -180.0f;
	m_subViewportCameras[3].pitchDeg = 0.0f;
	m_subViewportCameras[3].preset = SubViewportPreset::Right;
}

#if ENGINE_EDITOR
Renderer::SubViewportCamera OpenGLRenderer::getSubViewportCamera(int index) const
{
	if (index < 0 || index >= kMaxSubViewports) return {};
	return m_subViewportCameras[index];
}

void OpenGLRenderer::setSubViewportCamera(int index, const SubViewportCamera& cam)
{
	if (index < 0 || index >= kMaxSubViewports) return;
	m_subViewportCameras[index] = cam;
}

int OpenGLRenderer::subViewportHitTest(int screenX, int screenY) const
{
	if (m_viewportLayout == ViewportLayout::Single) return 0;

	const Vec4 vp = m_cachedViewportContentRect;
	if (vp.z <= 0.0f || vp.w <= 0.0f) return 0;

	const float relX = static_cast<float>(screenX) - vp.x;
	const float relY = static_cast<float>(screenY) - vp.y;
	if (relX < 0 || relY < 0 || relX >= vp.z || relY >= vp.w) return -1;

	const float halfW = vp.z * 0.5f;
	const float halfH = vp.w * 0.5f;

	switch (m_viewportLayout)
	{
	case ViewportLayout::TwoHorizontal:
		return (relY < halfH) ? 0 : 1;
	case ViewportLayout::TwoVertical:
		return (relX < halfW) ? 0 : 1;
	case ViewportLayout::Quad:
	{
		const int col = (relX < halfW) ? 0 : 1;
		const int row = (relY < halfH) ? 0 : 1;
		return row * 2 + col;
	}
	default:
		return 0;
	}
}
#endif // ENGINE_EDITOR — Multi-Viewport overrides

#if ENGINE_EDITOR
void OpenGLRenderer::computeSubViewportRects(int vpX, int vpY, int vpW, int vpH,
											  SubViewportRect* outRects, int count) const
{
	constexpr int kGap = 2; // pixel gap between sub-viewports
	switch (m_viewportLayout)
	{
	case ViewportLayout::TwoHorizontal:
	{
		const int halfH = (vpH - kGap) / 2;
		if (count > 0) outRects[0] = { vpX, vpY, vpW, halfH };
		if (count > 1) outRects[1] = { vpX, vpY + halfH + kGap, vpW, vpH - halfH - kGap };
		break;
	}
	case ViewportLayout::TwoVertical:
	{
		const int halfW = (vpW - kGap) / 2;
		if (count > 0) outRects[0] = { vpX, vpY, halfW, vpH };
		if (count > 1) outRects[1] = { vpX + halfW + kGap, vpY, vpW - halfW - kGap, vpH };
		break;
	}
	case ViewportLayout::Quad:
	{
		const int halfW = (vpW - kGap) / 2;
		const int halfH = (vpH - kGap) / 2;
		if (count > 0) outRects[0] = { vpX,               vpY,               halfW,                halfH };
		if (count > 1) outRects[1] = { vpX + halfW + kGap, vpY,               vpW - halfW - kGap,   halfH };
		if (count > 2) outRects[2] = { vpX,               vpY + halfH + kGap, halfW,                vpH - halfH - kGap };
		if (count > 3) outRects[3] = { vpX + halfW + kGap, vpY + halfH + kGap, vpW - halfW - kGap,   vpH - halfH - kGap };
		break;
	}
	default:
		if (count > 0) outRects[0] = { vpX, vpY, vpW, vpH };
		break;
	}
}
#endif // ENGINE_EDITOR — computeSubViewportRects

const std::string& OpenGLRenderer::name() const
{
	return m_name;
}

RendererCapabilities OpenGLRenderer::getCapabilities() const
{
	RendererCapabilities caps;
	caps.supportsShadows            = true;
	caps.supportsOcclusion          = true;
	caps.supportsWireframe          = true;
	caps.supportsVSync              = true;
	caps.supportsEntityPicking      = true;
	caps.supportsGizmos             = true;
	caps.supportsSkybox             = true;
#if ENGINE_EDITOR
	caps.supportsPopupWindows       = true;
#endif
	caps.supportsPostProcessing     = true;
	caps.supportsTextureCompression = true;
	caps.supportsTessellation       = true;
	return caps;
}

SDL_Window* OpenGLRenderer::window() const
{
	return m_window;
}

#if ENGINE_EDITOR
// â”€â”€â”€ Entity pick FBO â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
bool OpenGLRenderer::ensurePickFbo(int width, int height)
{
	if (m_pick.fbo != 0 && m_pick.width == width && m_pick.height == height)
	{
		return true;
	}
	releasePickFbo();
	m_pick.width = width;
	m_pick.height = height;

	glGenFramebuffers(1, &m_pick.fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, m_pick.fbo);

	glGenTextures(1, &m_pick.colorTex);
	glBindTexture(GL_TEXTURE_2D, m_pick.colorTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_pick.colorTex, 0);

	glGenRenderbuffers(1, &m_pick.depthRbo);
	glBindRenderbuffer(GL_RENDERBUFFER, m_pick.depthRbo);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_pick.depthRbo);

	const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		releasePickFbo();
		return false;
	}

	// Build pick shader if needed
	if (m_pick.program == 0)
	{
		const std::string vs =
			"#version 460 core\n"
			"layout(location = 0) in vec3 aPos;\n"
			"layout(location = 1) in vec3 aNormal;\n"
			"layout(location = 2) in vec2 aTexCoord;\n"
			"uniform mat4 uModel;\n"
			"uniform mat4 uView;\n"
			"uniform mat4 uProjection;\n"
			"void main() {\n"
			"    gl_Position = uProjection * uView * uModel * vec4(aPos, 1.0);\n"
			"}\n";
		const std::string fs =
			"#version 460 core\n"
			"layout(location = 0) out uint oEntityId;\n"
			"uniform uint uEntityId;\n"
			"void main() {\n"
			"    oEntityId = uEntityId;\n"
			"}\n";

		OpenGLShader vShader;
		OpenGLShader fShader;
		if (!vShader.loadFromSource(Shader::Type::Vertex, vs) ||
			!fShader.loadFromSource(Shader::Type::Fragment, fs))
		{
			return false;
		}
		GLuint prog = glCreateProgram();
		glAttachShader(prog, vShader.id());
		glAttachShader(prog, fShader.id());
		glLinkProgram(prog);
		GLint linked = 0;
		glGetProgramiv(prog, GL_LINK_STATUS, &linked);
		if (!linked)
		{
			glDeleteProgram(prog);
			return false;
		}
		m_pick.program = prog;
		m_pick.locModel = glGetUniformLocation(prog, "uModel");
		m_pick.locView = glGetUniformLocation(prog, "uView");
		m_pick.locProjection = glGetUniformLocation(prog, "uProjection");
		m_pick.locEntityId = glGetUniformLocation(prog, "uEntityId");
	}
	return true;
}

void OpenGLRenderer::releasePickFbo()
{
	if (m_pick.colorTex) { glDeleteTextures(1, &m_pick.colorTex); m_pick.colorTex = 0; }
	if (m_pick.depthRbo) { glDeleteRenderbuffers(1, &m_pick.depthRbo); m_pick.depthRbo = 0; }
	if (m_pick.fbo) { glDeleteFramebuffers(1, &m_pick.fbo); m_pick.fbo = 0; }
	if (m_pick.program) { glDeleteProgram(m_pick.program); m_pick.program = 0; }
	m_pick.width = 0;
	m_pick.height = 0;
}

void OpenGLRenderer::renderPickBuffer(const glm::mat4& view, const glm::mat4& projection)
{
	if (m_pick.fbo == 0 || m_pick.program == 0)
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, m_pick.fbo);
	glViewport(0, 0, m_pick.width, m_pick.height);
	const GLuint clearId = 0;
	glClearBufferuiv(GL_COLOR, 0, &clearId);
	glClear(GL_DEPTH_BUFFER_BIT);

	// Restrict pick rendering to the viewport content rect area
	const Vec4 vpRect = m_cachedViewportContentRect;
	if (vpRect.z > 0.0f && vpRect.w > 0.0f)
	{
		glViewport(static_cast<int>(vpRect.x),
				   m_pick.height - static_cast<int>(vpRect.y) - static_cast<int>(vpRect.w),
				   static_cast<int>(vpRect.z), static_cast<int>(vpRect.w));
	}

	glUseProgram(m_pick.program);
	if (m_pick.locView >= 0)
		glUniformMatrix4fv(m_pick.locView, 1, GL_FALSE, &view[0][0]);
	if (m_pick.locProjection >= 0)
		glUniformMatrix4fv(m_pick.locProjection, 1, GL_FALSE, &projection[0][0]);

	for (const auto& cmd : m_drawList)
	{
		if (cmd.entityId == 0 || !cmd.obj)
			continue;

		if (m_pick.locModel >= 0)
			glUniformMatrix4fv(m_pick.locModel, 1, GL_FALSE, &cmd.modelMatrix[0][0]);
		if (m_pick.locEntityId >= 0)
			glUniform1ui(m_pick.locEntityId, cmd.entityId);

		const GLuint vao = cmd.obj->getVao();
		const GLsizei indexCount = cmd.obj->getIndexCount();
		const GLsizei vertexCount = cmd.obj->getVertexCount();
		glBindVertexArray(vao);
		if (indexCount > 0)
			glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
		else
			glDrawArrays(GL_TRIANGLES, 0, vertexCount);
	}

	glBindVertexArray(0);
	glUseProgram(0);
	// Restore viewport tab FBO so selection outline draws into the tab
	GLuint vpFbo = 0;
	int vpFboH = 0;
#if ENGINE_EDITOR
	for (const auto& tab : m_editorTabs)
	{
		if (tab.active && tab.renderTarget && tab.renderTarget->isValid())
		{
			vpFbo = static_cast<const OpenGLRenderTarget*>(tab.renderTarget.get())->getGLFramebuffer();
			vpFboH = tab.renderTarget->getHeight();
			break;
		}
	}
#endif
	glBindFramebuffer(GL_FRAMEBUFFER, vpFbo);
	// Restore content-rect viewport so subsequent passes (outline, gizmo) draw in the right area
	{
		const Vec4 vr = m_cachedViewportContentRect;
		if (vr.z > 0.0f && vr.w > 0.0f && vpFboH > 0)
			glViewport(static_cast<int>(vr.x), vpFboH - static_cast<int>(vr.y) - static_cast<int>(vr.w),
					   static_cast<int>(vr.z), static_cast<int>(vr.w));
	}
}

void OpenGLRenderer::renderPickBufferSelectedEntities(const glm::mat4& view, const glm::mat4& projection, const std::unordered_set<unsigned int>& entityIds)
{
	if (m_pick.fbo == 0 || m_pick.program == 0 || entityIds.empty())
		return;

	glBindFramebuffer(GL_FRAMEBUFFER, m_pick.fbo);
	glViewport(0, 0, m_pick.width, m_pick.height);
	const GLuint clearId = 0;
	glClearBufferuiv(GL_COLOR, 0, &clearId);
	glClear(GL_DEPTH_BUFFER_BIT);

	// Restrict pick rendering to the viewport content rect area
	const Vec4 vpRect = m_cachedViewportContentRect;
	if (vpRect.z > 0.0f && vpRect.w > 0.0f)
	{
		glViewport(static_cast<int>(vpRect.x),
				   m_pick.height - static_cast<int>(vpRect.y) - static_cast<int>(vpRect.w),
				   static_cast<int>(vpRect.z), static_cast<int>(vpRect.w));
	}

	glUseProgram(m_pick.program);
	if (m_pick.locView >= 0)
		glUniformMatrix4fv(m_pick.locView, 1, GL_FALSE, &view[0][0]);
	if (m_pick.locProjection >= 0)
		glUniformMatrix4fv(m_pick.locProjection, 1, GL_FALSE, &projection[0][0]);

	for (const auto& cmd : m_drawList)
	{
		if (!cmd.obj || entityIds.count(cmd.entityId) == 0)
			continue;

		if (m_pick.locModel >= 0)
			glUniformMatrix4fv(m_pick.locModel, 1, GL_FALSE, &cmd.modelMatrix[0][0]);
		if (m_pick.locEntityId >= 0)
			glUniform1ui(m_pick.locEntityId, cmd.entityId);

		const GLuint vao = cmd.obj->getVao();
		const GLsizei indexCount = cmd.obj->getIndexCount();
		const GLsizei vertexCount = cmd.obj->getVertexCount();
		glBindVertexArray(vao);
		if (indexCount > 0)
			glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
		else
			glDrawArrays(GL_TRIANGLES, 0, vertexCount);
	}

	glBindVertexArray(0);
	glUseProgram(0);
	GLuint vpFbo = 0;
	int vpFboH2 = 0;
#if ENGINE_EDITOR
	for (const auto& tab : m_editorTabs)
	{
		if (tab.active && tab.renderTarget && tab.renderTarget->isValid())
		{
			vpFbo = static_cast<const OpenGLRenderTarget*>(tab.renderTarget.get())->getGLFramebuffer();
			vpFboH2 = tab.renderTarget->getHeight();
			break;
		}
	}
#endif
	glBindFramebuffer(GL_FRAMEBUFFER, vpFbo);
	// Restore content-rect viewport
	{
		const Vec4 vr = m_cachedViewportContentRect;
		if (vr.z > 0.0f && vr.w > 0.0f && vpFboH2 > 0)
			glViewport(static_cast<int>(vr.x), vpFboH2 - static_cast<int>(vr.y) - static_cast<int>(vr.w),
					   static_cast<int>(vr.z), static_cast<int>(vr.w));
	}
}

unsigned int OpenGLRenderer::pickEntityAt(int x, int y)
{
	if (m_pick.fbo == 0)
		return 0;

	// Flip Y (SDL top-left â†’ GL bottom-left)
	const int glY = m_pick.height - 1 - y;
	if (x < 0 || x >= m_pick.width || glY < 0 || glY >= m_pick.height)
		return 0;

	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_pick.fbo);
	unsigned int entityId = 0;
	glReadPixels(x, glY, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &entityId);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
	return entityId;
}

unsigned int OpenGLRenderer::pickEntityAtImmediate(int x, int y)
{
	const Vec2 vs = getViewportSize();
	const int w = static_cast<int>(vs.x);
	const int h = static_cast<int>(vs.y);
	if (w <= 0 || h <= 0)
		return 0;

	if (!ensurePickFbo(w, h))
		return 0;

	renderPickBuffer(m_lastViewMatrix, m_projectionMatrix);
	return pickEntityAt(x, y);
}
#endif // ENGINE_EDITOR — pick FBO / pick entity

bool OpenGLRenderer::screenToWorldPos(int screenX, int screenY, Vec3& outWorldPos) const
{
	// Find the active tab FBO to read depth from
	GLuint fbo = 0;
	int fboW = 0, fboH = 0;
#if ENGINE_EDITOR
	for (const auto& tab : m_editorTabs)
	{
		if (tab.active && tab.renderTarget && tab.renderTarget->isValid())
		{
			fbo = static_cast<const OpenGLRenderTarget*>(tab.renderTarget.get())->getGLFramebuffer();
			fboW = tab.renderTarget->getWidth();
			fboH = tab.renderTarget->getHeight();
			break;
		}
	}
#endif

	if (fbo == 0)
	{
		const Vec2 vs = getViewportSize();
		fboW = static_cast<int>(vs.x);
		fboH = static_cast<int>(vs.y);
	}

	if (fboW <= 0 || fboH <= 0)
		return false;

	const int glY = fboH - 1 - screenY;
	if (screenX < 0 || screenX >= fboW || glY < 0 || glY >= fboH)
		return false;

	// Read depth at pixel
	glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
	float depth = 1.0f;
	glReadPixels(screenX, glY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	// depth == 1.0 means far plane (no geometry)
	if (depth >= 1.0f)
		return false;

	// Unproject: NDC -> world
	// The projection uses the viewport content rect, so NDC must be computed
	// relative to that area rather than the full FBO.
	const Vec4 vpRect = m_cachedViewportContentRect;
	float vpX = 0.0f, vpY = 0.0f, vpW = static_cast<float>(fboW), vpH = static_cast<float>(fboH);
	if (vpRect.z > 0.0f && vpRect.w > 0.0f)
	{
		vpX = vpRect.x;
		vpY = vpRect.y;
		vpW = vpRect.z;
		vpH = vpRect.w;
	}

	const float localX = static_cast<float>(screenX) - vpX;
	const float localY = static_cast<float>(screenY) - vpY;
	const float ndcX = (2.0f * localX / vpW) - 1.0f;
	const float ndcY = 1.0f - (2.0f * localY / vpH); // top-down screen Y → GL NDC Y
	const float ndcZ = 2.0f * depth - 1.0f;

	const glm::mat4 invViewProj = glm::inverse(m_projectionMatrix * m_lastViewMatrix);
	const glm::vec4 clipPos(ndcX, ndcY, ndcZ, 1.0f);
	glm::vec4 worldPos = invViewProj * clipPos;

	if (std::abs(worldPos.w) < 1e-7f)
		return false;

	worldPos /= worldPos.w;
	outWorldPos = Vec3{ worldPos.x, worldPos.y, worldPos.z };
	return true;
}

#if ENGINE_EDITOR
// â”€â”€â”€ Selection outline (post-process edge detection on pick buffer) â”€â”€â”€â”€â”€â”€â”€â”€â”€
bool OpenGLRenderer::ensureOutlineResources()
{
	if (m_outline.program != 0)
		return true;

	const std::string vs =
		"#version 460 core\n"
		"out vec2 vTexCoord;\n"
		"void main() {\n"
		"    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
		"    vTexCoord = pos;\n"
		"    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);\n"
		"}\n";

	const std::string fs =
		"#version 460 core\n"
		"out vec4 FragColor;\n"
		"in vec2 vTexCoord;\n"
		"uniform usampler2D uPickTex;\n"
		"uniform vec4 uOutlineColor;\n"
		"uniform float uThickness;\n"
		"void main() {\n"
		"    ivec2 coord = ivec2(gl_FragCoord.xy);\n"
		"    uint center = texelFetch(uPickTex, coord, 0).r;\n"
		"    bool centerIsSelected = (center != 0u);\n"
		"    int r = int(uThickness);\n"
		"    bool foundEdge = false;\n"
		"    for (int dy = -r; dy <= r && !foundEdge; ++dy) {\n"
		"        for (int dx = -r; dx <= r && !foundEdge; ++dx) {\n"
		"            if (dx == 0 && dy == 0) continue;\n"
		"            uint neighbor = texelFetch(uPickTex, coord + ivec2(dx, dy), 0).r;\n"
		"            bool neighborIsSelected = (neighbor != 0u);\n"
		"            if (centerIsSelected != neighborIsSelected) {\n"
		"                foundEdge = true;\n"
		"            }\n"
		"        }\n"
		"    }\n"
		"    if (foundEdge) {\n"
		"        FragColor = uOutlineColor;\n"
		"    } else {\n"
		"        discard;\n"
		"    }\n"
		"}\n";

	OpenGLShader vShader;
	OpenGLShader fShader;
	if (!vShader.loadFromSource(Shader::Type::Vertex, vs) ||
		!fShader.loadFromSource(Shader::Type::Fragment, fs))
		return false;

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vShader.id());
	glAttachShader(prog, fShader.id());
	glLinkProgram(prog);
	GLint linked = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &linked);
	if (!linked) { glDeleteProgram(prog); return false; }

	m_outline.program = prog;
	m_outline.locPickTex = glGetUniformLocation(prog, "uPickTex");
	m_outline.locOutlineColor = glGetUniformLocation(prog, "uOutlineColor");
	m_outline.locThickness = glGetUniformLocation(prog, "uThickness");

	glGenVertexArrays(1, &m_outline.vao);

	return true;
}

void OpenGLRenderer::releaseOutlineResources()
{
	if (m_outline.program) { glDeleteProgram(m_outline.program); m_outline.program = 0; }
	if (m_outline.vao) { glDeleteVertexArrays(1, &m_outline.vao); m_outline.vao = 0; }
}

glm::mat3 OpenGLRenderer::getEntityRotationMatrix(const ECS::TransformComponent& tc) const
{
	const float rx = glm::radians(tc.rotation[0]);
	const float ry = glm::radians(tc.rotation[1]);
	const float rz = glm::radians(tc.rotation[2]);
	// Must match the object rendering order: T * Rx * Ry * Rz * S
	const glm::mat4 rotMat =
		glm::rotate(glm::mat4(1.0f), rx, glm::vec3(1, 0, 0)) *
		glm::rotate(glm::mat4(1.0f), ry, glm::vec3(0, 1, 0)) *
		glm::rotate(glm::mat4(1.0f), rz, glm::vec3(0, 0, 1));
	return glm::mat3(rotMat);
}

#endif // ENGINE_EDITOR
// ── Rubber-Band (Marquee) Selection ─────────────────────────────────────────

#if ENGINE_EDITOR
void OpenGLRenderer::beginRubberBand(int screenX, int screenY)
{
	m_rubberBandActive = true;
	m_rubberBandStart = Vec2{ static_cast<float>(screenX), static_cast<float>(screenY) };
	m_rubberBandEnd = m_rubberBandStart;
}

void OpenGLRenderer::updateRubberBand(int screenX, int screenY)
{
	if (!m_rubberBandActive)
		return;
	m_rubberBandEnd = Vec2{ static_cast<float>(screenX), static_cast<float>(screenY) };
}

void OpenGLRenderer::endRubberBand(bool ctrlHeld)
{
	if (!m_rubberBandActive)
		return;
	m_rubberBandActive = false;

	// Only resolve if the rectangle has a meaningful size (> 4 px in both axes)
	const float dx = std::abs(m_rubberBandEnd.x - m_rubberBandStart.x);
	const float dy = std::abs(m_rubberBandEnd.y - m_rubberBandStart.y);
	if (dx > 4.0f && dy > 4.0f)
	{
		if (!ctrlHeld)
			m_selectedEntities.clear();
		resolveRubberBandSelection();
	}
}

void OpenGLRenderer::cancelRubberBand()
{
	m_rubberBandActive = false;
}

void OpenGLRenderer::resolveRubberBandSelection()
{
	// Ensure pick buffer is freshly rendered before reading
	const Vec2 vs = getViewportSize();
	const int vpW = static_cast<int>(vs.x);
	const int vpH = static_cast<int>(vs.y);
	if (vpW <= 0 || vpH <= 0)
		return;
	if (!ensurePickFbo(vpW, vpH))
		return;
	renderPickBuffer(m_lastViewMatrix, m_projectionMatrix);

	// Compute pixel rect in screen-space
	const int x0 = static_cast<int>(std::min(m_rubberBandStart.x, m_rubberBandEnd.x));
	const int y0 = static_cast<int>(std::min(m_rubberBandStart.y, m_rubberBandEnd.y));
	const int x1 = static_cast<int>(std::max(m_rubberBandStart.x, m_rubberBandEnd.x));
	const int y1 = static_cast<int>(std::max(m_rubberBandStart.y, m_rubberBandEnd.y));

	const int rectW = x1 - x0;
	const int rectH = y1 - y0;
	if (rectW <= 0 || rectH <= 0)
		return;

	// Clamp to pick-buffer dimensions
	const int rx0 = std::max(0, std::min(x0, m_pick.width - 1));
	const int ry0 = std::max(0, std::min(y0, m_pick.height - 1));
	const int rx1 = std::max(0, std::min(x1, m_pick.width));
	const int ry1 = std::max(0, std::min(y1, m_pick.height));

	const int clampedW = rx1 - rx0;
	const int clampedH = ry1 - ry0;
	if (clampedW <= 0 || clampedH <= 0)
		return;

	// Read a block of entity IDs from the pick buffer (GL Y is flipped)
	const int glY = m_pick.height - ry1; // bottom-left in GL coords
	std::vector<unsigned int> pixels(static_cast<size_t>(clampedW) * clampedH, 0u);

	glBindFramebuffer(GL_READ_FRAMEBUFFER, m_pick.fbo);
	glReadPixels(rx0, glY, clampedW, clampedH, GL_RED_INTEGER, GL_UNSIGNED_INT, pixels.data());
	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);

	// Collect unique non-zero entity IDs
	for (unsigned int eid : pixels)
	{
		if (eid != 0)
			m_selectedEntities.insert(eid);
	}
}
#endif // ENGINE_EDITOR — Rubber-Band Selection

// ---------------------------------------------------------------------------
// Skeletal animation queries
// ---------------------------------------------------------------------------

bool OpenGLRenderer::isEntitySkinned(unsigned int entity) const
{
	return m_entityAnimators.find(entity) != m_entityAnimators.end();
}

int OpenGLRenderer::getEntityAnimationClipCount(unsigned int entity) const
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return 0;
	const Skeleton* sk = it->second->getSkeleton();
	return sk ? static_cast<int>(sk->animations.size()) : 0;
}

Renderer::AnimationClipInfo OpenGLRenderer::getEntityAnimationClipInfo(unsigned int entity, int clipIndex) const
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return {};
	const Skeleton* sk = it->second->getSkeleton();
	if (!sk || clipIndex < 0 || clipIndex >= static_cast<int>(sk->animations.size())) return {};
	const auto& clip = sk->animations[clipIndex];
	AnimationClipInfo info;
	info.name = clip.name;
	info.duration = clip.duration;
	info.ticksPerSecond = clip.ticksPerSecond;
	info.channelCount = static_cast<int>(clip.channels.size());
	return info;
}

int OpenGLRenderer::getEntityAnimatorCurrentClip(unsigned int entity) const
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return -1;
	return it->second->getCurrentClipIndex();
}

float OpenGLRenderer::getEntityAnimatorCurrentTime(unsigned int entity) const
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return 0.0f;
	return it->second->getCurrentTime();
}

bool OpenGLRenderer::isEntityAnimatorPlaying(unsigned int entity) const
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return false;
	return it->second->isPlaying();
}

void OpenGLRenderer::playEntityAnimation(unsigned int entity, int clipIndex, bool loop)
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return;
	it->second->playAnimation(clipIndex, loop);
}

void OpenGLRenderer::stopEntityAnimation(unsigned int entity)
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return;
	it->second->stop();
}

void OpenGLRenderer::setEntityAnimationSpeed(unsigned int entity, float speed)
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return;
	it->second->setSpeed(speed);
}

int OpenGLRenderer::getEntityBoneCount(unsigned int entity) const
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return 0;
	const Skeleton* sk = it->second->getSkeleton();
	return sk ? static_cast<int>(sk->bones.size()) : 0;
}

std::string OpenGLRenderer::getEntityBoneName(unsigned int entity, int boneIndex) const
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return {};
	const Skeleton* sk = it->second->getSkeleton();
	if (!sk || boneIndex < 0 || boneIndex >= static_cast<int>(sk->bones.size())) return {};
	return sk->bones[boneIndex].name;
}

int OpenGLRenderer::getEntityBoneParent(unsigned int entity, int boneIndex) const
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return -1;
	const Skeleton* sk = it->second->getSkeleton();
	if (!sk || boneIndex < 0 || boneIndex >= static_cast<int>(sk->bones.size())) return -1;
	return sk->bones[boneIndex].parentIndex;
}

// ── Skeletal animation: crossfade & layers ──────────────────────────

void OpenGLRenderer::crossfadeEntityAnimation(unsigned int entity, int toClip, float duration, bool loop)
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return;
	it->second->crossfade(toClip, duration, loop);
}

bool OpenGLRenderer::isEntityCrossfading(unsigned int entity) const
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return false;
	return it->second->isCrossfading();
}

void OpenGLRenderer::playEntityAnimationOnLayer(unsigned int entity, int layer, int clip, bool loop, float crossfadeDur)
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return;
	it->second->playOnLayer(layer, clip, loop, crossfadeDur);
}

void OpenGLRenderer::stopEntityAnimationLayer(unsigned int entity, int layer)
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return;
	it->second->stopLayer(layer);
}

void OpenGLRenderer::setEntityLayerWeight(unsigned int entity, int layer, float weight)
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return;
	it->second->setLayerWeight(layer, weight);
}

int OpenGLRenderer::getEntityAnimationLayerCount(unsigned int entity) const
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return 0;
	return it->second->getLayerCount();
}

void OpenGLRenderer::setEntityAnimationLayerCount(unsigned int entity, int count)
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return;
	it->second->setLayerCount(count);
}

int OpenGLRenderer::findEntityAnimationClipByName(unsigned int entity, const char* name) const
{
	auto it = m_entityAnimators.find(entity);
	if (it == m_entityAnimators.end()) return -1;
	const Skeleton* sk = it->second->getSkeleton();
	if (!sk || !name) return -1;
	for (int i = 0; i < static_cast<int>(sk->animations.size()); ++i)
		if (sk->animations[i].name == name) return i;
	return -1;
}
