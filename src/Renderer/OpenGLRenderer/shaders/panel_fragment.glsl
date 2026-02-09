#version 330 core

uniform vec4 uColor;
uniform vec4 uBorderColor;
uniform float uBorderSize;
uniform vec4 uRect;
uniform vec2 uViewportSize;

out vec4 FragColor;

void main()
{
    vec2 fragPos = vec2(gl_FragCoord.x, uViewportSize.y - gl_FragCoord.y);
    float left = fragPos.x - uRect.x;
    float right = uRect.z - fragPos.x;
    float top = fragPos.y - uRect.y;
    float bottom = uRect.w - fragPos.y;
    float edge = min(min(left, right), min(top, bottom));

    vec4 baseColor = uColor;
    if (uBorderSize > 0.0 && edge >= 0.0 && edge < uBorderSize)
    {
        baseColor = uBorderColor;
    }
    FragColor = baseColor;
}
