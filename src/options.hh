#ifndef RAYBOY_OPTIONS_HH
#define RAYBOY_OPTIONS_HH
#include "io.hh"
#include "math.hh"

struct options
{
    ivec2 window_size = ivec2(1280, 720);
    float resolution_scaling = 1.0f;
    std::vector<std::string> recent_roms = {};
    unsigned msaa_samples = 1;
    bool fullscreen = false;
    bool vsync = true;
    bool colormapping = true;
    bool render_subpixels = false;
    bool pixel_transitions = true;
    bool ray_tracing = true;
    unsigned shadow_rays = 1;
    unsigned reflection_rays = 1;
    unsigned refraction_rays = 1;
    int display_index = -1;
    std::string mode = "fancy";
    std::string gb_color = "atomic-purple";
    std::string scene = "white_room";
    int accumulation = -1;
    bool secondary_shadows = false;
    bool hdr = false;

    json serialize() const;
    bool deserialize(const json& j);

    void push_recent_rom(const std::string& path);
};

#endif

