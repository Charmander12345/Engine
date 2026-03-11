#version 330 core

uniform vec4 uColor;
uniform vec4 uHoverColor;
uniform float uIsHovered;
uniform vec4 uBorderColor;
uniform float uBorderSize;
uniform vec4 uRect;
uniform vec2 uViewportSize;
uniform float uBorderRadius;

out vec4 FragColor;

// SDF for a rounded rectangle centered at the origin with half-size b and corner radius r
float roundedRectSDF(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main()
{
    vec4 baseColor = mix(uColor, uHoverColor, clamp(uIsHovered, 0.0, 1.0));
    vec2 fragPos = vec2(gl_FragCoord.x, uViewportSize.y - gl_FragCoord.y);

    // Rectangle center and half-size
    vec2 center = (uRect.xy + uRect.zw) * 0.5;
    vec2 halfSize = (uRect.zw - uRect.xy) * 0.5;

    float radius = min(uBorderRadius, min(halfSize.x, halfSize.y));
    float dist = roundedRectSDF(fragPos - center, halfSize, radius);

    // Anti-aliased edge (1px smoothstep)
    float alpha = 1.0 - smoothstep(-0.5, 0.5, dist);

    if (uBorderSize > 0.0)
    {
        float borderDist = dist + uBorderSize;
        float borderMask = 1.0 - smoothstep(-0.5, 0.5, borderDist);
        float inBorder = alpha - borderMask;
        baseColor = mix(baseColor, uBorderColor, clamp(inBorder / max(alpha, 0.001), 0.0, 1.0));
    }

    baseColor.a *= alpha;
    if (baseColor.a < 0.001) discard;
    FragColor = baseColor;
}
