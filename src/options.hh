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
    int display_index = -1;

    json serialize() const;
    bool deserialize(const json& j);

    void push_recent_rom(const std::string& path);
};

#endif

