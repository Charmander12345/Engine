#pragma once

/// @file RendererEnums.h
/// @brief Shared renderer enums and lightweight types used by both
///        the runtime Renderer base class and the IEditorRenderer interface.
///
/// Extracted from Renderer.h (Phase 10 of the editor-separation plan)
/// so that IEditorRenderer.h and Renderer.h can both include them
/// without circular dependencies.

#include "../Core/MathTypes.h"

enum class GizmoMode { None, Translate, Rotate, Scale };
enum class GizmoAxis { None, X, Y, Z };
enum class AntiAliasingMode { None = 0, FXAA = 1, MSAA_2x = 2, MSAA_4x = 3 };
enum class DebugRenderMode {
    Lit = 0,          // Normal PBR/Blinn-Phong shading (default)
    Unlit,            // Diffuse texture only, no lighting
    Wireframe,        // Wireframe overlay
    ShadowMap,        // Visualise shadow map depth
    ShadowCascades,   // Colour-code CSM cascade splits
    InstanceGroups,   // Colour-code instanced batches
    Normals,          // World-space normals as colour
    Depth,            // Linearised depth buffer visualisation
    Overdraw          // Additive overdraw heat-map
};

enum class ViewportLayout { Single, TwoHorizontal, TwoVertical, Quad };
enum class SubViewportPreset { Perspective, Top, Front, Right };

struct SubViewportCamera
{
    Vec3  position{ 0.0f, 5.0f, 10.0f };
    float yawDeg{ -90.0f };
    float pitchDeg{ -15.0f };
    SubViewportPreset preset{ SubViewportPreset::Perspective };
};
