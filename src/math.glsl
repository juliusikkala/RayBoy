#ifndef MATH_GLSL
#define MATH_GLSL
#define M_PI  3.14159238265
#define SQRT2 1.41421356237
#define SQRT3 1.73205080756

vec2 project_lambert_azimuthal_equal_area(vec3 normal)
{
    vec3 n = normalize(normal);
    return inversesqrt(2.0f+2.0f*n.z)*n.xy;
}

vec3 unproject_lambert_azimuthal_equal_area(vec2 n2)
{
    n2 *= SQRT2;
    float d = dot(n2, n2);
    float f = sqrt(2.0f - d);
    return vec3(f*n2, 1.0f - d);
}

float linearize_depth(float depth, vec3 clip_info)
{
    return -2.0f * clip_info.x / (depth * clip_info.y + clip_info.z);
}

vec3 unproject_position(float linear_depth, vec2 uv, vec2 projection_info)
{
    return vec3((0.5f-uv) * projection_info * linear_depth, linear_depth);
}

mat3 create_tangent_space(vec3 normal)
{
    vec3 major;
    if(abs(normal.x) < 0.57735026918962576451) major = vec3(1,0,0);
    else if(abs(normal.y) < 0.57735026918962576451) major = vec3(0,1,0);
    else major = vec3(0,0,1);

    vec3 tangent = normalize(cross(normal, major));
    vec3 bitangent = cross(normal, tangent);

    return mat3(tangent, bitangent, normal);
}

//https://pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations
vec2 concentric_mapping(vec2 u)
{
    vec2 uOffset = 2.0f * u - 1.0f;
    vec2 abs_uOffset = abs(uOffset);

    if(all(lessThan(abs_uOffset, vec2(1e-4))))
        return vec2(0);

    vec2 r_theta = abs_uOffset.x > abs_uOffset.y ?
        vec2(uOffset.x, M_PI * 0.25 * uOffset.y / uOffset.x) :
        vec2(uOffset.y, M_PI * 0.5 - M_PI * 0.25 * uOffset.x / uOffset.y);
    return r_theta.x * vec2(cos(r_theta.y), sin(r_theta.y));
}

#endif
