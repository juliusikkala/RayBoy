#include "gpu_pipeline.hh"
#include "helpers.hh"
#include <cassert>

gpu_pipeline::gpu_pipeline(context& ctx)
: ctx(&ctx)
{
}

gpu_pipeline::~gpu_pipeline()
{
}

void gpu_pipeline::init_bindings(
    size_t count,
    const std::vector<VkDescriptorSetLayoutBinding>& bindings,
    size_t push_constant_size
){
    this->bindings = bindings;
    this->push_constant_size = push_constant_size;
    descriptor_set_layout = create_descriptor_set_layout(*ctx, bindings);

    VkDevice logical_device = ctx->get_device().logical_device;

    VkPushConstantRange range = {
        VK_SHADER_STAGE_ALL, 0, (uint32_t)push_constant_size
    };

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        nullptr, {},
        1, &*descriptor_set_layout,
        push_constant_size ? 1u : 0u,
        &range
    };
    VkPipelineLayout tmp_layout;
    vkCreatePipelineLayout(
        logical_device, &pipeline_layout_info, nullptr, &tmp_layout
    );
    pipeline_layout = vkres(*ctx, tmp_layout);

    // Create descriptor pool
    std::vector<VkDescriptorPoolSize> pool_sizes = calculate_descriptor_pool_sizes(
        bindings.size(), bindings.data(), count
    );
    VkDescriptorPoolCreateInfo pool_create_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        nullptr,
        0,
        (uint32_t)count,
        (uint32_t)pool_sizes.size(),
        pool_sizes.data()
    };
    VkDescriptorPool tmp_pool;
    vkCreateDescriptorPool(
        logical_device,
        &pool_create_info,
        nullptr,
        &tmp_pool
    );
    descriptor_pool = vkres(*ctx, tmp_pool);

    // Create descriptor sets
    VkDescriptorSetAllocateInfo descriptor_alloc_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        nullptr,
        descriptor_pool,
        1,
        &*descriptor_set_layout
    };

    descriptor_sets.resize(count);
    for(size_t i = 0; i < descriptor_sets.size(); ++i)
    {
        vkAllocateDescriptorSets(
            logical_device,
            &descriptor_alloc_info,
            &descriptor_sets[i]
        );
    }
}

void gpu_pipeline::set_descriptor(
    size_t set_index, size_t binding_index,
    std::vector<VkImageView> views, std::vector<VkSampler> samplers
){
    VkDescriptorSetLayoutBinding bind = find_binding(binding_index);
    std::vector<VkDescriptorImageInfo> image_infos(bind.descriptorCount);

    assert(views.size() == bind.descriptorCount);

    for(size_t i = 0; i < bind.descriptorCount; ++i)
    {
        VkSampler sampler = VK_NULL_HANDLE;
        if(samplers.size() == 1)
            sampler = samplers[0];
        else if(samplers.size() > 1)
        {
            assert(samplers.size() == bind.descriptorCount);
            sampler = samplers[i];
        }
        image_infos[i] = VkDescriptorImageInfo{
            sampler,
            views[i],
            bind.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ?
                VK_IMAGE_LAYOUT_GENERAL :
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
    }
    VkWriteDescriptorSet write = {
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
        descriptor_sets[set_index], (uint32_t)binding_index, 0,
        bind.descriptorCount, bind.descriptorType,
        image_infos.data(), nullptr, nullptr
    };
    vkUpdateDescriptorSets(ctx->get_device().logical_device, 1, &write, 0,  nullptr);
}

void gpu_pipeline::set_descriptor(
    size_t set_index,
    size_t binding_index,
    std::vector<VkBuffer> buffers
){
    VkDescriptorSetLayoutBinding bind = find_binding(binding_index);
    std::vector<VkDescriptorBufferInfo> infos(buffers.size());
    for(size_t i = 0; i < buffers.size(); ++i)
        infos[i] = {buffers[i], 0, VK_WHOLE_SIZE};

    VkWriteDescriptorSet write = {
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
        descriptor_sets[set_index], (uint32_t)binding_index, 0,
        (uint32_t )infos.size(), bind.descriptorType,
        nullptr, infos.data(), nullptr
    };
    vkUpdateDescriptorSets(ctx->get_device().logical_device, 1, &write, 0,  nullptr);
}

void gpu_pipeline::push_constants(VkCommandBuffer buf, const void* data)
{
    vkCmdPushConstants(
        buf, pipeline_layout, VK_SHADER_STAGE_ALL, 0,
        (uint32_t)push_constant_size, data
    );
}

VkDescriptorSetLayoutBinding gpu_pipeline::find_binding(size_t binding_index) const
{
    for(const VkDescriptorSetLayoutBinding& bind: bindings)
    {
        if(bind.binding == binding_index)
        {
            return bind;
        }
    }
    assert(false && "Missing binding index");
}
