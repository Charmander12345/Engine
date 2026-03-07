#version 460 core

// Two-pass separable Gaussian blur for bloom.
// uHorizontal == 1: blur horizontally; 0: blur vertically.

uniform sampler2D uSourceTexture;
uniform int uHorizontal;

in vec2 vTexCoord;
out vec4 FragColor;

void main()
{
    vec2 texelSize = 1.0 / textureSize(uSourceTexture, 0);

    // 9-tap Gaussian weights (sigma ~= 2.5)
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

    vec3 result = texture(uSourceTexture, vTexCoord).rgb * weights[0];

    vec2 direction = (uHorizontal != 0) ? vec2(texelSize.x, 0.0) : vec2(0.0, texelSize.y);

    for (int i = 1; i < 5; ++i)
    {
        result += texture(uSourceTexture, vTexCoord + direction * float(i)).rgb * weights[i];
        result += texture(uSourceTexture, vTexCoord - direction * float(i)).rgb * weights[i];
    }

    FragColor = vec4(result, 1.0);
}
