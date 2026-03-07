#version 460 core

// Fullscreen triangle – positions generated from gl_VertexID.
// Vertex 0: (-1, -1)   Vertex 1: (3, -1)   Vertex 2: (-1, 3)
// This covers the entire [-1,1] clip-space without needing a VBO.

out vec2 vTexCoord;

void main()
{
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
