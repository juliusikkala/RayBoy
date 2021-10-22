#include "vkres.hh"
#include "context.hh"
#include <type_traits>

template<typename T>
vkres<T>::vkres()
: value(VK_NULL_HANDLE), ctx(nullptr)
{
}

template<typename T>
vkres<T>::vkres(context& ctx, T t)
: value(t), ctx(&ctx)
{
}

template<typename T>
vkres<T>::vkres(vkres<T>&& other)
: value(other.value), ctx(other.ctx)
{
    other.value = VK_NULL_HANDLE;
}

template<typename T>
vkres<T>::~vkres()
{
    reset();
}

template<typename T>
void vkres<T>::reset(T other)
{
    if(ctx && value != VK_NULL_HANDLE && value != other)
    {
        const device& dev = ctx->get_device();
        VkDevice logical_device = dev.logical_device;
        ctx->at_frame_finish([value=value, logical_device=logical_device](){
#define destroy_type(type) \
            else if constexpr(std::is_same_v<T, Vk##type>) { \
                vkDestroy##type(logical_device, value, nullptr);\
            }
            if constexpr(false) {}
            destroy_type(AccelerationStructureKHR)
            destroy_type(Buffer)
            destroy_type(BufferView)
            destroy_type(CommandPool)
            destroy_type(DescriptorPool)
            destroy_type(DescriptorSetLayout)
            destroy_type(Framebuffer)
            destroy_type(Image)
            destroy_type(ImageView)
            destroy_type(Pipeline)
            destroy_type(PipelineLayout)
            destroy_type(QueryPool)
            destroy_type(RenderPass)
            destroy_type(Sampler)
            destroy_type(Semaphore)
            destroy_type(ShaderModule)
            else static_assert("Unknown Vulkan resource type!");
            ;
        });
    }
    value = other;
}

template<typename T>
void vkres<T>::operator=(vkres<T>&& other)
{
    reset(other.value);
    ctx = other.ctx;
    other.value = VK_NULL_HANDLE;
}

template<typename T>
const T& vkres<T>::operator*() const
{
    return value;
}

template<typename T>
vkres<T>::operator T()
{
    return value;
}

vkres<VkCommandBuffer>::vkres()
: value(VK_NULL_HANDLE), pool(VK_NULL_HANDLE), ctx(nullptr)
{
}

vkres<VkCommandBuffer>::vkres(context& ctx, VkCommandPool pool, VkCommandBuffer buf)
: value(buf), pool(pool), ctx(&ctx)
{
}

vkres<VkCommandBuffer>::vkres(vkres<VkCommandBuffer>&& other)
: value(other.value), pool(other.pool), ctx(other.ctx)
{
    other.value = VK_NULL_HANDLE;
}

vkres<VkCommandBuffer>::~vkres()
{
    reset();
}

void vkres<VkCommandBuffer>::reset(VkCommandBuffer other)
{
    if(pool != VK_NULL_HANDLE && ctx && value != VK_NULL_HANDLE && value != other)
    {
        const device& dev = ctx->get_device();
        VkDevice logical_device = dev.logical_device;
        ctx->at_frame_finish([value=value, pool=pool, logical_device=logical_device](){
            vkFreeCommandBuffers(logical_device, pool, 1, &value);
        });
    }
    value = other;
}

void vkres<VkCommandBuffer>::operator=(vkres<VkCommandBuffer>&& other)
{
    reset(other.value);
    ctx = other.ctx;
    pool = other.pool;
    other.value = VK_NULL_HANDLE;
}

const VkCommandBuffer& vkres<VkCommandBuffer>::operator*() const
{
    return value;
}

vkres<VkCommandBuffer>::operator VkCommandBuffer()
{
    return value;
}

template class vkres<VkAccelerationStructureKHR>;
template class vkres<VkBuffer>;
template class vkres<VkBufferView>;
template class vkres<VkCommandPool>;
template class vkres<VkDescriptorPool>;
template class vkres<VkDescriptorSetLayout>;
template class vkres<VkFramebuffer>;
template class vkres<VkImage>;
template class vkres<VkImageView>;
template class vkres<VkPipeline>;
template class vkres<VkPipelineLayout>;
template class vkres<VkQueryPool>;
template class vkres<VkRenderPass>;
template class vkres<VkSampler>;
template class vkres<VkSemaphore>;
template class vkres<VkShaderModule>;
