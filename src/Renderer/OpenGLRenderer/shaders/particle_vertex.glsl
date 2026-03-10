#version 460 core

// Per-particle attributes (from VBO)
layout(location = 0) in vec3 aPosition;   // World-space center
layout(location = 1) in vec4 aColor;
layout(location = 2) in float aSize;

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;

out vec4 vColor;
out vec2 vTexCoord;

// Expand each point into a camera-facing billboard quad via gl_VertexID.
// We draw GL_POINTS with a geometry shader to emit 4 vertices (triangle strip).
// However, for simplicity and portability we use point sprites here:
// the fragment shader samples a procedural soft circle.

void main()
{
    vColor = aColor;
    vTexCoord = vec2(0.5); // unused for point sprites, placeholder

    vec4 clipPos = uProjection * uView * vec4(aPosition, 1.0);
    gl_Position = clipPos;

    // Point size in pixels: scale by projection to approximate screen-space size
    // This gives a reasonable approximation for perspective projection.
    float dist = length((uView * vec4(aPosition, 1.0)).xyz);
    float projScale = uProjection[1][1]; // vertical FOV factor
    gl_PointSize = clamp(aSize * projScale * 600.0 / max(dist, 0.1), 1.0, 256.0);
}
