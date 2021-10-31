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

VkCommandPool vkres<VkCommandBuffer>::get_pool() const
{
    return pool;
}

vkres<VkBuffer>::vkres()
: buffer(VK_NULL_HANDLE), allocation(VK_NULL_HANDLE), ctx(nullptr)
{
}

vkres<VkBuffer>::vkres(context& ctx, VkBuffer buf, VmaAllocation alloc)
: buffer(buf), allocation(alloc), ctx(&ctx)
{
}

vkres<VkBuffer>::vkres(vkres<VkBuffer>&& other)
{
    operator=(std::move(other));
}

vkres<VkBuffer>::~vkres()
{
    reset();
}

void vkres<VkBuffer>::reset(VkBuffer buf, VmaAllocation alloc)
{
    if(ctx && buffer)
    {
        const device& dev = ctx->get_device();
        ctx->at_frame_finish([
            buffer=this->buffer,
            allocation=this->allocation,
            logical_device=dev.logical_device,
            allocator=dev.allocator
        ](){
            vkDestroyBuffer(logical_device, buffer, nullptr);
            if(allocation != VK_NULL_HANDLE)
                vmaFreeMemory(allocator, allocation);
        });
    }
    buffer = buf;
    allocation = alloc;
}

void vkres<VkBuffer>::operator=(vkres<VkBuffer>&& other)
{
    this->buffer = other.buffer;
    this->allocation = other.allocation;
    this->ctx = other.ctx;
    other.buffer = VK_NULL_HANDLE;
    other.allocation = VK_NULL_HANDLE;
}

const VkBuffer& vkres<VkBuffer>::operator*() const
{
    return buffer;
}

vkres<VkBuffer>::operator VkBuffer()
{
    return buffer;
}

VmaAllocation vkres<VkBuffer>::get_allocation() const
{
    return allocation;
}

vkres<VkImage>::vkres()
: image(VK_NULL_HANDLE), allocation(VK_NULL_HANDLE), ctx(nullptr)
{
}

vkres<VkImage>::vkres(context& ctx, VkImage img, VmaAllocation alloc)
: image(img), allocation(alloc), ctx(&ctx)
{
}

vkres<VkImage>::vkres(vkres<VkImage>&& other)
{
    operator=(std::move(other));
}

vkres<VkImage>::~vkres()
{
    reset();
}

void vkres<VkImage>::reset(VkImage img, VmaAllocation alloc)
{
    if(ctx && image)
    {
        const device& dev = ctx->get_device();
        ctx->at_frame_finish([
            image=this->image,
            allocation=this->allocation,
            logical_device=dev.logical_device,
            allocator=dev.allocator
        ](){
            vkDestroyImage(logical_device, image, nullptr);
            if(allocation != VK_NULL_HANDLE)
                vmaFreeMemory(allocator, allocation);
        });
    }
    image = img;
    allocation = alloc;
}

void vkres<VkImage>::operator=(vkres<VkImage>&& other)
{
    this->image = other.image;
    this->allocation = other.allocation;
    this->ctx = other.ctx;
    other.image = VK_NULL_HANDLE;
    other.image = VK_NULL_HANDLE;
}

const VkImage& vkres<VkImage>::operator*() const
{
    return image;
}

vkres<VkImage>::operator VkImage()
{
    return image;
}

template class vkres<VkAccelerationStructureKHR>;
template class vkres<VkBufferView>;
template class vkres<VkCommandPool>;
template class vkres<VkDescriptorPool>;
template class vkres<VkDescriptorSetLayout>;
template class vkres<VkFramebuffer>;
template class vkres<VkImageView>;
template class vkres<VkPipeline>;
template class vkres<VkPipelineLayout>;
template class vkres<VkQueryPool>;
template class vkres<VkRenderPass>;
template class vkres<VkSampler>;
template class vkres<VkSemaphore>;
template class vkres<VkShaderModule>;
