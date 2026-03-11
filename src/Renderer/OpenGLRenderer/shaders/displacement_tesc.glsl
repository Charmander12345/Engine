#version 460 core

// Tessellation Control Shader – passes vertices through and sets
// tessellation levels based on a uniform control factor.

layout(vertices = 3) out;

in vec2 vTexCoord[];
in vec3 vWorldPos[];
in vec3 vNormal[];
in mat3 vTBN[];

out vec2 tcTexCoord[];
out vec3 tcWorldPos[];
out vec3 tcNormal[];
out mat3 tcTBN[];

uniform float uTessLevel;       // outer/inner tessellation level (1–64)
uniform vec3  uViewPos;         // camera position for distance-based LOD

void main()
{
    // Pass through per-vertex attributes
    tcTexCoord[gl_InvocationID] = vTexCoord[gl_InvocationID];
    tcWorldPos[gl_InvocationID] = vWorldPos[gl_InvocationID];
    tcNormal[gl_InvocationID]   = vNormal[gl_InvocationID];
    tcTBN[gl_InvocationID]      = vTBN[gl_InvocationID];

    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

    // Only invocation 0 sets tessellation levels
    if (gl_InvocationID == 0)
    {
        // Distance-adaptive tessellation: closer patches get more detail
        vec3 center = (vWorldPos[0] + vWorldPos[1] + vWorldPos[2]) / 3.0;
        float dist = distance(uViewPos, center);
        float level = clamp(uTessLevel / max(dist * 0.1, 1.0), 1.0, uTessLevel);

        gl_TessLevelOuter[0] = level;
        gl_TessLevelOuter[1] = level;
        gl_TessLevelOuter[2] = level;
        gl_TessLevelInner[0] = level;
    }
}
