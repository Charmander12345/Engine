#version 460 core

// Tessellation Evaluation Shader – interpolates attributes across the
// tessellated triangle and displaces the vertex along the normal using
// a heightmap texture.

layout(triangles, equal_spacing, ccw) in;

in vec2 tcTexCoord[];
in vec3 tcWorldPos[];
in vec3 tcNormal[];
in mat3 tcTBN[];

out vec2 vTexCoord;
out vec3 vWorldPos;
out vec3 vNormal;
out mat3 vTBN;

uniform mat4 uView;
uniform mat4 uProjection;
uniform sampler2D uDisplacementMap;  // heightmap (R channel, 0–1)
uniform float uDisplacementScale;    // max displacement in world units

void main()
{
    // Barycentric interpolation of per-vertex attributes
    vec3 bary = gl_TessCoord;

    vTexCoord = bary.x * tcTexCoord[0]
              + bary.y * tcTexCoord[1]
              + bary.z * tcTexCoord[2];

    vWorldPos = bary.x * tcWorldPos[0]
              + bary.y * tcWorldPos[1]
              + bary.z * tcWorldPos[2];

    vNormal = normalize(bary.x * tcNormal[0]
                      + bary.y * tcNormal[1]
                      + bary.z * tcNormal[2]);

    // Interpolate TBN columns independently
    mat3 tbn0 = tcTBN[0];
    mat3 tbn1 = tcTBN[1];
    mat3 tbn2 = tcTBN[2];
    vec3 T = normalize(bary.x * tbn0[0] + bary.y * tbn1[0] + bary.z * tbn2[0]);
    vec3 B = normalize(bary.x * tbn0[1] + bary.y * tbn1[1] + bary.z * tbn2[1]);
    vTBN = mat3(T, B, vNormal);

    // Sample displacement heightmap and offset along normal
    float height = texture(uDisplacementMap, vTexCoord).r;
    vWorldPos += vNormal * height * uDisplacementScale;

    gl_Position = uProjection * uView * vec4(vWorldPos, 1.0);
}
