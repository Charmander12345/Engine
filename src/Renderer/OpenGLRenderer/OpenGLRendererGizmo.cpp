// OpenGLRendererGizmo.cpp – Gizmo + Grid methods split from OpenGLRenderer.cpp
// Contains: ensureGizmoResources, releaseGizmoResources, ensureGridResources,
//           releaseGridResources, drawViewportGrid, getGizmoWorldAxis,
//           renderGizmo, pickGizmoAxis, beginGizmoDrag, updateGizmoDrag, endGizmoDrag

#include "OpenGLRenderer.h"
#include <cmath>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "../../Core/UndoRedoManager.h"

#if ENGINE_EDITOR

namespace
{
	static void buildCircleVerts(std::vector<float>& verts, int segments, float radius, int axis)
	{
		for (int i = 0; i <= segments; ++i)
		{
			const float angle = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(segments);
			const float c = std::cos(angle) * radius;
			const float s = std::sin(angle) * radius;
			switch (axis)
			{
			case 0: verts.push_back(0.0f); verts.push_back(c);    verts.push_back(s);    break;
			case 1: verts.push_back(c);    verts.push_back(0.0f); verts.push_back(s);    break;
			case 2: verts.push_back(c);    verts.push_back(s);    verts.push_back(0.0f); break;
			}
		}
	}
}

// ============================================================================
// Editor Gizmos
// ============================================================================

bool OpenGLRenderer::ensureGizmoResources()
{
	if (m_gizmo.program != 0)
		return true;

	const char* vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
	gl_Position = uMVP * vec4(aPos, 1.0);
}
)";
	const char* fs = R"(
#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main() {
	FragColor = vec4(uColor, 1.0);
}
)";

	GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vsh, 1, &vs, nullptr);
	glCompileShader(vsh);
	GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fsh, 1, &fs, nullptr);
	glCompileShader(fsh);
	m_gizmo.program = glCreateProgram();
	glAttachShader(m_gizmo.program, vsh);
	glAttachShader(m_gizmo.program, fsh);
	glLinkProgram(m_gizmo.program);
	glDeleteShader(vsh);
	glDeleteShader(fsh);

	m_gizmo.locMVP = glGetUniformLocation(m_gizmo.program, "uMVP");
	m_gizmo.locColor = glGetUniformLocation(m_gizmo.program, "uColor");

	glGenVertexArrays(1, &m_gizmo.vao);
	glGenBuffers(1, &m_gizmo.vbo);
	glBindVertexArray(m_gizmo.vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
	glBufferData(GL_ARRAY_BUFFER, 4096 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glBindVertexArray(0);

	return true;
}

void OpenGLRenderer::releaseGizmoResources()
{
	if (m_gizmo.program) { glDeleteProgram(m_gizmo.program); m_gizmo.program = 0; }
	if (m_gizmo.vbo) { glDeleteBuffers(1, &m_gizmo.vbo); m_gizmo.vbo = 0; }
	if (m_gizmo.vao) { glDeleteVertexArrays(1, &m_gizmo.vao); m_gizmo.vao = 0; }
}

// ============================================================================
// Viewport Grid (infinite XZ plane)
// ============================================================================

bool OpenGLRenderer::ensureGridResources()
{
	if (m_grid.program != 0)
		return true;

	const char* vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uViewProj;
out vec3 vWorldPos;
void main() {
	vWorldPos = aPos;
	gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

	const char* fs = R"(
#version 330 core
in vec3 vWorldPos;
uniform vec3  uCameraPos;
uniform float uGridSize;
uniform vec4  uColor;
uniform float uFadeRadius;
out vec4 FragColor;
void main() {
	// Grid lines via screen-space derivatives
	vec2 coord = vWorldPos.xz / uGridSize;
	vec2 grid  = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
	float line = min(grid.x, grid.y);
	float alpha = 1.0 - min(line, 1.0);

	// Thicker lines every 10 grid units
	vec2 coord10 = vWorldPos.xz / (uGridSize * 10.0);
	vec2 grid10  = abs(fract(coord10 - 0.5) - 0.5) / fwidth(coord10);
	float line10 = min(grid10.x, grid10.y);
	float alpha10 = 1.0 - min(line10, 1.0);
	alpha = max(alpha * 0.35, alpha10 * 0.7);

	// Axis highlight: red for X (Z~=0), blue for Z (X~=0)
	float axisWidth = uGridSize * 0.05;
	vec3 lineColor = uColor.rgb;
	if (abs(vWorldPos.z) < axisWidth) {
		lineColor = vec3(0.8, 0.2, 0.2);
		alpha = max(alpha, 0.8);
	}
	if (abs(vWorldPos.x) < axisWidth) {
		lineColor = vec3(0.2, 0.2, 0.8);
		alpha = max(alpha, 0.8);
	}

	// Distance fade
	float dist = length(vWorldPos.xz - uCameraPos.xz);
	float fade = 1.0 - smoothstep(uFadeRadius * 0.6, uFadeRadius, dist);
	alpha *= fade * uColor.a;

	if (alpha < 0.005) discard;
	FragColor = vec4(lineColor, alpha);
}
)";

	GLuint vsh = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vsh, 1, &vs, nullptr);
	glCompileShader(vsh);
	GLuint fsh = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fsh, 1, &fs, nullptr);
	glCompileShader(fsh);
	m_grid.program = glCreateProgram();
	glAttachShader(m_grid.program, vsh);
	glAttachShader(m_grid.program, fsh);
	glLinkProgram(m_grid.program);
	glDeleteShader(vsh);
	glDeleteShader(fsh);

	m_grid.locViewProj  = glGetUniformLocation(m_grid.program, "uViewProj");
	m_grid.locCameraPos = glGetUniformLocation(m_grid.program, "uCameraPos");
	m_grid.locGridSize  = glGetUniformLocation(m_grid.program, "uGridSize");
	m_grid.locColor     = glGetUniformLocation(m_grid.program, "uColor");
	m_grid.locFadeRadius = glGetUniformLocation(m_grid.program, "uFadeRadius");

	// Large quad on XZ plane (Y=0)
	const float S = 500.0f;
	float verts[] = {
		-S, 0.0f, -S,
		 S, 0.0f, -S,
		 S, 0.0f,  S,
		-S, 0.0f, -S,
		 S, 0.0f,  S,
		-S, 0.0f,  S,
	};

	glGenVertexArrays(1, &m_grid.vao);
	glGenBuffers(1, &m_grid.vbo);
	glBindVertexArray(m_grid.vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_grid.vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glBindVertexArray(0);

	return true;
}

