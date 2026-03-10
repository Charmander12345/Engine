#version 460 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;
layout(location = 5) in vec4  aBoneIdsF;   // bone indices stored as float, cast to int in shader
layout(location = 6) in vec4  aBoneWeights;

layout(std430, binding = 0) buffer InstanceModelMatrices {
    mat4 instanceModels[];
};

out vec2 vTexCoord;
out vec3 vWorldPos;
out vec3 vNormal;
out mat3 vTBN;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform bool uInstanced;

// Skeletal animation bone matrices
#define MAX_BONES 128
uniform bool uSkinned;
uniform mat4 uBoneMatrices[MAX_BONES];

void main()
{
    mat4 model;
    if (uInstanced) {
        model = instanceModels[gl_InstanceID];
    } else {
        model = uModel;
    }

    // Apply skeletal skinning if enabled
    vec4 localPos;
    vec3 localNormal;
    vec3 localTangent;
    vec3 localBitangent;

    if (uSkinned) {
        ivec4 boneIds = ivec4(aBoneIdsF);
        mat4 boneTransform = mat4(0.0);
        for (int i = 0; i < 4; ++i) {
            if (boneIds[i] >= 0 && boneIds[i] < MAX_BONES) {
                boneTransform += uBoneMatrices[boneIds[i]] * aBoneWeights[i];
            }
        }
        localPos       = boneTransform * vec4(aPos, 1.0);
        localNormal    = mat3(boneTransform) * aNormal;
        localTangent   = mat3(boneTransform) * aTangent;
        localBitangent = mat3(boneTransform) * aBitangent;
    } else {
        localPos       = vec4(aPos, 1.0);
        localNormal    = aNormal;
        localTangent   = aTangent;
        localBitangent = aBitangent;
    }

    vec4 worldPos = model * localPos;
    vWorldPos = worldPos.xyz;
    mat3 normalMatrix = mat3(transpose(inverse(model)));
    vNormal = normalize(normalMatrix * localNormal);

    vec3 T = normalize(normalMatrix * localTangent);
    vec3 B = normalize(normalMatrix * localBitangent);
    vec3 N = vNormal;
    // Re-orthogonalize T with respect to N (Gram-Schmidt)
    T = normalize(T - dot(T, N) * N);
    B = cross(N, T);
    vTBN = mat3(T, B, N);

    gl_Position = uProjection * uView * worldPos;
    vTexCoord = aTexCoord;
}
