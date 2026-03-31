// OpenGLRendererDebug.cpp – Debug-rendering methods split from OpenGLRenderer.cpp
// Contains: drawSelectionOutline, renderColliderDebug, renderStreamingVolumeDebug,
//           renderBoneDebug, drawRubberBand

#include "OpenGLRenderer.h"
#include <cmath>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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

// ─── Selection outline ──────────────────────────────────────────────────────

void OpenGLRenderer::drawSelectionOutline()
{
	if (!ensureOutlineResources() || m_pick.colorTex == 0 || m_selectedEntities.empty())
		return;

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(m_outline.program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_pick.colorTex);
	if (m_outline.locPickTex >= 0)
		glUniform1i(m_outline.locPickTex, 0);
	if (m_outline.locOutlineColor >= 0)
		glUniform4f(m_outline.locOutlineColor, 1.0f, 0.6f, 0.0f, 1.0f);
	if (m_outline.locThickness >= 0)
		glUniform1f(m_outline.locThickness, 2.0f);

	glBindVertexArray(m_outline.vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);

	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
	glEnable(GL_DEPTH_TEST);
}

// ─── Collider debug wireframes ──────────────────────────────────────────────

void OpenGLRenderer::renderColliderDebug(const glm::mat4& view, const glm::mat4& projection)
{
	if (!m_collidersVisible)
		return;

	if (!ensureGizmoResources())
		return;

	auto& ecs = ECS::ECSManager::Instance();

	ECS::Schema colliderSchema;
	colliderSchema.require<ECS::CollisionComponent>().require<ECS::TransformComponent>();
	const auto entities = ecs.getEntitiesMatchingSchema(colliderSchema);
	if (entities.empty())
		return;

	glUseProgram(m_gizmo.program);
	glBindVertexArray(m_gizmo.vao);
	glDisable(GL_DEPTH_TEST);
	glLineWidth(2.0f);

	std::vector<float> verts;

	for (const auto entity : entities)
	{
		const auto* col = ecs.getComponent<ECS::CollisionComponent>(entity);
		const auto* tc  = ecs.getComponent<ECS::TransformComponent>(entity);
		if (!col || !tc)
			continue;

		// Determine wireframe color based on physics motion type and sensor flag
		glm::vec3 color(0.0f, 0.8f, 0.0f); // default green (static)
		if (col->isSensor)
		{
			color = glm::vec3(1.0f, 0.3f, 0.3f); // red for triggers
		}
		else if (ecs.hasComponent<ECS::PhysicsComponent>(entity))
		{
			const auto* phys = ecs.getComponent<ECS::PhysicsComponent>(entity);
			if (phys)
			{
				switch (phys->motionType)
				{
				case ECS::PhysicsComponent::MotionType::Static:    color = glm::vec3(0.0f, 0.8f, 0.0f); break;
				case ECS::PhysicsComponent::MotionType::Kinematic: color = glm::vec3(1.0f, 0.7f, 0.0f); break;
				case ECS::PhysicsComponent::MotionType::Dynamic:   color = glm::vec3(0.0f, 0.7f, 1.0f); break;
				}
			}
		}
		glUniform3fv(m_gizmo.locColor, 1, &color[0]);

		// Build model matrix: entity position + rotation + collider offset
		const glm::vec3 entityPos(tc->position[0], tc->position[1], tc->position[2]);
		const glm::vec3 offset(col->colliderOffset[0], col->colliderOffset[1], col->colliderOffset[2]);
		const glm::mat3 rot = getEntityRotationMatrix(*tc);

		glm::mat4 model = glm::translate(glm::mat4(1.0f), entityPos);
		model *= glm::mat4(rot);
		model = glm::translate(model, offset);

		const glm::mat4 mvp = projection * view * model;
		glUniformMatrix4fv(m_gizmo.locMVP, 1, GL_FALSE, &mvp[0][0]);

		constexpr float PI = 3.14159265f;
		constexpr int segments = 32;

		switch (col->colliderType)
		{
		case ECS::CollisionComponent::ColliderType::Box:
		{
			const float hx = col->colliderSize[0];
			const float hy = col->colliderSize[1];
			const float hz = col->colliderSize[2];
			// 12 edges of a wireframe box = 24 vertices
			float boxVerts[] = {
				// Bottom face
				-hx,-hy,-hz,  hx,-hy,-hz,
				 hx,-hy,-hz,  hx,-hy, hz,
				 hx,-hy, hz, -hx,-hy, hz,
				-hx,-hy, hz, -hx,-hy,-hz,
				// Top face
				-hx, hy,-hz,  hx, hy,-hz,
				 hx, hy,-hz,  hx, hy, hz,
				 hx, hy, hz, -hx, hy, hz,
				-hx, hy, hz, -hx, hy,-hz,
				// Vertical edges
				-hx,-hy,-hz, -hx, hy,-hz,
				 hx,-hy,-hz,  hx, hy,-hz,
				 hx,-hy, hz,  hx, hy, hz,
				-hx,-hy, hz, -hx, hy, hz,
			};
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(boxVerts), boxVerts);
			glDrawArrays(GL_LINES, 0, 24);
			break;
		}
		case ECS::CollisionComponent::ColliderType::Sphere:
		{
			const float r = col->colliderSize[0];
			for (int axis = 0; axis < 3; ++axis)
			{
				verts.clear();
				buildCircleVerts(verts, segments, r, axis);
				glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
				glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
				glDrawArrays(GL_LINE_STRIP, 0, segments + 1);
			}
			break;
		}
		case ECS::CollisionComponent::ColliderType::Capsule:
		{
			const float r  = col->colliderSize[0];
			const float hh = col->colliderSize[1]; // half-height of cylinder part
			constexpr int halfSeg = segments / 2;

			// Top circle (Y = +hh)
			verts.clear();
			for (int i = 0; i <= segments; ++i)
			{
				const float angle = 2.0f * PI * float(i) / float(segments);
				verts.push_back(std::cos(angle) * r);
				verts.push_back(hh);
				verts.push_back(std::sin(angle) * r);
			}
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
			glDrawArrays(GL_LINE_STRIP, 0, segments + 1);

			// Bottom circle (Y = -hh)
			verts.clear();
			for (int i = 0; i <= segments; ++i)
			{
				const float angle = 2.0f * PI * float(i) / float(segments);
				verts.push_back(std::cos(angle) * r);
				verts.push_back(-hh);
				verts.push_back(std::sin(angle) * r);
			}
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
			glDrawArrays(GL_LINE_STRIP, 0, segments + 1);

			// Top hemisphere arc (XY plane)
			verts.clear();
			for (int i = 0; i <= halfSeg; ++i)
			{
				const float angle = PI * float(i) / float(halfSeg);
				verts.push_back(std::cos(angle) * r);
				verts.push_back(hh + std::sin(angle) * r);
				verts.push_back(0.0f);
			}
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
			glDrawArrays(GL_LINE_STRIP, 0, halfSeg + 1);

			// Bottom hemisphere arc (XY plane)
			verts.clear();
			for (int i = 0; i <= halfSeg; ++i)
			{
				const float angle = PI + PI * float(i) / float(halfSeg);
				verts.push_back(std::cos(angle) * r);
				verts.push_back(-hh + std::sin(angle) * r);
				verts.push_back(0.0f);
			}
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
			glDrawArrays(GL_LINE_STRIP, 0, halfSeg + 1);

			// Top hemisphere arc (ZY plane)
			verts.clear();
			for (int i = 0; i <= halfSeg; ++i)
			{
				const float angle = PI * float(i) / float(halfSeg);
				verts.push_back(0.0f);
				verts.push_back(hh + std::sin(angle) * r);
				verts.push_back(std::cos(angle) * r);
			}
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
			glDrawArrays(GL_LINE_STRIP, 0, halfSeg + 1);

			// Bottom hemisphere arc (ZY plane)
			verts.clear();
			for (int i = 0; i <= halfSeg; ++i)
			{
				const float angle = PI + PI * float(i) / float(halfSeg);
				verts.push_back(0.0f);
				verts.push_back(-hh + std::sin(angle) * r);
				verts.push_back(std::cos(angle) * r);
			}
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
			glDrawArrays(GL_LINE_STRIP, 0, halfSeg + 1);

			// 4 vertical connecting lines
			float capsuleLines[] = {
				 r, -hh, 0.0f,   r,  hh, 0.0f,
				-r, -hh, 0.0f,  -r,  hh, 0.0f,
				0.0f, -hh,  r,  0.0f,  hh,  r,
				0.0f, -hh, -r,  0.0f,  hh, -r,
			};
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(capsuleLines), capsuleLines);
			glDrawArrays(GL_LINES, 0, 8);
			break;
		}
		case ECS::CollisionComponent::ColliderType::Cylinder:
		{
			const float r  = col->colliderSize[0];
			const float hh = col->colliderSize[1];

			// Top circle (Y = +hh)
			verts.clear();
			for (int i = 0; i <= segments; ++i)
			{
				const float angle = 2.0f * PI * float(i) / float(segments);
				verts.push_back(std::cos(angle) * r);
				verts.push_back(hh);
				verts.push_back(std::sin(angle) * r);
			}
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
			glDrawArrays(GL_LINE_STRIP, 0, segments + 1);

			// Bottom circle (Y = -hh)
			verts.clear();
			for (int i = 0; i <= segments; ++i)
			{
				const float angle = 2.0f * PI * float(i) / float(segments);
				verts.push_back(std::cos(angle) * r);
				verts.push_back(-hh);
				verts.push_back(std::sin(angle) * r);
			}
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, verts.size() * sizeof(float), verts.data());
			glDrawArrays(GL_LINE_STRIP, 0, segments + 1);

			// 4 vertical lines
			float cylLines[] = {
				 r, -hh, 0.0f,   r,  hh, 0.0f,
				-r, -hh, 0.0f,  -r,  hh, 0.0f,
				0.0f, -hh,  r,  0.0f,  hh,  r,
				0.0f, -hh, -r,  0.0f,  hh, -r,
			};
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(cylLines), cylLines);
			glDrawArrays(GL_LINES, 0, 8);
			break;
		}
		default:
			break; // Mesh and HeightField colliders not visualized as wireframes
		}
	}

	glEnable(GL_DEPTH_TEST);
	glLineWidth(1.0f);
	glBindVertexArray(0);
}

