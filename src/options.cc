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
    j["display_index"] = display_index;
    j["mode"] = mode;
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
        display_index = j.value("display_index", -1);
        mode = j.value("mode", "plain");
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
