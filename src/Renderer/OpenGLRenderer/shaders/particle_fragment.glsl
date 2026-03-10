#version 460 core

in vec4 vColor;
layout(location = 0) out vec4 FragColor;

void main()
{
    // Soft circle from gl_PointCoord (0,0 top-left to 1,1 bottom-right)
    vec2 coord = gl_PointCoord - vec2(0.5);
    float dist = dot(coord, coord);

    // Discard fragments outside the circle
    if (dist > 0.25)
        discard;

    // Smooth edge falloff
    float alpha = 1.0 - smoothstep(0.15, 0.25, dist);

    FragColor = vec4(vColor.rgb, vColor.a * alpha);
}
