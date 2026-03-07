#version 460 core

// Depth-aware bilateral blur for SSAO smoothing.
// Centered 5x5 kernel with depth rejection to prevent
// AO bleeding across geometry boundaries (bright-edge halos).

in vec2 vTexCoord;
out float FragColor;

uniform sampler2D uSsaoInput;
uniform sampler2D uDepthTexture;

void main()
{
    vec2 texelSize = 1.0 / vec2(textureSize(uSsaoInput, 0));
    float centerAO    = texture(uSsaoInput, vTexCoord).r;
    float centerDepth = texture(uDepthTexture, vTexCoord).r;

    // Sky fragments: no occlusion, skip blur
    if (centerDepth >= 1.0)
    {
        FragColor = 1.0;
        return;
    }

    float result      = 0.0;
    float totalWeight = 0.0;

    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            vec2 sampleUV    = vTexCoord + vec2(float(x), float(y)) * texelSize;
            float sampleAO   = texture(uSsaoInput, sampleUV).r;
            float sampleDepth = texture(uDepthTexture, sampleUV).r;

            // Bilateral weight: reject samples across depth discontinuities
            float depthDiff = abs(centerDepth - sampleDepth);
            float w = 1.0 - smoothstep(0.0, 0.002, depthDiff);

            result      += sampleAO * w;
            totalWeight += w;
        }
    }

    FragColor = (totalWeight > 0.0) ? result / totalWeight : centerAO;
}
