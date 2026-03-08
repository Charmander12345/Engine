#version 460 core
in vec2 vTexCoord;
in vec3 vWorldPos;
in vec3 vNormal;
in mat3 vTBN;
out vec4 FragColor;

struct Material {
    sampler2D diffuse;
    sampler2D specular;
    sampler2D normalMap;
    sampler2D emissiveMap;
    sampler2D metallicRoughness;
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
    float spotCutoff;      // cos(inner angle)
    float spotOuterCutoff; // cos(outer angle)
    int   type;            // 0=point, 1=directional, 2=spot
};

uniform Material material;
uniform vec3 uViewPos;
uniform int uHasSpecularMap;
uniform int uHasDiffuseMap;
uniform int uHasNormalMap;
uniform int uHasEmissiveMap;
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

// Fog
uniform int   uFogEnabled;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;

// Cascaded Shadow Maps (directional light)
#define MAX_CSM_CASCADES 4
uniform sampler2DArray uCsmMaps;
uniform int   uCsmEnabled;
uniform int   uCsmLightIndex;
uniform mat4  uCsmMatrices[MAX_CSM_CASCADES];
uniform float uCsmSplits[MAX_CSM_CASCADES];
uniform mat4  uViewMatrix;

// PBR (Metallic/Roughness)
uniform int   uPbrEnabled;
uniform int   uHasMetallicRoughnessMap;
uniform float uMetallic;
uniform float uRoughness;

// Debug render mode (0=Lit, 1=Unlit, 2=Wireframe, 3=ShadowMap, 4=ShadowCascades,
//                     5=InstanceGroups, 6=Normals, 7=Depth, 8=Overdraw)
uniform int   uDebugMode;
uniform vec3  uDebugColor; // per-batch tint for InstanceGroups mode
uniform float uNearPlane;
uniform float uFarPlane;

const float PI = 3.14159265359;

// GGX/Trowbridge-Reitz Normal Distribution Function
float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

// Smith's Schlick-GGX Geometry Function
float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return geometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * geometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

// Fresnel-Schlick Approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float calcShadow(int shadowIdx, vec3 worldPos, vec3 normal, vec3 lightDir)
{
    vec4 lightSpacePos = uLightSpaceMatrices[shadowIdx] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;

    // Fragments outside the shadow map are not in shadow
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0)
        return 0.0;

    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(uShadowMaps, 0).xy;

    // 5x5 PCF
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
    float shadow = 0.0;

    // Sample the cube map array: index = shadowIdx (selects which cube map in the array)
    float closestDepth = texture(uPointShadowMaps, vec4(fragToLight, float(shadowIdx))).r;
    closestDepth *= farPlane;
    shadow = (currentDepth - bias > closestDepth) ? 1.0 : 0.0;

    return shadow;
}

float calcCsmShadow(vec3 worldPos, vec3 normal, vec3 lightDir)
{
    // Compute view-space depth for cascade selection
    float depth = -(uViewMatrix * vec4(worldPos, 1.0)).z;

    // Find the cascade that contains this fragment
    int cascade = MAX_CSM_CASCADES - 1;
    for (int i = 0; i < MAX_CSM_CASCADES; ++i)
    {
        if (depth < uCsmSplits[i])
        {
            cascade = i;
            break;
        }
    }

    vec4 lightSpacePos = uCsmMatrices[cascade] * vec4(worldPos, 1.0);
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z > 1.0)
        return 0.0;

    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.001);
    // Reduce bias for closer cascades
    bias *= 1.0 / (float(cascade + 1) * 0.5 + 0.5);

    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(uCsmMaps, 0).xy;

    // 5x5 PCF
    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            float pcfDepth = texture(uCsmMaps, vec3(projCoords.xy + vec2(x, y) * texelSize, float(cascade))).r;
            shadow += (projCoords.z - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    shadow /= 25.0;
    return shadow;
}

