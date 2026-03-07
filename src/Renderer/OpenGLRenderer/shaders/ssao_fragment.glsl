#version 460 core

// Screen-Space Ambient Occlusion (SSAO)
// Depth-only approach: reconstructs view-space positions from depth buffer,
// derives normals from depth derivatives, and samples a hemisphere kernel
// with a random rotation per fragment (4x4 noise texture).

in vec2 vTexCoord;
out float FragColor;

uniform sampler2D uDepthTexture;
uniform sampler2D uNoiseTexture;

uniform mat4 uProjection;
uniform mat4 uInvProjection;
uniform vec2 uNoiseScale;       // viewport / noiseTexSize
uniform float uRadius;
uniform float uBias;
uniform float uPower;

#define KERNEL_SIZE 32
uniform vec3 uSamples[KERNEL_SIZE];

// Reconstruct view-space position from depth buffer
vec3 viewPosFromDepth(vec2 uv)
{
    float depth = texture(uDepthTexture, uv).r;
    // NDC: xy in [-1,1], z in [-1,1]
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 viewPos = uInvProjection * ndc;
    return viewPos.xyz / viewPos.w;
}

void main()
{
    vec3 fragPos = viewPosFromDepth(vTexCoord);

    // Skip fragments at far plane (no geometry)
    float rawDepth = texture(uDepthTexture, vTexCoord).r;
    if (rawDepth >= 1.0)
    {
        FragColor = 1.0;
        return;
    }

    // Reconstruct normal from depth derivatives (cross product of view-space partial derivatives)
    vec3 dPdx = dFdx(fragPos);
    vec3 dPdy = dFdy(fragPos);
    vec3 normal = normalize(cross(dPdy, dPdx));

    // Random rotation vector from noise texture (tiled across screen)
    vec3 randomVec = texture(uNoiseTexture, vTexCoord * uNoiseScale).rgb;

    // Build TBN from normal + random rotation (Gram-Schmidt)
    vec3 tangent   = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normal);

    // Hemisphere kernel sampling
    float occlusion = 0.0;
    for (int i = 0; i < KERNEL_SIZE; ++i)
    {
        // Transform sample from tangent space to view space
        vec3 samplePos = fragPos + TBN * uSamples[i] * uRadius;

        // Project sample to screen space
        vec4 offset = uProjection * vec4(samplePos, 1.0);
        offset.xy /= offset.w;
        offset.xy = offset.xy * 0.5 + 0.5;

        // Sample depth at projected position
        float sampleDepth = viewPosFromDepth(offset.xy).z;

        // Range check: only occlude if sample is within radius
        float rangeCheck = smoothstep(0.0, 1.0, uRadius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(KERNEL_SIZE));
    FragColor = pow(occlusion, uPower);
}
