#include "io.hh"
#include "options.hh"
#include <SDL.h>
#include <cstdio>
#include <algorithm>
#include <set>

namespace
{

std::string read_text_file(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");

    if(!f) throw std::runtime_error("Unable to open " + path);

    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* data = new char[sz];
    if(fread(data, 1, sz, f) != sz)
    {
        fclose(f);
        delete [] data;
        throw std::runtime_error("Unable to read " + path);
    }
    fclose(f);
    std::string ret(data, sz);

    delete [] data;
    return ret;
}

void write_binary_file(
    const std::string& path,
    const uint8_t* data,
    size_t size
){
    FILE* f = fopen(path.c_str(), "wb");

    if(!f) throw std::runtime_error("Unable to open " + path);

    if(fwrite(data, 1, size, f) != size)
    {
        fclose(f);
        throw std::runtime_error("Unable to write " + path);
    }
    fclose(f);
}

void write_text_file(const std::string& path, const std::string& content)
{
    write_binary_file(path, (uint8_t*)content.c_str(), content.size());
}

}

fs::path get_writable_path()
{
    static bool has_path = false;
    static fs::path path;
    if(!has_path)
    {
        char* path_str = SDL_GetPrefPath("jji.fi", "RayBoy");
        path = path_str;
        SDL_free(path_str);
        path = path.make_preferred();
        has_path = true;
    }

    return path;
}

std::vector<fs::path> get_readonly_paths()
{
    static bool has_path = false;
    static fs::path path;
    if(!has_path)
    {
        char* path_str = SDL_GetBasePath();
        path = path_str;
        SDL_free(path_str);
        path = path.make_preferred();
        has_path = true;
    }

    std::vector<fs::path> paths;
    paths.push_back(path);

    paths.push_back(".");
#ifdef DATA_DIRECTORY
    paths.push_back(fs::path{DATA_DIRECTORY});
#endif

    return paths;
}

std::string get_readonly_path(const std::string& file)
{
    for(const fs::path& path: get_readonly_paths())
    {
        if(fs::exists(path/file))
            return (path/file).string();
    }
    return file;
}

void write_json_file(const fs::path& path, const json& j)
{
    write_text_file(path.string(), j.dump(2));
}

json read_json_file(const fs::path& path)
{
    return json::parse(read_text_file(path.string()));
}

void write_options(const options& opts)
{
    write_json_file(get_writable_path()/"options.json", opts.serialize());
}

void load_options(options& opts)
{
    try
    {
        opts.deserialize(read_json_file(get_writable_path()/"options.json"));
    }
    // Failure is fine, just reset options.
    catch(...)
    {
        opts = options();
    }
}
