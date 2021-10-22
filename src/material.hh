#ifndef RAYBOY_MATERIAL_HH
#define RAYBOY_MATERIAL_HH
#include "math.hh"

class texture;
class sampler;

struct material
{
    bool potentially_transparent() const;

    using sampler_tex = std::pair<const sampler*, const texture*>;

    vec4 color_factor = vec4(1);
    sampler_tex color_texture = {nullptr, nullptr};

    vec2 metallic_roughness_factor = vec2(0.0f, 1.0f);
    sampler_tex metallic_roughness_texture = {nullptr, nullptr};

    float normal_factor = 1.0f;
    sampler_tex normal_texture = {nullptr, nullptr};

    float ior = 1.43f;
    vec3 emission_factor = vec3(0);
    sampler_tex emission_texture = {nullptr, nullptr};

    // Implements multiplicative transparency.
    float transmittance = 0.0f;
};

namespace std
{
    template<> struct hash<material::sampler_tex>
    {
        size_t operator()(const material::sampler_tex& v) const;
    };
}

#endif
