#version 450

#extension GL_GOOGLE_include_directive : require
#include "billboard_input_structures.glsl"

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outFragColor;
layout(location = 2) out uint outTextureIndex;

layout(push_constant) uniform constants {
    vec4 particleCenter;
    vec4 color;
    vec4 scale;
    uint textureIndex;
} pushConstants;

void main() {
    const vec3 positions[4] = vec3[4](
        vec3(-1.0, -1.0, 0.0),
        vec3(1.0, -1.0, 0.0),
        vec3(-1.0, 1.0, 0.0),
        vec3(1.0, 1.0, 0.0)
    );

    const vec2 uvs[4] = vec2[4](
        vec2(0.0, -0.0),
        vec2(1.0, -0.0),
        vec2(0.0, -1.0),
        vec2(1.0, -1.0)
    );

    vec3 cameraRight = vec3(sceneData.view[0][0], sceneData.view[1][0], sceneData.view[2][0]);
    vec3 cameraUp = vec3(sceneData.view[0][1], sceneData.view[1][1], sceneData.view[2][1]);

    vec3 pos = positions[gl_VertexIndex].xyz;

    vec3 vertexPos = pushConstants.particleCenter.xyz + cameraRight * pos.x * pushConstants.scale.x + cameraUp * pos.y * pushConstants.scale.x;
    gl_Position = sceneData.viewProj * vec4(vertexPos, 1.0);

    outUV = uvs[gl_VertexIndex];
    outFragColor = pushConstants.color.rgb;
    outTextureIndex = pushConstants.textureIndex;
}