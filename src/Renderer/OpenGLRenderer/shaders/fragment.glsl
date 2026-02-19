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
uniform int uLightCount;
uniform Light uLights[MAX_LIGHTS];

// Legacy single-light uniforms (fallback when uLightCount == 0)
uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform float uLightIntensity;

vec3 calcLight(Light light, vec3 normal, vec3 viewDir, vec3 diffColor, vec3 specColor)
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

    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diffColor * light.color * light.intensity * attenuation * diff;

    // Specular (Blinn-Phong)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    vec3 specular = specColor * light.color * light.intensity * attenuation * spec;

    return diffuse + specular;
}

void main()
{
    vec4 diffTex = texture(material.diffuse, vTexCoord);
    vec3 normal = normalize(vNormal);
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
            result += calcLight(uLights[i], normal, viewDir, diffTex.rgb, specColor);
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

    FragColor = vec4(result, diffTex.a > 0.0 ? diffTex.a : 1.0);
}