// ─── Streaming volume debug wireframes ──────────────────────────────────────

void OpenGLRenderer::renderStreamingVolumeDebug(const glm::mat4& view, const glm::mat4& projection)
{
	const auto& volumes = getStreamingVolumes();
	const auto& subLevels = getSubLevels();
	if (volumes.empty())
		return;

	if (!ensureGizmoResources())
		return;

	glUseProgram(m_gizmo.program);
	glBindVertexArray(m_gizmo.vao);
	glDisable(GL_DEPTH_TEST);
	glLineWidth(2.0f);

	for (const auto& vol : volumes)
	{
		// Determine color from linked sub-level
		glm::vec3 color(0.5f, 0.5f, 0.5f);
		if (vol.subLevelIndex >= 0 && vol.subLevelIndex < static_cast<int>(subLevels.size()))
		{
			const auto& c = subLevels[vol.subLevelIndex].color;
			color = glm::vec3(c.x, c.y, c.z);
		}
		glUniform3fv(m_gizmo.locColor, 1, &color[0]);

		const glm::vec3 center(vol.center.x, vol.center.y, vol.center.z);
		const glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
		const glm::mat4 mvp = projection * view * model;
		glUniformMatrix4fv(m_gizmo.locMVP, 1, GL_FALSE, &mvp[0][0]);

		const float hx = vol.halfExtents.x;
		const float hy = vol.halfExtents.y;
		const float hz = vol.halfExtents.z;

		float boxVerts[] = {
			// Bottom face
			-hx,-hy,-hz,  hx,-hy,-hz,
			 hx,-hy,-hz,  hx,-hy, hz,
			 hx,-hy, hz, -hx,-hy, hz,
			-hx,-hy, hz, -hx,-hy,-hz,
			// Top face
			-hx, hy,-hz,  hx, hy,-hz,
			 hx, hy,-hz,  hx, hy, hz,
			 hx, hy, hz, -hx, hy, hz,
			-hx, hy, hz, -hx, hy,-hz,
			// Vertical edges
			-hx,-hy,-hz, -hx, hy,-hz,
			 hx,-hy,-hz,  hx, hy,-hz,
			 hx,-hy, hz,  hx, hy, hz,
			-hx,-hy, hz, -hx, hy, hz,
		};
		glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(boxVerts), boxVerts);
		glDrawArrays(GL_LINES, 0, 24);
	}

	glEnable(GL_DEPTH_TEST);
	glLineWidth(1.0f);
	glBindVertexArray(0);
}