void OpenGLRenderer::releaseGridResources()
{
	if (m_grid.program) { glDeleteProgram(m_grid.program); m_grid.program = 0; }
	if (m_grid.vbo) { glDeleteBuffers(1, &m_grid.vbo); m_grid.vbo = 0; }
	if (m_grid.vao) { glDeleteVertexArrays(1, &m_grid.vao); m_grid.vao = 0; }
}

void OpenGLRenderer::drawViewportGrid(const glm::mat4& view, const glm::mat4& projection)
{
	if (!m_gridVisible || !ensureGridResources())
		return;

	const glm::mat4 vp = projection * view;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	glEnable(GL_DEPTH_TEST);

	glUseProgram(m_grid.program);
	glUniformMatrix4fv(m_grid.locViewProj, 1, GL_FALSE, glm::value_ptr(vp));

	const glm::vec3 camPos = m_camera ? glm::vec3{
		m_camera->getPosition().x, m_camera->getPosition().y, m_camera->getPosition().z
	} : glm::vec3(0.0f);
	glUniform3fv(m_grid.locCameraPos, 1, glm::value_ptr(camPos));
	glUniform1f(m_grid.locGridSize, m_gridSize);
	glUniform4f(m_grid.locColor, 0.5f, 0.5f, 0.5f, 0.6f);
	glUniform1f(m_grid.locFadeRadius, 200.0f);

	glBindVertexArray(m_grid.vao);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);

	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
}

// ============================================================================
// Gizmo interaction
// ============================================================================

glm::vec3 OpenGLRenderer::getGizmoWorldAxis(const ECS::TransformComponent& tc, int axisIdx) const
{
	const glm::mat3 rot = getEntityRotationMatrix(tc);
	return glm::normalize(rot[axisIdx]);
}

