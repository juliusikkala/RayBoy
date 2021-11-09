#ifndef RAYBOY_IO_HH
#define RAYBOY_IO_HH
#include "json.hpp"
#include <filesystem>
#include <set>
using json = nlohmann::json;

namespace fs = std::filesystem;

fs::path get_writable_path();
std::vector<fs::path> get_readonly_paths();
std::string get_readonly_path(const std::string& file);

void write_json_file(const fs::path& path, const json& j);
json read_json_file(const fs::path& path);

struct options;

void write_options(const options& opts);
void load_options(options& opts);

#endif
