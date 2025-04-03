struct DirectionalLight {
    vec4 position;
    vec4 direction;
    vec4 ambient;
    vec4 diffuse;
    vec4 specular;
};

struct PointLight {
    vec4 position;
    vec3 ambient;
    float constant;
    vec3 diffuse;
    float linear;
    vec3 specular;
    float quadratic;
};

struct SpotLight {
    vec3 position;
    float cutOff;
    vec3 ambient;
    float constant;
    vec3 diffuse;
    float linear;
    vec3 specular;
    float quadratic;
    vec3 direction;
    float outerCutOff;
};