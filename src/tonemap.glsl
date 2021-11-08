#ifndef TONEMAP_GLSL
#define TONEMAP_GLSL
#include "srgb.glsl"

vec3 aces_tonemap(vec3 col)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((col*(a*col+b))/(col*(c*col+d)+e), vec3(0), vec3(1));
}

vec3 gamma_correction(vec3 col, float gamma)
{
    return pow(col, vec3(gamma));
}

layout(binding = 1, rgba32f) uniform writeonly image2D image_output;
layout(binding = 2) uniform uniform_buffer {
    float exposure;
    float gamma;
} params;

layout(push_constant) uniform push_constants
{
    uint algorithm;
    uint samples;
} pc;

vec3 tonemap_pre_resolve(vec3 col)
{
    // Prevents infinitely bright regions causing NaN
    vec3 c = clamp(col * params.exposure, vec3(0.0f), vec3(1e5));

    if(pc.algorithm == 0)
    {
        return aces_tonemap(c);
    }
    else return c;
}

vec3 tonemap_post_resolve(vec3 col)
{
    if(pc.algorithm == 0)
    {
        return srgb_correction(col);
    }
    else if(pc.algorithm == 1)
    {
        return gamma_correction(col, params.gamma);
    }
    else return col;
}

#endif
