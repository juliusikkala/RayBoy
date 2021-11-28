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
    {
        extra_flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT|
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }

    vertex_buffer = upload_buffer(
        ctx, vertex_buf_size, this->vertices.data(),
        extra_flags|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    );
    index_buffer = upload_buffer(
        ctx, index_buf_size, this->indices.data(),
        extra_flags|VK_BUFFER_USAGE_INDEX_BUFFER_BIT
    );

    // Build ray tracing acceleration structure
    if(ctx.get_device().supports_ray_tracing)
        rebuild_acceleration_structure();
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
    if(this->opaque == opaque)
        return;
    this->opaque = opaque;
    if(ctx->get_device().supports_ray_tracing)
        rebuild_acceleration_structure();
}

bool mesh::is_opaque() const
{
    return opaque;
}

void mesh::draw(VkCommandBuffer buf) const
{
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(buf, 0, 1, &*vertex_buffer, &offset);
    vkCmdBindIndexBuffer(buf, *index_buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(buf, indices.size(), 1, 0, 0, 0);
}

void mesh::rebuild_acceleration_structure()
{
    VkBufferDeviceAddressInfo vertex_info = {
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        nullptr,
        vertex_buffer
    };
    VkBufferDeviceAddressInfo index_info = {
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        nullptr,
        index_buffer
    };
    VkAccelerationStructureGeometryKHR as_geom = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        nullptr,
        VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        VkAccelerationStructureGeometryTrianglesDataKHR{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            nullptr,
            VK_FORMAT_R32G32B32_SFLOAT,
            vkGetBufferDeviceAddress(ctx->get_device().logical_device, &vertex_info),
            sizeof(vertex),
            (uint32_t)(this->vertices.size()),
            VK_INDEX_TYPE_UINT32,
            vkGetBufferDeviceAddress(ctx->get_device().logical_device, &index_info),
            0
        },
        opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR
    };

    VkAccelerationStructureBuildGeometryInfoKHR as_build_info = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        nullptr,
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR|
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        1,
        &as_geom,
        nullptr,
        0
    };

    VkAccelerationStructureBuildSizesInfoKHR build_size = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
        nullptr
    };
    uint32_t max_primitive_count = this->indices.size()/3;
    vkGetAccelerationStructureBuildSizesKHR(
        ctx->get_device().logical_device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &as_build_info,
        &max_primitive_count,
        &build_size
    );

    uint32_t alignment = ctx->get_device().as_properties.minAccelerationStructureScratchOffsetAlignment;
    vkres<VkBuffer> scratch_buffer = create_gpu_buffer(
        *ctx,
        build_size.buildScratchSize + alignment,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );
    VkBufferDeviceAddressInfo scratch_info = {
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        nullptr,
        scratch_buffer
    };
    as_build_info.scratchData.deviceAddress = vkGetBufferDeviceAddress(
        ctx->get_device().logical_device, &scratch_info
    );
    as_build_info.scratchData.deviceAddress += alignment - (as_build_info.scratchData.deviceAddress % alignment);

    vkres<VkBuffer> uncompact_blas_buffer = create_gpu_buffer(
        *ctx,
        build_size.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR|
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );

    VkAccelerationStructureCreateInfoKHR as_create_info = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        nullptr,
        0,
        uncompact_blas_buffer,
        0,
        build_size.accelerationStructureSize,
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        0
    };
    VkAccelerationStructureKHR as_tmp;
    vkCreateAccelerationStructureKHR(ctx->get_device().logical_device, &as_create_info, nullptr, &as_tmp);
    vkres<VkAccelerationStructureKHR> uncompact_blas(*ctx, as_tmp);
    as_build_info.dstAccelerationStructure = uncompact_blas;

    VkQueryPool query_pool;
    VkQueryPoolCreateInfo qp_info = {
        VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        nullptr,
        0,
        VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
        1,
        0
    };
    vkCreateQueryPool(ctx->get_device().logical_device, &qp_info, nullptr, &query_pool);

    // Finally, actually build the acceleration structure.
    VkCommandBuffer cmd = begin_command_buffer(*ctx);
    vkCmdResetQueryPool(cmd, query_pool, 0, 1);

    VkAccelerationStructureBuildRangeInfoKHR range = {
        max_primitive_count, 0, 0, 0
    };
    VkAccelerationStructureBuildRangeInfoKHR* range_ptr = &range;

    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &as_build_info, &range_ptr);
    VkMemoryBarrier2KHR barrier = {
        VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR,
        nullptr,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
    };
    VkDependencyInfoKHR deps = {
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR, nullptr, 0,
        1, &barrier, 0, nullptr, 0, nullptr
    };
    vkCmdPipelineBarrier2KHR(cmd, &deps);
    vkCmdWriteAccelerationStructuresPropertiesKHR(cmd, 1, &*uncompact_blas, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, query_pool, 0);
    end_command_buffer(*ctx, cmd);

    // But wait, we still have to compact it too! -.-
    VkDeviceSize compacted_size = 0;
    vkGetQueryPoolResults(
        ctx->get_device().logical_device,
        query_pool,
        0,
        1,
        sizeof(VkDeviceSize),
        &compacted_size,
        sizeof(VkDeviceSize),
        VK_QUERY_RESULT_WAIT_BIT
    );
    vkDestroyQueryPool(ctx->get_device().logical_device, query_pool, nullptr);

    blas_buffer = create_gpu_buffer(
        *ctx,
        compacted_size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR|
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );
    as_create_info.buffer = blas_buffer;
    as_create_info.size = compacted_size;

    vkCreateAccelerationStructureKHR(ctx->get_device().logical_device, &as_create_info, nullptr, &as_tmp);
    blas = vkres(*ctx, as_tmp);

    as_build_info.dstAccelerationStructure = blas;

    cmd = begin_command_buffer(*ctx);
    VkCopyAccelerationStructureInfoKHR copy_info = {
        VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR,
        nullptr,
        uncompact_blas,
        blas,
        VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR
    };
    vkCmdCopyAccelerationStructureKHR(cmd, &copy_info);
    end_command_buffer(*ctx, cmd);

    // Really finally, get the address and store it.
    VkAccelerationStructureDeviceAddressInfoKHR as_addr_info = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        nullptr,
        blas
    };
    blas_address = vkGetAccelerationStructureDeviceAddressKHR(ctx->get_device().logical_device, &as_addr_info);
}