void OpenGLRenderer::renderGizmo(const glm::mat4& view, const glm::mat4& projection)
{
	if (m_selectedEntities.empty() || m_gizmo.mode == GizmoMode::None)
		return;

	const unsigned int primaryEntity = *m_selectedEntities.begin();
	auto& ecs = ECS::ECSManager::Instance();
	const auto* tc = ecs.getComponent<ECS::TransformComponent>(primaryEntity);
	if (!tc)
		return;

	if (!ensureGizmoResources())
		return;

	const glm::vec3 entityPos{ tc->position[0], tc->position[1], tc->position[2] };

	const glm::vec4 viewPos = view * glm::vec4(entityPos, 1.0f);
	const float dist = std::abs(viewPos.z);
	const float gizmoScale = dist * 0.12f;

	// Build model matrix: translate to entity, apply entity rotation, then scale
	const glm::mat3 rotMat = getEntityRotationMatrix(*tc);
	glm::mat4 model = glm::translate(glm::mat4(1.0f), entityPos);
	model *= glm::mat4(rotMat);
	model = model * glm::scale(glm::mat4(1.0f), glm::vec3(gizmoScale));
	const glm::mat4 mvp = projection * view * model;

	glUseProgram(m_gizmo.program);
	glUniformMatrix4fv(m_gizmo.locMVP, 1, GL_FALSE, &mvp[0][0]);
	glBindVertexArray(m_gizmo.vao);

	glDisable(GL_DEPTH_TEST);
	glLineWidth(4.0f);

	const glm::vec3 axisColors[3] = {
		{ 1.0f, 0.2f, 0.2f },
		{ 0.2f, 1.0f, 0.2f },
		{ 0.3f, 0.3f, 1.0f }
	};

	if (m_gizmo.mode == GizmoMode::Translate)
	{
		for (int a = 0; a < 3; ++a)
		{
			const bool hovered = (static_cast<int>(m_gizmo.hoveredAxis) - 1 == a) ||
								 (static_cast<int>(m_gizmo.activeAxis) - 1 == a);
			glm::vec3 col = hovered ? glm::vec3(1.0f, 1.0f, 0.3f) : axisColors[a];
			glUniform3fv(m_gizmo.locColor, 1, &col[0]);

			// In local space, axes are simply (1,0,0), (0,1,0), (0,0,1)
			glm::vec3 dir(0.0f); dir[a] = 1.0f;
			glm::vec3 up(0.0f); up[(a + 1) % 3] = 1.0f;
			const float arrowLen = 0.15f;
			const glm::vec3 tip = dir;
			const glm::vec3 back = dir * (1.0f - arrowLen);

			float verts[] = {
				0.0f, 0.0f, 0.0f, dir.x, dir.y, dir.z,
				tip.x, tip.y, tip.z, back.x + up.x * arrowLen * 0.3f, back.y + up.y * arrowLen * 0.3f, back.z + up.z * arrowLen * 0.3f,
				tip.x, tip.y, tip.z, back.x - up.x * arrowLen * 0.3f, back.y - up.y * arrowLen * 0.3f, back.z - up.z * arrowLen * 0.3f
			};

			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
			glDrawArrays(GL_LINES, 0, 6);
		}
	}
	else if (m_gizmo.mode == GizmoMode::Rotate)
	{
		constexpr int segments = 48;
		for (int a = 0; a < 3; ++a)
		{
			const bool hovered = (static_cast<int>(m_gizmo.hoveredAxis) - 1 == a) ||
								 (static_cast<int>(m_gizmo.activeAxis) - 1 == a);
			glm::vec3 col = hovered ? glm::vec3(1.0f, 1.0f, 0.3f) : axisColors[a];
			glUniform3fv(m_gizmo.locColor, 1, &col[0]);

			std::vector<float> circleVerts;
			circleVerts.reserve((segments + 1) * 3);
			buildCircleVerts(circleVerts, segments, 1.0f, a);

			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, circleVerts.size() * sizeof(float), circleVerts.data());
			glDrawArrays(GL_LINE_STRIP, 0, segments + 1);
		}
	}
	else if (m_gizmo.mode == GizmoMode::Scale)
	{
		for (int a = 0; a < 3; ++a)
		{
			const bool hovered = (static_cast<int>(m_gizmo.hoveredAxis) - 1 == a) ||
								 (static_cast<int>(m_gizmo.activeAxis) - 1 == a);
			glm::vec3 col = hovered ? glm::vec3(1.0f, 1.0f, 0.3f) : axisColors[a];
			glUniform3fv(m_gizmo.locColor, 1, &col[0]);

			glm::vec3 dir(0.0f); dir[a] = 1.0f;
			const float cubeSize = 0.06f;
			const glm::vec3 tip = dir;

			float shaft[] = { 0.0f, 0.0f, 0.0f, tip.x, tip.y, tip.z };
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(shaft), shaft);
			glDrawArrays(GL_LINES, 0, 2);

			glm::vec3 u(0.0f), v(0.0f);
			u[(a + 1) % 3] = cubeSize;
			v[(a + 2) % 3] = cubeSize;
			float cube[] = {
				tip.x - u.x, tip.y - u.y, tip.z - u.z, tip.x + u.x, tip.y + u.y, tip.z + u.z,
				tip.x - v.x, tip.y - v.y, tip.z - v.z, tip.x + v.x, tip.y + v.y, tip.z + v.z,
			};
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(cube), cube);
			glDrawArrays(GL_LINES, 0, 4);
		}
	}

	glEnable(GL_DEPTH_TEST);
	glLineWidth(1.0f);
	glBindVertexArray(0);
}

