#ifndef RAYBOY_MESH_HH
#define RAYBOY_MESH_HH

#include "context.hh"
#include "vkres.hh"
#include "math.hh"
#include <vector>

class mesh
{
public:
    struct vertex
    {
        pvec4 pos;
        pvec4 normal;
        // xy: primary texture coordinates, zw: lightmap texture coordinates
        pvec4 uv;
        pvec4 tangent;
    };

    mesh(
        context& ctx,
        std::vector<vertex>&& vertices,
        std::vector<uint32_t>&& indices,
        bool opaque = false
    );
    mesh(mesh&& other) = default;

    VkBuffer get_vertex_buffer() const;
    VkBuffer get_index_buffer() const;
    VkAccelerationStructureKHR get_blas() const;
    VkDeviceAddress get_blas_address() const;

    void set_opaque(bool opaque);
    bool is_opaque() const;

    void draw(VkCommandBuffer buf) const;

    static constexpr VkVertexInputBindingDescription bindings[] = {
        {0, sizeof(vertex), VK_VERTEX_INPUT_RATE_VERTEX}
    };

    static constexpr VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex, pos)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vertex, uv)},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vertex, tangent)}
    };

private:
    context* ctx;
    bool opaque;
    std::vector<vertex> vertices;
    std::vector<uint32_t> indices;
    vkres<VkBuffer> vertex_buffer;
    vkres<VkBuffer> index_buffer;
    vkres<VkAccelerationStructureKHR> blas;
    vkres<VkBuffer> blas_buffer;
    VkDeviceAddress blas_address;
};

#endif
