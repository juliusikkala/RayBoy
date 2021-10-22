#include "mesh.hh"
#include "helpers.hh"

mesh::mesh(
    context& ctx,
    std::vector<vertex>&& vertices,
    std::vector<uint32_t>&& indices,
    bool opaque
):  ctx(&ctx), opaque(opaque), vertices(std::move(vertices)),
    indices(std::move(indices))
{
    size_t vertex_buf_size = this->vertices.size() * sizeof(vertex);
    size_t index_buf_size = this->indices.size() * sizeof(uint32_t);
    VkBufferUsageFlags extra_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if(ctx.get_device().supports_ray_tracing)
        extra_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    vertex_buffer = upload_buffer(
        ctx, vertex_buf_size, this->vertices.data(),
        extra_flags|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    );
    index_buffer = upload_buffer(
        ctx, index_buf_size, this->indices.data(),
        extra_flags|VK_BUFFER_USAGE_INDEX_BUFFER_BIT
    );

    // TODO: Ray tracing acceleration structure
}

VkBuffer mesh::get_vertex_buffer() const
{
    return *vertex_buffer;
}

VkBuffer mesh::get_index_buffer() const
{
    return *index_buffer;
}

VkAccelerationStructureKHR mesh::get_blas() const
{
    return *blas;
}

VkDeviceAddress mesh::get_blas_address() const
{
    return blas_address;
}

void mesh::set_opaque(bool opaque)
{
    this->opaque = opaque;
}

bool mesh::is_opaque() const
{
    return opaque;
}
