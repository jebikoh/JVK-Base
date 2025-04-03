#define NR_POINT_LIGHTS 2

#extension GL_GOOGLE_include_directive : require
#include "lights.glsl"

layout(set = 0, binding = 0) uniform SceneData {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    vec4 cameraPos;
    DirectionalLight dirLight;
    PointLight pointLights[NR_POINT_LIGHTS];
    SpotLight spotLight;
    uint enableSpotlight;
} sceneData;

layout(set = 1, binding = 0) uniform GLTFMaterialData{
    vec4 colorFactors;
    vec4 metallicRoughnessFactors;
    vec4 ambient;
    vec4 diffuse;
    vec3 specular;
    float shininess;
} materialData;

layout(set = 1, binding = 1) uniform sampler2D colorTex;
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTex;
layout(set = 1, binding = 3) uniform sampler2D ambientTex;
layout(set = 1, binding = 4) uniform sampler2D diffuseTex;
layout(set = 1, binding = 5) uniform sampler2D specularTex;
