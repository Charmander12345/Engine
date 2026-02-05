#version 330 core

layout(location = 0) in vec2 aPos;

uniform mat4 uProjection;

void main()
{
    gl_Position = uProjection * vec4(aPos.xy, 0.0, 1.0);
}
