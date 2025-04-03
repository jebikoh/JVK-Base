#version 450

#extension GL_GOOGLE_include_directive : require
#include "mesh_input_structures.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inFragPos;

layout(location = 0) out vec4 outFragColor;

vec3 calcDirectionalLight(DirectionalLight light, vec3 normal, vec3 viewDir, vec4 diffuseVal, vec4 specularVal) {
    vec3 lightDir = normalize(-light.direction.xyz);

    // Ambient
    vec3 ambient = light.ambient.rgb * diffuseVal.rgb;

    // Diffuse
    float diffuseStrength = max(dot(normal, lightDir), 0.0f);
    vec3 diffuse = light.diffuse.rgb * diffuseStrength * diffuseVal.rgb;

    // Specular
    vec3 reflectDir = reflect(-lightDir, normal);
    float specularStrength = pow(max(dot(viewDir, reflectDir), 0.0f), materialData.shininess);
    vec3 specular = light.specular.rgb * specularStrength * specularVal.rgb;

    return ambient + diffuse + specular;
}

vec3 calcPointLight(PointLight light, vec3 normal, vec3 viewDir, vec4 diffuseVal, vec4 specularVal) {
    vec3 lightDir = normalize(light.position.xyz - inFragPos);

    // Ambient
    vec3 ambient = light.ambient.rgb * diffuseVal.rgb;

    // Diffuse
    float diffuseStrength = max(dot(normal, lightDir), 0.0f);
    vec3 diffuse = light.diffuse.rgb * diffuseStrength * diffuseVal.rgb;

    // Specular
    vec3 reflectDir = reflect(-lightDir, normal);
    float specularStrength = pow(max(dot(viewDir, reflectDir), 0.0f), 32);
    vec3 specular = light.specular.rgb * specularStrength * specularVal.rgb;

    // Attenuation
    float distance = length(light.position.xyz - inFragPos);
    float attenuation = 1.0f / (light.constant + light.linear * distance + light.quadratic * (distance * distance));

    return (ambient + diffuse + specular) * attenuation;
}

vec3 calcSpotLight(SpotLight light, vec3 normal, vec3 viewDir, vec4 diffuseVal, vec4 specularVal) {
    vec3 lightDir = normalize(light.position.xyz - inFragPos);

    // Ambient
    vec3 ambient = light.ambient * diffuseVal.rgb;

    // Diffuse
    float diffuseStrength = max(dot(normal, lightDir), 0.0f);
    vec3 diffuse = light.diffuse * diffuseStrength * diffuseVal.rgb;

    // Specular
    vec3 reflDir = reflect(-lightDir, normal);
    float specularStrength = pow(max(dot(viewDir, reflDir), 0.0f), 32);
    vec3 specular = light.specular * specularStrength * specularVal.rgb;

    // Spotlight
    float theta = dot(lightDir, normalize(-light.direction.xyz));
    float epsilon = light.cutOff - light.outerCutOff;
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0f, 1.0f);
    diffuse *= intensity;
    specular *= intensity;

    // Attenuation
    float distance = length(light.position.xyz - inFragPos);
    float attenuation = 1.0f / (light.constant + light.linear * distance + light.quadratic * (distance * distance));

    diffuse *= attenuation;
    specular *= attenuation;

    return ambient + diffuse + specular;
}

void main() {
    vec4 diffuseVal = texture(diffuseTex, inUV);

//    if (diffuseVal.a < 0.1f) {
//        discard;
//    }

    vec4 specularVal = texture(specularTex, inUV);

    vec3 norm = normalize(inNormal);
    vec3 viewDir = normalize(sceneData.cameraPos.xyz - inFragPos);

    // Directional Light
    vec3 result = vec3(0.0f);
    result += calcDirectionalLight(sceneData.dirLight, norm, viewDir, diffuseVal, specularVal);

    // Point Lights
    for (int i = 0; i < NR_POINT_LIGHTS; i++) {
        result += calcPointLight(sceneData.pointLights[i], norm, viewDir, diffuseVal, specularVal);
    }

    if (sceneData.enableSpotlight == 1) {
        result += calcSpotLight(sceneData.spotLight, norm, viewDir, diffuseVal, specularVal);
    }

    outFragColor = vec4(result, 1.0f);
}