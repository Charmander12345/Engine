#version 460 core

// Bloom bright-pass extraction + progressive downsample.
// First pass (uPass == 0): extract pixels above uThreshold.
// Subsequent passes: simple bilinear downsample of previous mip.

uniform sampler2D uSourceTexture;
uniform float uThreshold;   // luminance threshold for bright extraction
uniform int   uPass;        // 0 = bright pass, 1+ = downsample

in vec2 vTexCoord;
out vec4 FragColor;

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main()
{
    vec3 color = texture(uSourceTexture, vTexCoord).rgb;

    if (uPass == 0)
    {
        // Soft knee threshold (avoids harsh cutoff)
        float brightness = luminance(color);
        float contribution = max(brightness - uThreshold, 0.0);
        contribution /= max(brightness, 0.001);
        color *= contribution;
    }

    FragColor = vec4(color, 1.0);
}