// ─── Bone debug visualization ───────────────────────────────────────────────

void OpenGLRenderer::renderBoneDebug(const glm::mat4& view, const glm::mat4& projection)
{
	if (!m_bonesVisible)
		return;

	if (m_selectedEntities.empty())
		return;

	if (!ensureGizmoResources())
		return;

	auto& ecs = ECS::ECSManager::Instance();

	glUseProgram(m_gizmo.program);
	glBindVertexArray(m_gizmo.vao);
	glDisable(GL_DEPTH_TEST);

	for (const unsigned int entity : m_selectedEntities)
	{
		// Find the animator for this entity
		auto ait = m_entityAnimators.find(entity);
		if (ait == m_entityAnimators.end())
			continue;

		const auto& animator = ait->second;
		const Skeleton* skeleton = animator->getSkeleton();
		if (!skeleton || skeleton->bones.empty())
			continue;

		const auto& boneMatrices = animator->getBoneMatrices();
		if (boneMatrices.empty())
			continue;

		// Build entity model matrix (position + rotation + scale)
		const auto* tc = ecs.getComponent<ECS::TransformComponent>(entity);
		if (!tc)
			continue;

		const glm::vec3 entityPos(tc->position[0], tc->position[1], tc->position[2]);
		const glm::mat3 rot = getEntityRotationMatrix(*tc);
		glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), entityPos);
		modelMatrix *= glm::mat4(rot);
		modelMatrix = glm::scale(modelMatrix, glm::vec3(tc->scale[0], tc->scale[1], tc->scale[2]));

		const glm::mat4 vp = projection * view;

		// Compute bone world positions from finalBoneMatrices:
		// globalTransform = finalBoneMatrix * inverse(offsetMatrix)
		// bonePos_meshSpace = globalTransform * (0,0,0,1) = translation column
		// bonePos_world = entityModelMatrix * bonePos_meshSpace
		const size_t boneCount = skeleton->bones.size();
		std::vector<glm::vec3> boneWorldPositions(boneCount);

		for (size_t b = 0; b < boneCount; ++b)
		{
			if (b >= boneMatrices.size()) break;

			// globalTransform = finalBoneMatrix * inverse(offsetMatrix)
			const Mat4x4 invOffset = skeleton->bones[b].offsetMatrix.inverse();
			const Mat4x4 globalTf = boneMatrices[b] * invOffset;

			// Row-major: translation is at [12], [13], [14]
			const glm::vec4 meshPos(globalTf.m[12], globalTf.m[13], globalTf.m[14], 1.0f);
			const glm::vec4 worldPos = modelMatrix * meshPos;
			boneWorldPositions[b] = glm::vec3(worldPos);
		}

		// Draw bone lines (from each bone to its parent)
		// Use identity MVP since positions are already in world space — use VP only
		const glm::mat4 identity(1.0f);
		glUniformMatrix4fv(m_gizmo.locMVP, 1, GL_FALSE, &vp[0][0]);

		glLineWidth(2.0f);

		for (size_t b = 0; b < boneCount; ++b)
		{
			const int parentIdx = skeleton->bones[b].parentIndex;
			if (parentIdx < 0 || parentIdx >= static_cast<int>(boneCount))
				continue;

			// Bone color: cyan for normal bones
			const glm::vec3 boneColor(0.0f, 0.9f, 0.9f);
			glUniform3fv(m_gizmo.locColor, 1, &boneColor[0]);

			const glm::vec3& from = boneWorldPositions[parentIdx];
			const glm::vec3& to = boneWorldPositions[b];

			float lineVerts[] = {
				from.x, from.y, from.z,
				to.x, to.y, to.z
			};

			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(lineVerts), lineVerts);
			glDrawArrays(GL_LINES, 0, 2);
		}

		// Draw bone joint points as small cross markers
		for (size_t b = 0; b < boneCount; ++b)
		{
			// Root bone: yellow, larger; others: cyan, smaller
			const bool isRoot = (skeleton->bones[b].parentIndex < 0);
			const float crossSize = isRoot ? 0.06f : 0.03f;
			const glm::vec3 pointColor = isRoot
				? glm::vec3(1.0f, 1.0f, 0.0f)   // yellow for root
				: glm::vec3(0.0f, 0.9f, 0.9f);   // cyan for others
			glUniform3fv(m_gizmo.locColor, 1, &pointColor[0]);

			const glm::vec3& p = boneWorldPositions[b];

			// Draw a small 3D cross (3 lines) at the bone position
			float crossVerts[] = {
				p.x - crossSize, p.y, p.z, p.x + crossSize, p.y, p.z,
				p.x, p.y - crossSize, p.z, p.x, p.y + crossSize, p.z,
				p.x, p.y, p.z - crossSize, p.x, p.y, p.z + crossSize,
			};

			glLineWidth(isRoot ? 3.0f : 2.0f);
			glBindBuffer(GL_ARRAY_BUFFER, m_gizmo.vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(crossVerts), crossVerts);
			glDrawArrays(GL_LINES, 0, 6);
		}
	}

	glEnable(GL_DEPTH_TEST);
	glLineWidth(1.0f);
	glBindVertexArray(0);
}

