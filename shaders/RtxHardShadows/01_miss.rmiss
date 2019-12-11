#version 460
#extension GL_NV_ray_tracing : require

layout(location = 0) rayPayloadInNV Payload {
    vec4 diffuseColor;
    vec4 specularColor;
    vec4 normal; // normla + specular alpha
    vec4 other; // depth, int ior, ext ior, material type
} payload;

void main()
{
    payload.diffuseColor = vec4(0.0, 0.0, 0.0, 1.0);
    payload.specularColor = vec4(0.0, 0.0, 0.0, 1.0);
    payload.normal = vec4(0.0, 0.0, 0.0, 0.0);
    payload.other = vec4(-1.0, 0.0, 0.0, -1.0);
}