#version 330 core

uniform vec4 uColor;
uniform vec4 uHoverColor;
uniform float uIsHovered;

out vec4 FragColor;

void main()
{
    FragColor = mix(uColor, uHoverColor, clamp(uIsHovered, 0.0, 1.0));
}