// Pick gizmo axis
OpenGLRenderer::GizmoAxis OpenGLRenderer::pickGizmoAxis(const glm::mat4& view, const glm::mat4& projection, int screenX, int screenY) const
{
	if (m_selectedEntities.empty() || m_gizmo.mode == GizmoMode::None)
		return GizmoAxis::None;

	const unsigned int primaryEntity = *m_selectedEntities.begin();
	auto& ecs = ECS::ECSManager::Instance();
	const auto* tc = ecs.getComponent<ECS::TransformComponent>(primaryEntity);
	if (!tc)
		return GizmoAxis::None;

	const glm::vec3 entityPos{ tc->position[0], tc->position[1], tc->position[2] };
	const glm::vec4 viewPos = view * glm::vec4(entityPos, 1.0f);
	const float dist = std::abs(viewPos.z);
	const float gizmoScale = dist * 0.12f;

	const int w = m_cachedWindowWidth > 0 ? m_cachedWindowWidth : 1;
	const int h = m_cachedWindowHeight > 0 ? m_cachedWindowHeight : 1;

	// When the viewport content rect is available, NDC maps to the content
	// rect area (not the full window) because the projection uses the
	// content rect aspect ratio and glViewport is set to that area.
	const Vec4 gvp = m_cachedViewportContentRect;
	const float gvpX = (gvp.z > 0.0f && gvp.w > 0.0f) ? gvp.x : 0.0f;
	const float gvpY = (gvp.z > 0.0f && gvp.w > 0.0f) ? gvp.y : 0.0f;
	const float gvpW = (gvp.z > 0.0f && gvp.w > 0.0f) ? gvp.z : static_cast<float>(w);
	const float gvpH = (gvp.z > 0.0f && gvp.w > 0.0f) ? gvp.w : static_cast<float>(h);

	const auto project = [&](const glm::vec3& worldPos) -> glm::vec2
	{
		const glm::vec4 clip = projection * view * glm::vec4(worldPos, 1.0f);
		if (clip.w <= 0.0001f) return { -9999.0f, -9999.0f };
		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		return {
			gvpX + (ndc.x * 0.5f + 0.5f) * gvpW,
			gvpY + (1.0f - (ndc.y * 0.5f + 0.5f)) * gvpH
		};
	};

	const auto distToSegment = [](glm::vec2 p, glm::vec2 a, glm::vec2 b) -> float
	{
		const glm::vec2 ab = b - a;
		const float lenSq = glm::dot(ab, ab);
		if (lenSq < 0.0001f) return glm::length(p - a);
		const float t = glm::clamp(glm::dot(p - a, ab) / lenSq, 0.0f, 1.0f);
		return glm::length(p - (a + ab * t));
	};

	const glm::vec2 mousePos{ static_cast<float>(screenX), static_cast<float>(screenY) };
	const glm::vec2 origin = project(entityPos);

	// Skip if entity center is behind camera
	if (origin.x < -9000.0f) return GizmoAxis::None;

	GizmoAxis best = GizmoAxis::None;
	constexpr float kPickThreshold = 22.0f; // pixel threshold
	float bestDist = kPickThreshold;

	if (m_gizmo.mode == GizmoMode::Rotate)
	{
		// For rotation gizmos, test distance to the projected circle (ring)
		const glm::mat3 rotMat = getEntityRotationMatrix(*tc);
		constexpr int pickSegments = 32;

		for (int a = 0; a < 3; ++a)
		{
			float minDist = kPickThreshold;
			glm::vec2 prevScreen{ -9999.0f, -9999.0f };

			for (int i = 0; i <= pickSegments; ++i)
			{
				const float angle = 2.0f * 3.14159265f * static_cast<float>(i) / static_cast<float>(pickSegments);
				const float ca = std::cos(angle);
				const float sa = std::sin(angle);
				glm::vec3 localPt(0.0f);
				switch (a)
				{
				case 0: localPt = glm::vec3(0.0f, ca, sa); break; // YZ circle, perpendicular to X
				case 1: localPt = glm::vec3(ca, 0.0f, sa); break; // XZ circle, perpendicular to Y
				case 2: localPt = glm::vec3(ca, sa, 0.0f); break; // XY circle, perpendicular to Z
				}
				const glm::vec3 worldPt = entityPos + rotMat * localPt * gizmoScale;
				const glm::vec2 screenPt = project(worldPt);

				if (i > 0 && prevScreen.x > -9000.0f && screenPt.x > -9000.0f)
				{
					const float d = distToSegment(mousePos, prevScreen, screenPt);
					if (d < minDist)
					{
						minDist = d;
					}
				}
				prevScreen = screenPt;
			}

			if (minDist < bestDist)
			{
				bestDist = minDist;
				best = static_cast<GizmoAxis>(a + 1);
			}
		}
	}
	else
	{
		// For translate / scale, test distance to the axis line segment
		for (int a = 0; a < 3; ++a)
		{
			const glm::vec3 worldAxis = getGizmoWorldAxis(*tc, a);
			const glm::vec2 tip = project(entityPos + worldAxis * gizmoScale);
			if (tip.x < -9000.0f) continue;

			const float d = distToSegment(mousePos, origin, tip);
			if (d < bestDist)
			{
				bestDist = d;
				best = static_cast<GizmoAxis>(a + 1);
			}
		}
	}

	return best;
}

