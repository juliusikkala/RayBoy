#include "options.hh"

json options::serialize() const
{
    json j;
    j["window_width"] = window_size.x;
    j["window_height"] = window_size.y;
    j["resolution_scaling"] = resolution_scaling;
    j["recent_roms"] = recent_roms;
    j["msaa_samples"] = msaa_samples;
    j["fullscreen"] = fullscreen;
    j["vsync"] = vsync;
    j["colormapping"] = colormapping;
    j["render_subpixels"] = render_subpixels;
    j["pixel_transitions"] = pixel_transitions;
    j["ray_tracing"] = ray_tracing;
    j["shadow_rays"] = shadow_rays;
    j["reflection_rays"] = reflection_rays;
    j["display_index"] = display_index;
    j["mode"] = mode;
    j["gb_color"] = gb_color;
    j["scene"] = scene;
    return j;
}

bool options::deserialize(const json& j)
{
    *this = options();

    try
    {
        window_size.x = j.value("window_width", 1280);
        window_size.y = j.value("window_height", 720);
        resolution_scaling = j.value("resolution_scaling", 1.0f);

        for(size_t i = 0; i < j.at("recent_roms").size(); ++i)
        {
            std::string path = j.at("recent_roms")[i].get<std::string>();
            if(fs::exists(path))
            {
                recent_roms.push_back(path);
            }
        }

        msaa_samples = j.value("msaa_samples", 1);
        fullscreen = j.value("fullscreen", false);
        vsync = j.value("vsync", true);
        colormapping = j.value("colormapping", true);
        render_subpixels = j.value("render_subpixels", false);
        pixel_transitions = j.value("pixel_transitions", true);
        ray_tracing = j.value("ray_tracing", true);
        shadow_rays = j.value("shadow_rays", 1);
        reflection_rays = j.value("reflection_rays", 1);
        display_index = j.value("display_index", -1);
        mode = j.value("mode", "plain");
        gb_color = j.value("gb_color", "atomic-purple");
        scene = j.value("scene", "white_room");
    }
    catch(...)
    {
        return false;
    }

    return true;
}

void options::push_recent_rom(const std::string& path)
{
    recent_roms.insert(recent_roms.begin(), path);
    for(size_t i = 1; i < recent_roms.size(); ++i)
    {
        if(recent_roms[i] == path)
        {
            recent_roms.erase(recent_roms.begin()+i);
            break;
        }
    }
    if(recent_roms.size() > 10)
        recent_roms.pop_back();
}
