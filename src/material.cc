#include "material.hh"
#include "texture.hh"

bool material::potentially_transparent() const
{
    return color_factor.a < 1.0f || transmittance > 0.0f ||
        (color_texture.second && color_texture.second->potentially_transparent());
}

size_t std::hash<material::sampler_tex>::operator()(const material::sampler_tex& v) const
{
    size_t a = std::hash<const sampler*>()(v.first);
    size_t b = std::hash<const texture*>()(v.second);
    return a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2));
}
