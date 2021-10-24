#include "model.hh"

model::model() {}

void model::add_vertex_group(const material& mat, const mesh* mesh)
{
    groups.push_back({mat, mesh});
}

void model::clear_vertex_groups()
{
    groups.clear();
}

bool model::potentially_transparent() const
{
    for(const auto& group: groups)
        if(group.mat.potentially_transparent()) return true;
    return false;
}

size_t model::group_count() const
{
    return groups.size();
}

model::vertex_group& model::operator[](size_t i)
{
    return groups[i];
}

const model::vertex_group& model::operator[](size_t i) const
{
    return groups[i];
}

model::iterator model::begin()
{
    return groups.begin();
}

model::const_iterator model::begin() const
{
    return groups.begin();
}

model::const_iterator model::cbegin() const
{
    return groups.cbegin();
}

model::iterator model::end()
{
    return groups.end();
}

model::const_iterator model::end() const
{
    return groups.end();
}

model::const_iterator model::cend() const
{
    return groups.cend();
}