// ─── Rubber-band selection overlay ──────────────────────────────────────────

void OpenGLRenderer::drawRubberBand(const glm::mat4& ortho)
{
	if (!m_rubberBandActive)
		return;

	const float x0 = std::min(m_rubberBandStart.x, m_rubberBandEnd.x);
	const float y0 = std::min(m_rubberBandStart.y, m_rubberBandEnd.y);
	const float x1 = std::max(m_rubberBandStart.x, m_rubberBandEnd.x);
	const float y1 = std::max(m_rubberBandStart.y, m_rubberBandEnd.y);

	// Build a simple quad (two triangles) for the filled rectangle
	const float verts[] = {
		x0, y0,  x1, y0,  x1, y1,
		x0, y0,  x1, y1,  x0, y1
	};

	// Use a tiny immediate-mode VAO/VBO for the overlay
	GLuint vao = 0, vbo = 0;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

	// Use the gizmo shader (simple MVP + uniform colour) if available
	if (m_gizmo.program != 0)
	{
		glUseProgram(m_gizmo.program);
		if (m_gizmo.locMVP >= 0)
			glUniformMatrix4fv(m_gizmo.locMVP, 1, GL_FALSE, glm::value_ptr(ortho));

		glDisable(GL_DEPTH_TEST);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Filled rectangle (semi-transparent blue)
		if (m_gizmo.locColor >= 0)
			glUniform4f(m_gizmo.locColor, 0.25f, 0.56f, 1.0f, 0.18f);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		// Border (opaque blue, 1.5 px)
		if (m_gizmo.locColor >= 0)
			glUniform4f(m_gizmo.locColor, 0.25f, 0.56f, 1.0f, 0.85f);
		glLineWidth(1.5f);

		// Build a line-loop for the border
		const float borderVerts[] = {
			x0, y0,  x1, y0,
			x1, y0,  x1, y1,
			x1, y1,  x0, y1,
			x0, y1,  x0, y0
		};
		glBufferData(GL_ARRAY_BUFFER, sizeof(borderVerts), borderVerts, GL_STREAM_DRAW);
		glDrawArrays(GL_LINES, 0, 8);

		glDisable(GL_BLEND);
		glEnable(GL_DEPTH_TEST);
		glUseProgram(0);
	}

	glBindVertexArray(0);
	glDeleteBuffers(1, &vbo);
	glDeleteVertexArrays(1, &vao);
}
#endif // ENGINE_EDITOR — Debug-rendering editor-only code
