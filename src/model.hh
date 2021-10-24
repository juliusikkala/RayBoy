#ifndef RAYBOY_MODEL_HH
#define RAYBOY_MODEL_HH
#include "mesh.hh"
#include "material.hh"
#include <vector>

class model
{
public:
    model();

    struct vertex_group
    {
        material mat;
        const class mesh* mesh;
    };

    void add_vertex_group(const material& mat, const mesh* mesh);
    void clear_vertex_groups();

    bool potentially_transparent() const;

    size_t group_count() const;
    vertex_group& operator[](size_t i);
    const vertex_group& operator[](size_t i) const;

    using iterator = std::vector<vertex_group>::iterator;
    using const_iterator = std::vector<vertex_group>::const_iterator;

    iterator begin();
    const_iterator begin() const;
    const_iterator cbegin() const;

    iterator end();
    const_iterator end() const;
    const_iterator cend() const;

private:
    std::vector<vertex_group> groups;
}; 

#endif

