#extension GL_GOOGLE_include_directive : require
#include "lights.glsl"

#define NR_POINT_LIGHTS 2

layout(set = 0, binding = 0) uniform SceneData {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPos;
    DirectionalLight dirLight;
    PointLight pointLights[NR_POINT_LIGHTS];
    SpotLight spotLight;
} sceneData;

layout(set = 1, binding = 0) uniform sampler2D textures[2];