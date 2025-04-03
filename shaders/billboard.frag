#version 450

#extension GL_GOOGLE_include_directive : require
#include "billboard_input_structures.glsl"

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inFragColor;
layout(location = 2) flat in uint inTextureIndex;

layout(location = 0) out vec4 outFragColor;

void main() {
    // Just use texture for alpha testing
    vec4 texColor = texture(textures[inTextureIndex], inUV);
    if (texColor.a < 0.01f) {
        discard;
    }

    outFragColor = vec4(inFragColor, 1.0f);
}