// Helper: unproject screen coords to a world ray
static void screenToRay(int screenX, int screenY, int w, int h,
	const glm::mat4& invVP, glm::vec3& rayOrigin, glm::vec3& rayDir)
{
	const float nx = (2.0f * static_cast<float>(screenX) / static_cast<float>(w)) - 1.0f;
	const float ny = 1.0f - (2.0f * static_cast<float>(screenY) / static_cast<float>(h));

	glm::vec4 nearClip = invVP * glm::vec4(nx, ny, -1.0f, 1.0f);
	glm::vec4 farClip  = invVP * glm::vec4(nx, ny, 1.0f, 1.0f);
	nearClip /= nearClip.w;
	farClip  /= farClip.w;

	rayOrigin = glm::vec3(nearClip);
	rayDir = glm::normalize(glm::vec3(farClip) - glm::vec3(nearClip));
}

// Project a point onto the closest point on a ray (parameterized by t)
static float closestTOnAxis(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
	const glm::vec3& axisOrigin, const glm::vec3& axisDir)
{
	// Find the parameter along axisDir where the two rays are closest
	// Using the standard closest-approach formula for two lines
	const glm::vec3 w0 = axisOrigin - rayOrigin;
	const float a = glm::dot(axisDir, axisDir);
	const float b = glm::dot(axisDir, rayDir);
	const float c = glm::dot(rayDir, rayDir);
	const float d = glm::dot(axisDir, w0);
	const float e = glm::dot(rayDir, w0);
	const float denom = a * c - b * b;
	if (std::abs(denom) < 1e-8f) return 0.0f;
	return (b * e - c * d) / denom;
}

bool OpenGLRenderer::beginGizmoDrag(int screenX, int screenY)
{
	if (m_selectedEntities.empty() || m_gizmo.mode == GizmoMode::None || !m_camera)
		return false;

	const unsigned int primaryEntity = *m_selectedEntities.begin();
	const Mat4 engineView = m_camera->getViewMatrixColumnMajor();
	const glm::mat4 view = glm::make_mat4(engineView.m);

	const GizmoAxis axis = pickGizmoAxis(view, m_projectionMatrix, screenX, screenY);
	if (axis == GizmoAxis::None)
		return false;

	auto& ecs = ECS::ECSManager::Instance();
	const auto* tc = ecs.getComponent<ECS::TransformComponent>(primaryEntity);
	if (!tc)
		return false;

	const int axisIdx = static_cast<int>(axis) - 1;
	const glm::vec3 entityPos{ tc->position[0], tc->position[1], tc->position[2] };
	const glm::vec3 worldAxis = getGizmoWorldAxis(*tc, axisIdx);

	m_gizmo.activeAxis = axis;
	m_gizmo.dragging = true;
	m_gizmo.dragEntityStart = entityPos;
	m_gizmo.dragWorldAxis = worldAxis;
	m_gizmo.dragRotStart = tc->rotation[axisIdx];
	m_gizmo.dragScaleStart = tc->scale[axisIdx];
	m_gizmo.dragStartScreen = glm::vec2{ static_cast<float>(screenX), static_cast<float>(screenY) };
	m_gizmo.dragOldTransform = *tc;

	// Store old transforms for ALL selected entities (group undo)
	m_gizmo.dragOldTransforms.clear();
	for (unsigned int eid : m_selectedEntities)
	{
		const auto* etc = ecs.getComponent<ECS::TransformComponent>(eid);
		if (etc)
			m_gizmo.dragOldTransforms[eid] = *etc;
	}

	// Compute the initial parameter along the axis ray
	const Vec4 bgvp = m_cachedViewportContentRect;
	const int bw = (bgvp.z > 0.0f && bgvp.w > 0.0f) ? static_cast<int>(bgvp.z) : (m_cachedWindowWidth > 0 ? m_cachedWindowWidth : 1);
	const int bh = (bgvp.z > 0.0f && bgvp.w > 0.0f) ? static_cast<int>(bgvp.w) : (m_cachedWindowHeight > 0 ? m_cachedWindowHeight : 1);
	const int bsx = screenX - static_cast<int>((bgvp.z > 0.0f) ? bgvp.x : 0.0f);
	const int bsy = screenY - static_cast<int>((bgvp.w > 0.0f) ? bgvp.y : 0.0f);
	const glm::mat4 invVP = glm::inverse(m_projectionMatrix * view);
	glm::vec3 rayO, rayD;
	screenToRay(bsx, bsy, bw, bh, invVP, rayO, rayD);
	m_gizmo.dragStartT = closestTOnAxis(rayO, rayD, entityPos, worldAxis);

	return true;
}

