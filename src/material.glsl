#ifndef MATERIAL_GLSL
#define MATERIAL_GLSL

struct material
{
    vec4 color;
    float metallic;
    float roughness;
    vec3 normal;
    float ior_before;
    float ior_after;
    float f0;
    vec3 emission;
    vec3 transmittance;
};

#endif
