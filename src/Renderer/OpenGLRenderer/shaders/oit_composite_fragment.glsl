#version 460 core

// Weighted Blended OIT composite pass (McGuire & Bavoil 2013).
// Reads the accumulation texture (RGBA16F) and revealage texture (R8)
// produced by the transparent pass and blends the result over the
// opaque scene already present in the destination framebuffer.

uniform sampler2D uAccumTexture;
uniform sampler2D uRevealageTexture;

in vec2 vTexCoord;
out vec4 FragColor;

void main()
{
    vec4 accum = texture(uAccumTexture, vTexCoord);
    float revealage = texture(uRevealageTexture, vTexCoord).r;

    // No transparent geometry at this pixel
    if (accum.a < 1e-4)
        discard;

    // Average colour weighted by coverage
    vec3 averageColor = accum.rgb / max(accum.a, 1e-5);

    // Final composited colour: transparent layer over opaque
    FragColor = vec4(averageColor, 1.0 - revealage);
}