void OpenGLRenderer::updateGizmoDrag(int screenX, int screenY)
{
	if (!m_gizmo.dragging || m_gizmo.activeAxis == GizmoAxis::None || m_selectedEntities.empty() || !m_camera)
		return;

	const unsigned int primaryEntity = *m_selectedEntities.begin();
	auto& ecs = ECS::ECSManager::Instance();
	auto* tc = ecs.getComponent<ECS::TransformComponent>(primaryEntity);
	if (!tc)
		return;

	const int axisIdx = static_cast<int>(m_gizmo.activeAxis) - 1;
	const Mat4 engineView = m_camera->getViewMatrixColumnMajor();
	const glm::mat4 view = glm::make_mat4(engineView.m);

	const Vec4 ugvp = m_cachedViewportContentRect;
	const int w = (ugvp.z > 0.0f && ugvp.w > 0.0f) ? static_cast<int>(ugvp.z) : (m_cachedWindowWidth > 0 ? m_cachedWindowWidth : 1);
	const int h = (ugvp.z > 0.0f && ugvp.w > 0.0f) ? static_cast<int>(ugvp.w) : (m_cachedWindowHeight > 0 ? m_cachedWindowHeight : 1);
	const int localScreenX = screenX - static_cast<int>((ugvp.z > 0.0f) ? ugvp.x : 0.0f);
	const int localScreenY = screenY - static_cast<int>((ugvp.w > 0.0f) ? ugvp.y : 0.0f);

	if (m_gizmo.mode == GizmoMode::Translate)
	{
		// Ray-plane intersection for 1:1 movement
		const glm::mat4 invVP = glm::inverse(m_projectionMatrix * view);
		glm::vec3 rayO, rayD;
		screenToRay(localScreenX, localScreenY, w, h, invVP, rayO, rayD);

		const float currentT = closestTOnAxis(rayO, rayD, m_gizmo.dragEntityStart, m_gizmo.dragWorldAxis);
		const float deltaT = currentT - m_gizmo.dragStartT;

		// Move entity along the world-space axis
		glm::vec3 newPos = m_gizmo.dragEntityStart + m_gizmo.dragWorldAxis * deltaT;

		// Snap to grid when enabled
		if (m_snapEnabled && m_gridSize > 0.0f)
		{
			newPos.x = std::round(newPos.x / m_gridSize) * m_gridSize;
			newPos.y = std::round(newPos.y / m_gridSize) * m_gridSize;
			newPos.z = std::round(newPos.z / m_gridSize) * m_gridSize;
		}

		tc->position[0] = newPos.x;
		tc->position[1] = newPos.y;
		tc->position[2] = newPos.z;
	}
	else if (m_gizmo.mode == GizmoMode::Rotate)
	{
		// Compute screen-space delta angle from mouse movement perpendicular
		// to the projected rotation axis.
		const float rpX = (ugvp.z > 0.0f) ? ugvp.x : 0.0f;
		const float rpY = (ugvp.w > 0.0f) ? ugvp.y : 0.0f;
		const auto project = [&](const glm::vec3& worldPos) -> glm::vec2
		{
			const glm::vec4 clip = m_projectionMatrix * view * glm::vec4(worldPos, 1.0f);
			if (clip.w <= 0.0001f) return { 0.0f, 0.0f };
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			return {
				rpX + (ndc.x * 0.5f + 0.5f) * static_cast<float>(w),
				rpY + (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(h)
			};
		};

		const glm::vec2 originScreen = project(m_gizmo.dragEntityStart);
		const glm::vec2 axisTipScreen = project(m_gizmo.dragEntityStart + m_gizmo.dragWorldAxis);
		glm::vec2 axisScreenDir = axisTipScreen - originScreen;
		const float axisScreenLen = glm::length(axisScreenDir);
		if (axisScreenLen < 1.0f) return;

		const glm::vec2 axisPerp{ -axisScreenDir.y / axisScreenLen, axisScreenDir.x / axisScreenLen };
		const glm::vec2 mouseDelta{
			static_cast<float>(screenX) - m_gizmo.dragStartScreen.x,
			static_cast<float>(screenY) - m_gizmo.dragStartScreen.y
		};
		const float projectedPixels = glm::dot(mouseDelta, axisPerp);
		const float deltaAngleRad = glm::radians(projectedPixels * 0.5f);

		// Build the old rotation matrix from the drag-start snapshot
		const glm::mat3 oldRot = getEntityRotationMatrix(m_gizmo.dragOldTransform);

		// Apply incremental rotation in local space around the selected unit axis
		glm::vec3 localAxis(0.0f);
		localAxis[axisIdx] = 1.0f;
		const glm::mat3 deltaRot = glm::mat3(glm::rotate(glm::mat4(1.0f), deltaAngleRad, localAxis));
		const glm::mat3 newRot = oldRot * deltaRot;

		// Decompose back to Euler XYZ (matching Rx * Ry * Rz order)
		// R[2][0] = sin(ry)
		const float sinRy = glm::clamp(newRot[2][0], -1.0f, 1.0f);
		const float ry = std::asin(sinRy);
		float rx, rz;
		if (std::abs(std::cos(ry)) > 1e-4f)
		{
			rx = std::atan2(-newRot[2][1], newRot[2][2]);
			rz = std::atan2(-newRot[1][0], newRot[0][0]);
		}
		else
		{
			// Gimbal lock fallback
			rx = std::atan2(newRot[0][1], newRot[1][1]);
			rz = 0.0f;
		}

		float degX = glm::degrees(rx);
		float degY = glm::degrees(ry);
		float degZ = glm::degrees(rz);

		// Snap rotation to fixed degree increments when enabled
		if (m_snapEnabled && m_rotationSnapDeg > 0.0f)
		{
			degX = std::round(degX / m_rotationSnapDeg) * m_rotationSnapDeg;
			degY = std::round(degY / m_rotationSnapDeg) * m_rotationSnapDeg;
			degZ = std::round(degZ / m_rotationSnapDeg) * m_rotationSnapDeg;
		}

		tc->rotation[0] = degX;
		tc->rotation[1] = degY;
		tc->rotation[2] = degZ;
	}
	else if (m_gizmo.mode == GizmoMode::Scale)
	{
		// Use screen-space pixel delta projected onto the screen-space axis direction
		const float spX = (ugvp.z > 0.0f) ? ugvp.x : 0.0f;
		const float spY = (ugvp.w > 0.0f) ? ugvp.y : 0.0f;
		const auto project = [&](const glm::vec3& worldPos) -> glm::vec2
		{
			const glm::vec4 clip = m_projectionMatrix * view * glm::vec4(worldPos, 1.0f);
			if (clip.w <= 0.0001f) return { 0.0f, 0.0f };
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			return {
				spX + (ndc.x * 0.5f + 0.5f) * static_cast<float>(w),
				spY + (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(h)
			};
		};

		const glm::vec2 originScreen = project(m_gizmo.dragEntityStart);
		const glm::vec2 axisTipScreen = project(m_gizmo.dragEntityStart + m_gizmo.dragWorldAxis);
		const glm::vec2 axisScreenDir = axisTipScreen - originScreen;
		const float axisScreenLen = glm::length(axisScreenDir);
		if (axisScreenLen < 1.0f) return;

		const glm::vec2 axisNorm = axisScreenDir / axisScreenLen;
		const glm::vec2 mouseDelta{
			static_cast<float>(screenX) - m_gizmo.dragStartScreen.x,
			static_cast<float>(screenY) - m_gizmo.dragStartScreen.y
		};
		const float projectedPixels = glm::dot(mouseDelta, axisNorm);
		float newScale = std::max(0.01f, m_gizmo.dragScaleStart + projectedPixels * 0.01f);

		// Snap scale to fixed step increments when enabled
		if (m_snapEnabled && m_scaleSnapStep > 0.0f)
		{
			newScale = std::max(m_scaleSnapStep, std::round(newScale / m_scaleSnapStep) * m_scaleSnapStep);
		}

		tc->scale[axisIdx] = newScale;
	}

	// Apply primary entity's transform, then compute delta and apply to all other selected entities
	ecs.setComponent<ECS::TransformComponent>(primaryEntity, *tc);

	// For translate: apply position delta to all other selected entities
	if (m_gizmo.mode == GizmoMode::Translate && m_selectedEntities.size() > 1)
	{
		const auto& oldPrimary = m_gizmo.dragOldTransforms[primaryEntity];
		const float dx = tc->position[0] - oldPrimary.position[0];
		const float dy = tc->position[1] - oldPrimary.position[1];
		const float dz = tc->position[2] - oldPrimary.position[2];
		for (unsigned int eid : m_selectedEntities)
		{
			if (eid == primaryEntity) continue;
			auto itOld = m_gizmo.dragOldTransforms.find(eid);
			if (itOld == m_gizmo.dragOldTransforms.end()) continue;
			auto* otc = ecs.getComponent<ECS::TransformComponent>(eid);
			if (!otc) continue;
			otc->position[0] = itOld->second.position[0] + dx;
			otc->position[1] = itOld->second.position[1] + dy;
			otc->position[2] = itOld->second.position[2] + dz;
			ecs.setComponent<ECS::TransformComponent>(eid, *otc);
		}
	}
	// For rotate: apply rotation delta to all other selected entities
	else if (m_gizmo.mode == GizmoMode::Rotate && m_selectedEntities.size() > 1)
	{
		const auto& oldPrimary = m_gizmo.dragOldTransforms[primaryEntity];
		const float drx = tc->rotation[0] - oldPrimary.rotation[0];
		const float dry = tc->rotation[1] - oldPrimary.rotation[1];
		const float drz = tc->rotation[2] - oldPrimary.rotation[2];
		for (unsigned int eid : m_selectedEntities)
		{
			if (eid == primaryEntity) continue;
			auto itOld = m_gizmo.dragOldTransforms.find(eid);
			if (itOld == m_gizmo.dragOldTransforms.end()) continue;
			auto* otc = ecs.getComponent<ECS::TransformComponent>(eid);
			if (!otc) continue;
			otc->rotation[0] = itOld->second.rotation[0] + drx;
			otc->rotation[1] = itOld->second.rotation[1] + dry;
			otc->rotation[2] = itOld->second.rotation[2] + drz;
			ecs.setComponent<ECS::TransformComponent>(eid, *otc);
		}
	}
	// For scale: apply scale delta to all other selected entities
	else if (m_gizmo.mode == GizmoMode::Scale && m_selectedEntities.size() > 1)
	{
		const auto& oldPrimary = m_gizmo.dragOldTransforms[primaryEntity];
		const float ds = tc->scale[axisIdx] - oldPrimary.scale[axisIdx];
		for (unsigned int eid : m_selectedEntities)
		{
			if (eid == primaryEntity) continue;
			auto itOld = m_gizmo.dragOldTransforms.find(eid);
			if (itOld == m_gizmo.dragOldTransforms.end()) continue;
			auto* otc = ecs.getComponent<ECS::TransformComponent>(eid);
			if (!otc) continue;
			otc->scale[axisIdx] = std::max(0.01f, itOld->second.scale[axisIdx] + ds);
			ecs.setComponent<ECS::TransformComponent>(eid, *otc);
		}
	}
}

void OpenGLRenderer::endGizmoDrag()
{
	if (!m_selectedEntities.empty())
	{
		auto& ecs = ECS::ECSManager::Instance();

		// Capture new transforms for all selected entities
		std::unordered_map<unsigned int, ECS::TransformComponent> newTransforms;
		for (unsigned int eid : m_selectedEntities)
		{
			const auto* tc = ecs.getComponent<ECS::TransformComponent>(eid);
			if (tc)
				newTransforms[eid] = *tc;
		}

		// Capture old transforms from the drag-start snapshot
		auto oldTransforms = m_gizmo.dragOldTransforms;

		if (!newTransforms.empty())
		{
			std::string modeLabel = "Transform";
			switch (m_gizmo.mode)
			{
			case GizmoMode::Translate: modeLabel = "Move"; break;
			case GizmoMode::Rotate:    modeLabel = "Rotate"; break;
			case GizmoMode::Scale:     modeLabel = "Scale"; break;
			default: break;
			}

			UndoRedoManager::Command cmd;
			cmd.description = modeLabel;
			cmd.execute = [newTransforms]() {
				auto& e = ECS::ECSManager::Instance();
				for (const auto& [eid, tc] : newTransforms)
					e.setComponent<ECS::TransformComponent>(eid, tc);
			};
			cmd.undo = [oldTransforms]() {
				auto& e = ECS::ECSManager::Instance();
				for (const auto& [eid, tc] : oldTransforms)
					e.setComponent<ECS::TransformComponent>(eid, tc);
			};
			UndoRedoManager::Instance().pushCommand(std::move(cmd));
		}
	}

	m_gizmo.dragging = false;
	m_gizmo.activeAxis = GizmoAxis::None;
}
#endif // ENGINE_EDITOR — Gizmo + Grid editor-only code
