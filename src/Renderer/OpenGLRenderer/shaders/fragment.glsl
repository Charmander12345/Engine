#version 460 core
in vec2 vTexCoord;
in vec3 vWorldPos;
in vec3 vNormal;
out vec4 FragColor;

uniform sampler2D texture0;
uniform vec3 uLightPos;
uniform vec3 uLightColor;
uniform float uLightIntensity;

void main()
{
    vec4 tex = texture(texture0, vTexCoord);
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vWorldPos);
    float diff = max(dot(normal, lightDir), 0.0);
    float distance = length(uLightPos - vWorldPos);
    float attenuation = 1.0 / (1.0 + distance * distance);
    vec3 ambient = tex.rgb * 0.05;
    vec3 diffuse = tex.rgb * (uLightColor * (uLightIntensity * attenuation * diff));
    vec3 lit = ambient + diffuse;
    FragColor = vec4(lit, tex.a);
}
