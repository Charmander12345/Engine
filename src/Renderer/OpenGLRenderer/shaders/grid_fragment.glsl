#version 460 core
in vec2 vTexCoord;
in vec3 vWorldPos;
in vec3 vNormal;
out vec4 FragColor;

struct Material {
    sampler2D diffuse;
    sampler2D specular;
    float shininess;
};

#define MAX_LIGHTS 8
#define MAX_SHADOW_LIGHTS 4
#define MAX_POINT_SHADOW_LIGHTS 4
#define LIGHT_POINT       0
#define LIGHT_DIRECTIONAL 1
#define LIGHT_SPOT        2

struct Light {
    vec3  position;
    vec3  direction;
    vec3  color;
    float intensity;
    float range;
    float spotCutoff;
    float spotOuterCutoff;
    int   type;
};

uniform Material material;
uniform vec3 uViewPos;
uniform int uLightCount;
uniform Light uLights[MAX_LIGHTS];

// Legacy single-light uniforms (fallback when uLightCount == 0)
uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform float uLightIntensity;

// Multi-light shadow mapping (directional / spot)
uniform sampler2DArray uShadowMaps;
uniform int uShadowCount;
uniform mat4 uLightSpaceMatrices[MAX_SHADOW_LIGHTS];
uniform int uShadowLightIndices[MAX_SHADOW_LIGHTS];

// Point light shadow mapping (cube maps)
uniform samplerCubeArray uPointShadowMaps;
uniform int uPointShadowCount;
uniform vec3 uPointShadowPositions[MAX_POINT_SHADOW_LIGHTS];
uniform float uPointShadowFarPlanes[MAX_POINT_SHADOW_LIGHTS];
uniform int uPointShadowLightIndices[MAX_POINT_SHADOW_LIGHTS];

float calcShadow(int shadowIdx, vec3 worldPos, vec3 normal, vec3 lightDir)
{
    vec4 lightSpacePos = uLightSpaceMatrices[shadowIdx] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0)
        return 0.0;

    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(uShadowMaps, 0).xy;

    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            float pcfDepth = texture(uShadowMaps, vec3(projCoords.xy + vec2(x, y) * texelSize, float(shadowIdx))).r;
            shadow += (projCoords.z - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    shadow /= 25.0;
    return shadow;
}

float calcPointShadow(int shadowIdx, vec3 worldPos, vec3 lightPos, float farPlane)
{
    vec3 fragToLight = worldPos - lightPos;
    float currentDepth = length(fragToLight);

    float bias = 0.05;
    float closestDepth = texture(uPointShadowMaps, vec4(fragToLight, float(shadowIdx))).r;
    closestDepth *= farPlane;
    return (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
}

vec3 calcLight(Light light, vec3 normal, vec3 viewDir, vec3 diffColor, int lightIndex)
{
    vec3 lightDir;
    float attenuation = 1.0;

    if (light.type == LIGHT_DIRECTIONAL)
    {
        lightDir = normalize(-light.direction);
    }
    else
    {
        lightDir = normalize(light.position - vWorldPos);
        float dist = length(light.position - vWorldPos);
        attenuation = 1.0 / (1.0 + dist * dist);

        if (light.range > 0.0)
        {
            float rangeFactor = clamp(1.0 - (dist / light.range), 0.0, 1.0);
            attenuation *= rangeFactor * rangeFactor;
        }
    }

    if (light.type == LIGHT_SPOT)
    {
        float theta = dot(lightDir, normalize(-light.direction));
        float epsilon = light.spotCutoff - light.spotOuterCutoff;
        float spotFade = clamp((theta - light.spotOuterCutoff) / max(epsilon, 0.001), 0.0, 1.0);
        attenuation *= spotFade;
    }

    float shadow = 0.0;
    for (int s = 0; s < uShadowCount && s < MAX_SHADOW_LIGHTS; ++s)
    {
        if (uShadowLightIndices[s] == lightIndex)
        {
            shadow = calcShadow(s, vWorldPos, normal, lightDir);
            break;
        }
    }

    if (light.type == LIGHT_POINT)
    {
        for (int ps = 0; ps < uPointShadowCount && ps < MAX_POINT_SHADOW_LIGHTS; ++ps)
        {
            if (uPointShadowLightIndices[ps] == lightIndex)
            {
                shadow = calcPointShadow(ps, vWorldPos, uPointShadowPositions[ps], uPointShadowFarPlanes[ps]);
                break;
            }
        }
    }

    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diffColor * light.color * light.intensity * attenuation * diff;

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), 16.0);
    vec3 specular = vec3(0.3) * light.color * light.intensity * attenuation * spec;

    return (1.0 - shadow) * (diffuse + specular);
}

// World-space grid pattern material.
vec3 worldGrid(vec3 worldPos)
{
    vec2 coord = worldPos.xz;

    vec2 gridMinor = abs(fract(coord / 0.25) - 0.5);
    vec2 gridMajor = abs(fract(coord) - 0.5);

    vec2 dMinor = fwidth(coord / 0.25);
    vec2 dMajor = fwidth(coord);

    float lineMinor = max(
        smoothstep(0.5 - dMinor.x * 1.5, 0.5, gridMinor.x),
        smoothstep(0.5 - dMinor.y * 1.5, 0.5, gridMinor.y));
    float lineMajor = max(
        smoothstep(0.5 - dMajor.x * 1.5, 0.5, gridMajor.x),
        smoothstep(0.5 - dMajor.y * 1.5, 0.5, gridMajor.y));

    vec3 baseColor = vec3(0.62, 0.62, 0.65);
    vec3 minorColor = vec3(0.42, 0.42, 0.46);
    vec3 majorColor = vec3(0.25, 0.25, 0.30);

    vec3 color = baseColor;
    color = mix(color, minorColor, lineMinor);
    color = mix(color, majorColor, lineMajor);
    return color;
}

void main()
{
    vec3 gridColor = worldGrid(vWorldPos);
    vec3 normal = normalize(vNormal);
    vec3 viewDir = normalize(uViewPos - vWorldPos);

    vec3 ambient = gridColor * 0.1;
    vec3 result = ambient;

    if (uLightCount > 0)
    {
        for (int i = 0; i < uLightCount && i < MAX_LIGHTS; ++i)
        {
            result += calcLight(uLights[i], normal, viewDir, gridColor, i);
        }
    }
    else
    {
        // Legacy single-light fallback
        vec3 lightDir = normalize(uLightPos - vWorldPos);
        float distance = length(uLightPos - vWorldPos);
        float attenuation = 1.0 / (1.0 + distance * distance);

        float diff = max(dot(normal, lightDir), 0.0);
        vec3 diffuse = gridColor * uLightColor * uLightIntensity * attenuation * diff;

        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), 16.0);
        vec3 specular = vec3(0.3) * uLightColor * uLightIntensity * attenuation * spec;

        result += diffuse + specular;
    }

    FragColor = vec4(result, 1.0);
}
