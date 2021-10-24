#include "sampler.hh"

sampler::sampler(
    context& ctx,
    VkFilter min, VkFilter mag,
    VkSamplerMipmapMode mipmap_mode,
    VkSamplerAddressMode extension,
    float anisotropy,
    float max_mipmap,
    float mipmap_bias,
    bool shadow
){
    VkSamplerCreateInfo info = {
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr, 0,
        min, mag, mipmap_mode, extension, extension, extension, mipmap_bias,
        anisotropy > 0 ? VK_TRUE : VK_FALSE, anisotropy,
        shadow, shadow ? VK_COMPARE_OP_LESS : VK_COMPARE_OP_ALWAYS,
        0.0f, max_mipmap,
        VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        VK_FALSE
    };
    VkSampler tmp;
    vkCreateSampler(ctx.get_device().logical_device, &info, nullptr, &tmp);
    sampler_object = vkres(ctx, tmp);
}

VkSampler sampler::get() const
{
    return *sampler_object;
}
