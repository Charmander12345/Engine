#version 460 core
in vec3 vColor;
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D texture0;

void main()
{
    vec4 tex = texture(texture0, vTexCoord);
    FragColor = tex;
}
