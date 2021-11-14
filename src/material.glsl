#ifndef MATERIAL_GLSL
#define MATERIAL_GLSL

struct material
{
    vec4 color;
    float metallic;
    float roughness;
    float roughness2;
    vec3 normal;
    float ior_before;
    float ior_after;
    float f0;
    vec3 emission;
    vec3 transmittance;
};

float fresnel_schlick(float cos_d, float f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - cos_d, 5.0f);
}

// This is pre-divided by 4.0f*cos_l*cos_v (normally, that would be in the
// nominator, but it would be divided out later anyway.)
// Uses Schlick-GGX approximation for the geometry shadowing equation.
float geometry_smith(float cos_l, float cos_v, float k)
{
    float mk = 1.0f - k;
    return 0.25f / ((cos_l * mk + k) * (cos_v * mk + k));
}

// Multiplied by pi so that it can be avoided later.
float distribution_ggx(float cos_h, float a)
{
    float a2 = a * a;
    float denom = cos_h * cos_h * (a2 - 1.0f) + 1.0f;
    return a2 / (denom * denom);
}

vec3 brdf(
    vec3 diffuse_light_color,
    vec3 specular_light_color,
    vec3 light_dir,
    vec3 view_dir,
    in material mat
){
    vec3 h = normalize(view_dir + light_dir);
    float cos_l = max(dot(mat.normal, light_dir), 0.0f);
    float cos_v = max(dot(mat.normal, view_dir), 0.0f);
    float cos_h = max(dot(mat.normal, h), 0.0f);
    float cos_d = clamp(dot(view_dir, h), 0.0f, 1.0f);

    float k = mat.roughness2 + 1.0f;
    k = k * k * 0.125f;

    vec3 fresnel = mix(vec3(fresnel_schlick(cos_d, mat.f0)), mat.color.rgb, mat.metallic);
    float geometry = geometry_smith(cos_l, cos_v, k);
    float distribution = distribution_ggx(cos_h, mat.roughness2);

    vec3 specular = fresnel * geometry * distribution * specular_light_color;

    vec3 kd = (vec3(1.0f) - fresnel) * (1.0f - mat.metallic);

    vec3 diffuse = (1.0f - mat.transmittance) * kd * mat.color.rgb * diffuse_light_color;

    return (diffuse + specular) * cos_l;
}

layout(binding = 9) uniform sampler2D brdf_integration;

// https://seblagarde.wordpress.com/2011/08/17/hello-world/
vec3 fresnel_schlick_attenuated(float cos_d, vec3 f0, float roughness)
{
    return f0 + (max(vec3(1.0f - roughness), f0) - f0)
        * pow(1.0f - cos_d, 5.0f);
}

vec3 brdf_indirect(
    vec3 incoming_diffuse,
    vec3 incoming_specular,
    vec3 view_dir,
    in material mat
){
    float cos_v = max(dot(mat.normal, view_dir), 0.0f);
    vec3 f0_m = mix(vec3(mat.f0), mat.color.rgb, mat.metallic);

    // The fresnel value must be attenuated, because we are actually integrating
    // over all directions instead of just one specific direction here. This is
    // an approximated function, though.
    vec3 fresnel = clamp(
        fresnel_schlick_attenuated(cos_v, f0_m, mat.roughness2),
        vec3(0), vec3(1)
    );

    vec3 kd = (vec3(1.0f) - fresnel) * (1.0f - mat.metallic);
    vec3 diffuse = kd * mat.color.rgb * incoming_diffuse;

    vec2 bi = texture(brdf_integration, vec2(cos_v, mat.roughness)).xy;
    vec3 specular = incoming_specular * (fresnel * bi.x + bi.y);

    return diffuse + specular;
}

#endif
