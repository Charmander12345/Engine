#version 460 core

// Resolve / post-processing pass.
// Reads from the HDR scene texture and applies:
//   1. Anti-aliasing (FXAA or MSAA – selected via uAAMode)
//   2. Bloom (additive blend)
//   3. Tone mapping (ACES filmic)
//   4. Gamma correction (linear → sRGB)

uniform sampler2D uSceneTexture;
uniform int   uGammaEnabled;
uniform int   uToneMappingEnabled;
uniform float uExposure;
uniform int   uAAMode;          // 0=None, 1=FXAA, 2+=MSAA (resolved, no shader AA)
uniform vec2  uTexelSize;

uniform int       uBloomEnabled;
uniform sampler2D uBloomTexture;
uniform float     uBloomIntensity;

uniform int       uSsaoEnabled;
uniform sampler2D uSsaoTexture;

in vec2 vTexCoord;

out vec4 FragColor;

// ACES filmic tone mapping (simple fit by Krzysztof Narkowicz)
vec3 acesToneMap(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Simple FXAA (quality-tuned luminance-based edge AA)
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 fxaa(sampler2D tex, vec2 uv, vec2 texel)
{
    vec3 rgbM  = texture(tex, uv).rgb;
    vec3 rgbNW = texture(tex, uv + vec2(-1.0, -1.0) * texel).rgb;
    vec3 rgbNE = texture(tex, uv + vec2( 1.0, -1.0) * texel).rgb;
    vec3 rgbSW = texture(tex, uv + vec2(-1.0,  1.0) * texel).rgb;
    vec3 rgbSE = texture(tex, uv + vec2( 1.0,  1.0) * texel).rgb;

    float lumM  = luma(rgbM);
    float lumNW = luma(rgbNW);
    float lumNE = luma(rgbNE);
    float lumSW = luma(rgbSW);
    float lumSE = luma(rgbSE);

    float lumMin = min(lumM, min(min(lumNW, lumNE), min(lumSW, lumSE)));
    float lumMax = max(lumM, max(max(lumNW, lumNE), max(lumSW, lumSE)));
    float lumRange = lumMax - lumMin;

    // Skip edge detection on low-contrast areas
    if (lumRange < max(0.0312, lumMax * 0.125))
        return rgbM;

    vec2 dir;
    dir.x = -((lumNW + lumNE) - (lumSW + lumSE));
    dir.y =  ((lumNW + lumSW) - (lumNE + lumSE));

    float dirReduce = max((lumNW + lumNE + lumSW + lumSE) * 0.03125, 0.0078125);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * texel;

    vec3 rgbA = 0.5 * (
        texture(tex, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(tex, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(tex, uv + dir * -0.5).rgb +
        texture(tex, uv + dir *  0.5).rgb);
    float lumB = luma(rgbB);

    return (lumB < lumMin || lumB > lumMax) ? rgbA : rgbB;
}

void main()
{
    vec3 color;

    // Anti-aliasing (FXAA applied in HDR / linear space before tone mapping)
    // MSAA modes are resolved on the CPU side; shader only applies FXAA when mode == 1
    if (uAAMode == 1)
    {
        color = fxaa(uSceneTexture, vTexCoord, uTexelSize);
    }
    else
    {
        color = texture(uSceneTexture, vTexCoord).rgb;
    }

    // Bloom (additive blend in HDR / linear space, before tone mapping)
    if (uBloomEnabled != 0)
    {
        color += texture(uBloomTexture, vTexCoord).rgb * uBloomIntensity;
    }

    // SSAO (multiply ambient occlusion in HDR / linear space, before tone mapping)
    if (uSsaoEnabled != 0)
    {
        float ao = texture(uSsaoTexture, vTexCoord).r;
        color *= ao;
    }

    // Tone mapping (exposure + ACES)
    if (uToneMappingEnabled != 0)
    {
        color = color * uExposure;
        color = acesToneMap(color);
    }

    // Gamma correction (linear → sRGB)
    if (uGammaEnabled != 0)
    {
        color = pow(color, vec3(1.0 / 2.2));
    }

    FragColor = vec4(color, 1.0);
}