vec3 calcLight(Light light, vec3 normal, vec3 viewDir, vec3 diffColor, vec3 specColor, int lightIndex)
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

        // Range-based falloff
        if (light.range > 0.0)
        {
            float rangeFactor = clamp(1.0 - (dist / light.range), 0.0, 1.0);
            attenuation *= rangeFactor * rangeFactor;
        }
    }

    // Spot cone
    if (light.type == LIGHT_SPOT)
    {
        float theta = dot(lightDir, normalize(-light.direction));
        float epsilon = light.spotCutoff - light.spotOuterCutoff;
        float spotFade = clamp((theta - light.spotOuterCutoff) / max(epsilon, 0.001), 0.0, 1.0);
        attenuation *= spotFade;
    }

    // Shadow: CSM for the primary directional light, regular maps for others
    float shadow = 0.0;
    if (uCsmEnabled != 0 && light.type == LIGHT_DIRECTIONAL && lightIndex == uCsmLightIndex)
    {
        shadow = calcCsmShadow(vWorldPos, normal, lightDir);
    }
    else
    {
        for (int s = 0; s < uShadowCount && s < MAX_SHADOW_LIGHTS; ++s)
        {
            if (uShadowLightIndices[s] == lightIndex)
            {
                shadow = calcShadow(s, vWorldPos, normal, lightDir);
                break;
            }
        }
    }

    // Point light shadow: check cube map shadows
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

    // Diffuse & Specular
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 radiance = light.color * light.intensity * attenuation;

    if (uPbrEnabled != 0)
    {
        // PBR Cook-Torrance
        float metallic  = uMetallic;
        float roughness = uRoughness;
        if (uHasMetallicRoughnessMap != 0)
        {
            vec2 mr = texture(material.metallicRoughness, vTexCoord).gb;
            roughness = mr.x;
            metallic  = mr.y;
        }
        roughness = clamp(roughness, 0.04, 1.0);

        vec3 F0 = mix(vec3(0.04), diffColor, metallic);
        vec3 halfwayDir = normalize(lightDir + viewDir);

        float NDF = distributionGGX(normal, halfwayDir, roughness);
        float G   = geometrySmith(normal, viewDir, lightDir, roughness);
        vec3  F   = fresnelSchlick(max(dot(halfwayDir, viewDir), 0.0), F0);

        vec3 numerator  = NDF * G * F;
        float denominator = 4.0 * max(dot(normal, viewDir), 0.0) * NdotL + 0.0001;
        vec3 specBRDF = numerator / denominator;

        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        return (1.0 - shadow) * (kD * diffColor / PI + specBRDF) * radiance * NdotL;
    }
    else
    {
        // Blinn-Phong
        vec3 diffuse = diffColor * radiance * NdotL;
        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
        vec3 specular = specColor * radiance * spec;
        return (1.0 - shadow) * (diffuse + specular);
    }
}

