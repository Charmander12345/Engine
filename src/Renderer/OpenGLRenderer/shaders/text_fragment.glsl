#version 330 core

in vec2 vUV;

uniform sampler2D uTextAtlas;
uniform vec4 uTextColor;

out vec4 FragColor;

void main()
{
    float alpha = texture(uTextAtlas, vUV).r;
    FragColor = vec4(uTextColor.rgb, uTextColor.a * alpha);
}
