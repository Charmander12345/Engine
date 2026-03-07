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

// FXAA 3.11 Quality – 9-sample neighborhood, edge-orientation detection,
// edge endpoint walking (12 steps with variable stride), and subpixel
// aliasing correction.  Returns a single offset-sampled colour per pixel.
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 fxaa(sampler2D tex, vec2 uv, vec2 texel)
{
    // 1. Sample 3x3 neighbourhood
    vec3 rgbM  = texture(tex, uv).rgb;
    vec3 rgbN  = texture(tex, uv + vec2( 0.0, -1.0) * texel).rgb;
    vec3 rgbS  = texture(tex, uv + vec2( 0.0,  1.0) * texel).rgb;
    vec3 rgbW  = texture(tex, uv + vec2(-1.0,  0.0) * texel).rgb;
    vec3 rgbE  = texture(tex, uv + vec2( 1.0,  0.0) * texel).rgb;
    vec3 rgbNW = texture(tex, uv + vec2(-1.0, -1.0) * texel).rgb;
    vec3 rgbNE = texture(tex, uv + vec2( 1.0, -1.0) * texel).rgb;
    vec3 rgbSW = texture(tex, uv + vec2(-1.0,  1.0) * texel).rgb;
    vec3 rgbSE = texture(tex, uv + vec2( 1.0,  1.0) * texel).rgb;

    float lumM  = luma(rgbM);
    float lumN  = luma(rgbN);
    float lumS  = luma(rgbS);
    float lumW  = luma(rgbW);
    float lumE  = luma(rgbE);
    float lumNW = luma(rgbNW);
    float lumNE = luma(rgbNE);
    float lumSW = luma(rgbSW);
    float lumSE = luma(rgbSE);

    // 2. Local contrast – early-out on low-contrast areas
    float lumMin = min(lumM, min(min(lumN, lumS), min(lumW, lumE)));
    float lumMax = max(lumM, max(max(lumN, lumS), max(lumW, lumE)));
    float lumRange = lumMax - lumMin;
    if (lumRange < max(0.0312, lumMax * 0.125))
        return rgbM;

    // 3. Edge orientation (horizontal vs vertical second-derivative comparison)
    float edgeH = abs(-2.0 * lumN + lumNW + lumNE)
                + abs(-2.0 * lumM + lumW  + lumE ) * 2.0
                + abs(-2.0 * lumS + lumSW + lumSE);
    float edgeV = abs(-2.0 * lumW + lumNW + lumSW)
                + abs(-2.0 * lumM + lumN  + lumS ) * 2.0
                + abs(-2.0 * lumE + lumNE + lumSE);
    bool isHorizontal = (edgeH >= edgeV);

    // 4. Select edge-normal direction (perpendicular to the edge)
    float stepLen = isHorizontal ? texel.y : texel.x;
    float lumPos  = isHorizontal ? lumS : lumE;
    float lumNeg  = isHorizontal ? lumN : lumW;
    float gradPos = abs(lumPos - lumM);
    float gradNeg = abs(lumNeg - lumM);

    float lumEdge;
    if (gradPos >= gradNeg)
        lumEdge = lumPos;
    else
    {
        stepLen = -stepLen;
        lumEdge = lumNeg;
    }

    // UV at half-texel offset perpendicular to the edge
    vec2 edgeUV = uv;
    if (isHorizontal) edgeUV.y += stepLen * 0.5;
    else              edgeUV.x += stepLen * 0.5;

    float lumAvgEdge    = 0.5 * (lumM + lumEdge);
    float gradThreshold = lumRange * 0.25;

    // 5. Edge endpoint walking – variable stride for quality/performance balance
    vec2 edgeStep = isHorizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    const float steps[12] = float[12](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

    vec2  uvP = edgeUV + edgeStep;
    vec2  uvN = edgeUV - edgeStep;
    float lumEndP = luma(texture(tex, uvP).rgb) - lumAvgEdge;
    float lumEndN = luma(texture(tex, uvN).rgb) - lumAvgEdge;
    bool  doneP = abs(lumEndP) >= gradThreshold;
    bool  doneN = abs(lumEndN) >= gradThreshold;

    for (int i = 1; i < 12 && !(doneP && doneN); ++i)
    {
        if (!doneP)
        {
            uvP     += edgeStep * steps[i];
            lumEndP  = luma(texture(tex, uvP).rgb) - lumAvgEdge;
            doneP    = abs(lumEndP) >= gradThreshold;
        }
        if (!doneN)
        {
            uvN     -= edgeStep * steps[i];
            lumEndN  = luma(texture(tex, uvN).rgb) - lumAvgEdge;
            doneN    = abs(lumEndN) >= gradThreshold;
        }
    }

    // 6. Compute edge pixel offset
    float distP = isHorizontal ? (uvP.x - uv.x) : (uvP.y - uv.y);
    float distN = isHorizontal ? (uv.x - uvN.x) : (uv.y - uvN.y);
    float distNearest = min(distP, distN);
    float edgeLength  = distP + distN;

    bool lumMIsNeg   = (lumM - lumAvgEdge) < 0.0;
    bool correctSide = ((distP <= distN) ? (lumEndP < 0.0) : (lumEndN < 0.0)) != lumMIsNeg;
    float pixelOff   = correctSide ? (0.5 - distNearest / edgeLength) : 0.0;

    // 7. Subpixel aliasing correction
    float lumAvg = (1.0 / 12.0) * (2.0 * (lumN + lumS + lumW + lumE)
                 + (lumNW + lumNE + lumSW + lumSE));
    float subOff = clamp(abs(lumAvg - lumM) / lumRange, 0.0, 1.0);
    subOff = smoothstep(0.0, 1.0, subOff);
    subOff = subOff * subOff * 0.75;

    pixelOff = max(pixelOff, subOff);

    // 8. Final single-offset sample
    vec2 finalUV = uv;
    if (isHorizontal) finalUV.y += pixelOff * stepLen;
    else              finalUV.x += pixelOff * stepLen;

    return texture(tex, finalUV).rgb;
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