void main()
{
    // --- Debug: Normals ---
    if (uDebugMode == 6)
    {
        vec3 n = normalize(vNormal);
        if (uHasNormalMap != 0)
        {
            vec3 mapNormal = texture(material.normalMap, vTexCoord).rgb;
            mapNormal = mapNormal * 2.0 - 1.0;
            n = normalize(vTBN * mapNormal);
        }
        FragColor = vec4(n * 0.5 + 0.5, 1.0);
        return;
    }

    // --- Debug: Depth (logarithmic mapping for better visual distribution) ---
    if (uDebugMode == 7)
    {
        float z = gl_FragCoord.z;
        float ndc = z * 2.0 - 1.0;
        float near = uNearPlane;
        float far  = uFarPlane;
        float linearDepth = (2.0 * near * far) / (far + near - ndc * (far - near));
        float d = log2(1.0 + linearDepth) / log2(1.0 + far);
        FragColor = vec4(vec3(1.0 - d), 1.0);
        return;
    }

    // --- Debug: Overdraw ---
    if (uDebugMode == 8)
    {
        FragColor = vec4(0.1, 0.02, 0.0, 1.0);
        return;
    }

    // --- Debug: Instance Groups ---
    if (uDebugMode == 5)
    {
        vec4 diffTex = (uHasDiffuseMap != 0) ? texture(material.diffuse, vTexCoord) : vec4(1.0);
        FragColor = vec4(diffTex.rgb * uDebugColor, diffTex.a > 0.0 ? diffTex.a : 1.0);
        return;
    }

    // --- Debug: Shadow Map visualisation ---
    if (uDebugMode == 3)
    {
        vec3 normal = normalize(vNormal);
        float shadow = 0.0;
        for (int s = 0; s < uShadowCount && s < MAX_SHADOW_LIGHTS; ++s)
        {
            shadow = max(shadow, calcShadow(s, vWorldPos, normal, vec3(0.0, -1.0, 0.0)));
        }
        if (uCsmEnabled != 0)
        {
            shadow = max(shadow, calcCsmShadow(vWorldPos, normal, vec3(0.0, -1.0, 0.0)));
        }
        FragColor = vec4(vec3(1.0 - shadow), 1.0);
        return;
    }

    // --- Debug: Shadow Cascades ---
    if (uDebugMode == 4)
    {
        float depth = -(uViewMatrix * vec4(vWorldPos, 1.0)).z;
        vec3 cascadeColor = vec3(0.5);
        if (depth < uCsmSplits[0])      cascadeColor = vec3(0.2, 1.0, 0.2);
        else if (depth < uCsmSplits[1]) cascadeColor = vec3(0.2, 0.5, 1.0);
        else if (depth < uCsmSplits[2]) cascadeColor = vec3(1.0, 1.0, 0.2);
        else                            cascadeColor = vec3(1.0, 0.2, 0.2);
        vec4 diffTex = (uHasDiffuseMap != 0) ? texture(material.diffuse, vTexCoord) : vec4(1.0);
        FragColor = vec4(diffTex.rgb * 0.3 + cascadeColor * 0.7, 1.0);
        return;
    }

    vec4 diffTex;
    if (uHasDiffuseMap != 0)
    {
        diffTex = texture(material.diffuse, vTexCoord);
    }
    else
    {
        diffTex = vec4(1.0);
    }

    // --- Debug: Unlit / Wireframe ---
    if (uDebugMode == 1 || uDebugMode == 2)
    {
        FragColor = vec4(diffTex.rgb, diffTex.a > 0.0 ? diffTex.a : 1.0);
        return;
    }

    // --- Normal Lit path (uDebugMode == 0) ---
    vec3 normal = normalize(vNormal);
    // Normal mapping: perturb normal using tangent-space map
    if (uHasNormalMap != 0)
    {
        vec3 mapNormal = texture(material.normalMap, vTexCoord).rgb;
        mapNormal = mapNormal * 2.0 - 1.0; // [0,1] -> [-1,1]
        normal = normalize(vTBN * mapNormal);
    }
    vec3 viewDir = normalize(uViewPos - vWorldPos);
    vec3 specColor = (uHasSpecularMap != 0)
        ? texture(material.specular, vTexCoord).rgb
        : vec3(1.0);

    // Ambient
    vec3 ambient = diffTex.rgb * 0.1;

    vec3 result = ambient;

    if (uLightCount > 0)
    {
        for (int i = 0; i < uLightCount && i < MAX_LIGHTS; ++i)
        {
            result += calcLight(uLights[i], normal, viewDir, diffTex.rgb, specColor, i);
        }
    }
    else
    {
        // Legacy single-light fallback
        vec3 lightDir = normalize(uLightPos - vWorldPos);
        float distance = length(uLightPos - vWorldPos);
        float attenuation = 1.0 / (1.0 + distance * distance);

        float diff = max(dot(normal, lightDir), 0.0);
        vec3 diffuse = diffTex.rgb * uLightColor * uLightIntensity * attenuation * diff;

        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
        vec3 specular = specColor * uLightColor * uLightIntensity * attenuation * spec;

        result += diffuse + specular;
    }

    // Emissive contribution (added after lighting, before fog)
    if (uHasEmissiveMap != 0)
    {
        vec3 emissive = texture(material.emissiveMap, vTexCoord).rgb;
        result += emissive;
    }

    // Apply fog (exponential squared, based on view distance)
    if (uFogEnabled != 0)
    {
        float dist = length(uViewPos - vWorldPos);
        float fogFactor = exp(-uFogDensity * uFogDensity * dist * dist);
        fogFactor = clamp(fogFactor, 0.0, 1.0);
        result = mix(uFogColor, result, fogFactor);
    }

    FragColor = vec4(result, diffTex.a > 0.0 ? diffTex.a : 1.0);
}
