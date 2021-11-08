// As described in 
// https://en.wikipedia.org/wiki/SRGB#Specification_of_the_transformation,
// but branchless and SIMD-optimized.
vec3 inverse_srgb_correction(vec3 col)
{
    vec3 low = col * 0.07739938f;
    vec3 high = pow(fma(col, vec3(0.94786729f), vec3(0.05213270f)), vec3(2.4f));
    return mix(low, high, lessThan(vec3(0.04045f), col));
}

vec3 srgb_correction(vec3 col)
{
    vec3 low = col * 12.92;
    vec3 high = fma(pow(col, vec3(1.0f/2.4f)), vec3(1.055f), vec3(-0.055f));
    return mix(low, high, lessThan(vec3(0.0031308), col));
}
