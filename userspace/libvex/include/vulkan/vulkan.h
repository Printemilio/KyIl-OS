#ifndef VEXOS_VULKAN_H
#define VEXOS_VULKAN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VK_NULL_HANDLE 0
#define VK_TRUE 1
#define VK_FALSE 0

#define VK_MAKE_VERSION(major, minor, patch) ((((uint32_t)(major)) << 22) | (((uint32_t)(minor)) << 12) | ((uint32_t)(patch)))
#define VK_API_VERSION_1_0 VK_MAKE_VERSION(1, 0, 0)

typedef uint32_t VkFlags;
typedef uint32_t VkBool32;
typedef uint32_t VkResult;
typedef uint64_t VkDeviceSize;
typedef uint64_t VkInstance;
typedef uint64_t VkPhysicalDevice;
typedef uint64_t VkDevice;
typedef uint64_t VkQueue;
typedef uint64_t VkCommandPool;
typedef uint64_t VkCommandBuffer;
typedef uint64_t VkImage;
typedef uint64_t VkImageView;
typedef uint64_t VkBuffer;
typedef uint64_t VkDeviceMemory;
typedef uint64_t VkShaderModule;
typedef uint64_t VkRenderPass;
typedef uint64_t VkFramebuffer;
typedef uint64_t VkPipelineLayout;
typedef uint64_t VkPipeline;
typedef uint64_t VkSurfaceKHR;
typedef uint64_t VkSwapchainKHR;
typedef uint64_t VkSemaphore;
typedef uint64_t VkFence;
typedef uint64_t VkDescriptorSetLayout;
typedef uint64_t VkDescriptorPool;
typedef uint64_t VkDescriptorSet;
typedef uint64_t VkSampler;
typedef uint64_t VkQueryPool;
typedef void (*PFN_vkVoidFunction)(void);
typedef uint32_t VkIndexType;

enum {
    VK_SUCCESS = 0,
    VK_NOT_READY = 1,
    VK_TIMEOUT = 2,
    VK_ERROR_OUT_OF_HOST_MEMORY = (uint32_t)-1,
    VK_ERROR_INITIALIZATION_FAILED = (uint32_t)-3,
    VK_ERROR_DEVICE_LOST = (uint32_t)-4,
    VK_ERROR_UNKNOWN = (uint32_t)-13
};

enum {
    VK_STRUCTURE_TYPE_APPLICATION_INFO = 0,
    VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1,
    VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO = 2,
    VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO = 3,
    VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO = 38,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO = 42,
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO = 43,
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO = 45,
    VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO = 12,
    VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO = 14,
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO = 15,
    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO = 16,
    VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO = 30,
    VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO = 37,
    VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO = 38 + 1,
    VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO = 28,
    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO = 100,
    VK_STRUCTURE_TYPE_FENCE_CREATE_INFO = 8,
    VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO = 9,
    VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER = 44,
    VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR = 1000001000,
    VK_STRUCTURE_TYPE_PRESENT_INFO_KHR = 1000001001,
    VK_STRUCTURE_TYPE_VEX_SURFACE_CREATE_INFO_KHR = 1000002000,
    VK_STRUCTURE_TYPE_SUBMIT_INFO = 4
};

enum {
    VK_DESCRIPTOR_TYPE_SAMPLER                = 0,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER = 1,
    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE          = 2,
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE          = 3,
    VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER   = 4,
    VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER   = 5,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER         = 6,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER         = 7,
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC = 8,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC = 9,
    VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT       = 10
};

enum {
    VK_SHADER_STAGE_VERTEX_BIT                  = 0x00000001,
    VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT    = 0x00000002,
    VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT = 0x00000004,
    VK_SHADER_STAGE_GEOMETRY_BIT                = 0x00000008,
    VK_SHADER_STAGE_FRAGMENT_BIT                = 0x00000010,
    VK_SHADER_STAGE_COMPUTE_BIT                 = 0x00000020,
    VK_SHADER_STAGE_ALL_GRAPHICS                = 0x0000001F,
    VK_SHADER_STAGE_ALL                         = 0x7FFFFFFF
};

enum {
    VK_FILTER_NEAREST  = 0,
    VK_FILTER_LINEAR   = 1
};

enum {
    VK_SAMPLER_MIPMAP_MODE_NEAREST = 0,
    VK_SAMPLER_MIPMAP_MODE_LINEAR  = 1
};

enum {
    VK_SAMPLER_ADDRESS_MODE_REPEAT               = 0,
    VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT      = 1,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE        = 2,
    VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER      = 3,
    VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE = 4
};

enum {
    VK_COMPARE_OP_NEVER            = 0,
    VK_COMPARE_OP_LESS             = 1,
    VK_COMPARE_OP_EQUAL            = 2,
    VK_COMPARE_OP_LESS_OR_EQUAL    = 3,
    VK_COMPARE_OP_GREATER          = 4,
    VK_COMPARE_OP_NOT_EQUAL        = 5,
    VK_COMPARE_OP_GREATER_OR_EQUAL = 6,
    VK_COMPARE_OP_ALWAYS           = 7
};

enum {
    VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK = 0,
    VK_BORDER_COLOR_INT_TRANSPARENT_BLACK   = 1,
    VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK      = 2,
    VK_BORDER_COLOR_INT_OPAQUE_BLACK        = 3,
    VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE      = 4,
    VK_BORDER_COLOR_INT_OPAQUE_WHITE        = 5
};

enum {
    VK_QUERY_TYPE_OCCLUSION           = 0,
    VK_QUERY_TYPE_PIPELINE_STATISTICS = 1,
    VK_QUERY_TYPE_TIMESTAMP           = 2
};

enum {
    VK_QUERY_RESULT_64_BIT                = 0x00000001,
    VK_QUERY_RESULT_WAIT_BIT              = 0x00000002,
    VK_QUERY_RESULT_WITH_AVAILABILITY_BIT = 0x00000004,
    VK_QUERY_RESULT_PARTIAL_BIT           = 0x00000008
};

enum {
    VK_IMAGE_TYPE_1D = 0,
    VK_IMAGE_TYPE_2D = 1,
    VK_IMAGE_TYPE_3D = 2
};

enum {
    VK_IMAGE_TILING_OPTIMAL = 0,
    VK_IMAGE_TILING_LINEAR  = 1
};

enum {
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO = 32,
    VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO       = 33,
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO      = 34,
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET              = 35,
    VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET               = 36,
    VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO               = 31,
    VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO            = 11,
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2        = 1000059000
};

enum {
    VK_QUEUE_GRAPHICS_BIT = 0x00000001,
    VK_COMMAND_BUFFER_LEVEL_PRIMARY = 0,
    VK_IMAGE_LAYOUT_GENERAL = 1,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL = 6,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL = 7,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR = 1000001002,
    VK_FORMAT_B8G8R8A8_UNORM = 44,
    VK_COLOR_SPACE_SRGB_NONLINEAR_KHR = 0,
    VK_PRESENT_MODE_FIFO_KHR = 2,
    VK_PRESENT_MODE_IMMEDIATE_KHR = 0,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT = 0x00000001,
    VK_IMAGE_USAGE_TRANSFER_DST_BIT = 0x00000002,
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT = 0x00000001,
    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT = 0x00000002,
    VK_FENCE_CREATE_SIGNALED_BIT = 0x00000001,
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT = 0x00000001,
    VK_IMAGE_ASPECT_COLOR_BIT = 0x00000001,
    VK_PIPELINE_BIND_POINT_GRAPHICS = 0,
    VK_INDEX_TYPE_UINT32 = 1
};

typedef struct VkApplicationInfo {
    uint32_t sType;
    const void *pNext;
    const char *pApplicationName;
    uint32_t applicationVersion;
    const char *pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
} VkApplicationInfo;

typedef struct VkInstanceCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    const VkApplicationInfo *pApplicationInfo;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
} VkInstanceCreateInfo;

typedef struct VkAllocationCallbacks {
    const void *pUserData;
} VkAllocationCallbacks;

typedef struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[256];
} VkPhysicalDeviceProperties;

typedef struct VkMemoryType {
    VkFlags propertyFlags;
    uint32_t heapIndex;
} VkMemoryType;

typedef struct VkMemoryHeap {
    VkDeviceSize size;
    VkFlags flags;
} VkMemoryHeap;

typedef struct VkPhysicalDeviceMemoryProperties {
    uint32_t memoryTypeCount;
    VkMemoryType memoryTypes[8];
    uint32_t memoryHeapCount;
    VkMemoryHeap memoryHeaps[8];
} VkPhysicalDeviceMemoryProperties;

typedef struct VkFormatProperties {
    VkFlags linearTilingFeatures;
    VkFlags optimalTilingFeatures;
    VkFlags bufferFeatures;
} VkFormatProperties;

typedef struct VkQueueFamilyProperties {
    VkFlags queueFlags;
    uint32_t queueCount;
    uint32_t timestampValidBits;
    struct {
        uint32_t width;
        uint32_t height;
        uint32_t depth;
    } minImageTransferGranularity;
} VkQueueFamilyProperties;

typedef struct VkDeviceQueueCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
    uint32_t queueCount;
    const float *pQueuePriorities;
} VkDeviceQueueCreateInfo;

typedef struct VkDeviceCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queueCreateInfoCount;
    const VkDeviceQueueCreateInfo *pQueueCreateInfos;
    uint32_t enabledLayerCount;
    const char *const *ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char *const *ppEnabledExtensionNames;
    const void *pEnabledFeatures;
} VkDeviceCreateInfo;

typedef struct VkExtent2D {
    uint32_t width;
    uint32_t height;
} VkExtent2D;

typedef struct VkSwapchainCreateInfoKHR {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    uint64_t surface;
    uint32_t minImageCount;
    uint32_t imageFormat;
    uint32_t imageColorSpace;
    VkExtent2D imageExtent;
    uint32_t imageArrayLayers;
    VkFlags imageUsage;
    uint32_t imageSharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
    uint32_t preTransform;
    uint32_t compositeAlpha;
    uint32_t presentMode;
    VkBool32 clipped;
    VkSwapchainKHR oldSwapchain;
} VkSwapchainCreateInfoKHR;

typedef struct VkVexSurfaceCreateInfoKHR {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} VkVexSurfaceCreateInfoKHR;

typedef struct VkCommandPoolCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queueFamilyIndex;
} VkCommandPoolCreateInfo;

typedef struct VkCommandBufferAllocateInfo {
    uint32_t sType;
    const void *pNext;
    VkCommandPool commandPool;
    uint32_t level;
    uint32_t commandBufferCount;
} VkCommandBufferAllocateInfo;

typedef struct VkCommandBufferBeginInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    const void *pInheritanceInfo;
} VkCommandBufferBeginInfo;

typedef struct VkMemoryAllocateInfo {
    uint32_t sType;
    const void *pNext;
    VkDeviceSize allocationSize;
    uint32_t memoryTypeIndex;
} VkMemoryAllocateInfo;

typedef struct VkBufferCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    VkDeviceSize size;
    VkFlags usage;
    uint32_t sharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
} VkBufferCreateInfo;

typedef struct VkImageSubresourceRange {
    VkFlags aspectMask;
    uint32_t baseMipLevel;
    uint32_t levelCount;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} VkImageSubresourceRange;

typedef struct VkImageCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t imageType;
    uint32_t format;
    struct {
        uint32_t width;
        uint32_t height;
        uint32_t depth;
    } extent;
    uint32_t mipLevels;
    uint32_t arrayLayers;
    uint32_t samples;
    uint32_t tiling;
    VkFlags usage;
    uint32_t sharingMode;
    uint32_t queueFamilyIndexCount;
    const uint32_t *pQueueFamilyIndices;
    uint32_t initialLayout;
} VkImageCreateInfo;

typedef struct VkImageViewCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    VkImage image;
    uint32_t viewType;
    uint32_t format;
    struct {
        uint32_t r;
        uint32_t g;
        uint32_t b;
        uint32_t a;
    } components;
    VkImageSubresourceRange subresourceRange;
} VkImageViewCreateInfo;

typedef struct VkShaderModuleCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    size_t codeSize;
    const uint32_t *pCode;
} VkShaderModuleCreateInfo;

typedef struct VkPipelineLayoutCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t setLayoutCount;
    const void *pSetLayouts;
    uint32_t pushConstantRangeCount;
    const void *pPushConstantRanges;
} VkPipelineLayoutCreateInfo;

typedef struct VkAttachmentDescription {
    VkFlags flags;
    uint32_t format;
    uint32_t samples;
    uint32_t loadOp;
    uint32_t storeOp;
    uint32_t stencilLoadOp;
    uint32_t stencilStoreOp;
    uint32_t initialLayout;
    uint32_t finalLayout;
} VkAttachmentDescription;

typedef struct VkAttachmentReference {
    uint32_t attachment;
    uint32_t layout;
} VkAttachmentReference;

typedef struct VkSubpassDescription {
    VkFlags flags;
    uint32_t pipelineBindPoint;
    uint32_t inputAttachmentCount;
    const void *pInputAttachments;
    uint32_t colorAttachmentCount;
    const VkAttachmentReference *pColorAttachments;
    const void *pResolveAttachments;
    const void *pDepthStencilAttachment;
    uint32_t preserveAttachmentCount;
    const uint32_t *pPreserveAttachments;
} VkSubpassDescription;

typedef struct VkRenderPassCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t attachmentCount;
    const VkAttachmentDescription *pAttachments;
    uint32_t subpassCount;
    const VkSubpassDescription *pSubpasses;
    uint32_t dependencyCount;
    const void *pDependencies;
} VkRenderPassCreateInfo;

typedef struct VkFramebufferCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    VkRenderPass renderPass;
    uint32_t attachmentCount;
    const VkImageView *pAttachments;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
} VkFramebufferCreateInfo;

typedef struct VkGraphicsPipelineCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t stageCount;
    const void *pStages;
    const void *pVertexInputState;
    const void *pInputAssemblyState;
    const void *pTessellationState;
    const void *pViewportState;
    const void *pRasterizationState;
    const void *pMultisampleState;
    const void *pDepthStencilState;
    const void *pColorBlendState;
    const void *pDynamicState;
    VkPipelineLayout layout;
    VkRenderPass renderPass;
    uint32_t subpass;
    VkPipeline basePipelineHandle;
    int32_t basePipelineIndex;
} VkGraphicsPipelineCreateInfo;

typedef struct VkRect2D {
    struct {
        int32_t x;
        int32_t y;
    } offset;
    VkExtent2D extent;
} VkRect2D;

typedef struct VkViewport {
    float x;
    float y;
    float width;
    float height;
    float minDepth;
    float maxDepth;
} VkViewport;

typedef struct VkRenderPassBeginInfo {
    uint32_t sType;
    const void *pNext;
    VkRenderPass renderPass;
    VkFramebuffer framebuffer;
    VkRect2D renderArea;
    uint32_t clearValueCount;
    const void *pClearValues;
} VkRenderPassBeginInfo;

typedef struct VkFenceCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
} VkFenceCreateInfo;

typedef struct VkSemaphoreCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
} VkSemaphoreCreateInfo;

typedef union VkClearColorValue {
    float float32[4];
    int32_t int32[4];
    uint32_t uint32[4];
} VkClearColorValue;

typedef struct VkImageSubresourceLayers {
    VkFlags aspectMask;
    uint32_t mipLevel;
    uint32_t baseArrayLayer;
    uint32_t layerCount;
} VkImageSubresourceLayers;

typedef struct VkBufferImageCopy {
    VkDeviceSize bufferOffset;
    uint32_t bufferRowLength;
    uint32_t bufferImageHeight;
    VkImageSubresourceLayers imageSubresource;
    struct {
        int32_t x;
        int32_t y;
        int32_t z;
    } imageOffset;
    struct {
        uint32_t width;
        uint32_t height;
        uint32_t depth;
    } imageExtent;
} VkBufferImageCopy;

typedef struct VkMemoryRequirements {
    VkDeviceSize size;
    VkDeviceSize alignment;
    uint32_t memoryTypeBits;
} VkMemoryRequirements;

typedef struct VkImageMemoryBarrier {
    uint32_t sType;
    const void *pNext;
    VkFlags srcAccessMask;
    VkFlags dstAccessMask;
    uint32_t oldLayout;
    uint32_t newLayout;
    uint32_t srcQueueFamilyIndex;
    uint32_t dstQueueFamilyIndex;
    VkImage image;
    VkImageSubresourceRange subresourceRange;
} VkImageMemoryBarrier;

typedef struct VkSubmitInfo {
    uint32_t sType;
    const void *pNext;
    uint32_t waitSemaphoreCount;
    const VkSemaphore *pWaitSemaphores;
    const VkFlags *pWaitDstStageMask;
    uint32_t commandBufferCount;
    const VkCommandBuffer *pCommandBuffers;
    uint32_t signalSemaphoreCount;
    const VkSemaphore *pSignalSemaphores;
} VkSubmitInfo;

typedef struct VkPresentInfoKHR {
    uint32_t sType;
    const void *pNext;
    uint32_t waitSemaphoreCount;
    const VkSemaphore *pWaitSemaphores;
    uint32_t swapchainCount;
    const VkSwapchainKHR *pSwapchains;
    const uint32_t *pImageIndices;
    VkResult *pResults;
} VkPresentInfoKHR;

typedef struct VkSurfaceCapabilitiesKHR {
    uint32_t minImageCount;
    uint32_t maxImageCount;
    VkExtent2D currentExtent;
    VkExtent2D minImageExtent;
    VkExtent2D maxImageExtent;
    uint32_t maxImageArrayLayers;
    VkFlags supportedTransforms;
    uint32_t currentTransform;
    VkFlags supportedCompositeAlpha;
    VkFlags supportedUsageFlags;
} VkSurfaceCapabilitiesKHR;

typedef struct VkSurfaceFormatKHR {
    uint32_t format;
    uint32_t colorSpace;
} VkSurfaceFormatKHR;

VkResult vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkInstance *pInstance);
void vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks *pAllocator);
VkResult vkCreateVexSurfaceKHR(VkInstance instance, const VkVexSurfaceCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
void vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface, const VkAllocationCallbacks *pAllocator);
VkResult vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount, VkPhysicalDevice *pPhysicalDevices);
void vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties *pProperties);
void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties *pMemoryProperties);
void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, VkFormatProperties *pFormatProperties);
void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties *pQueueFamilyProperties);
VkResult vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32 *pSupported);
VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR *pSurfaceCapabilities);
VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats);
VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t *pPresentModeCount, uint32_t *pPresentModes);
VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDevice *pDevice);
void vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator);
void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue *pQueue);
VkResult vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSwapchainKHR *pSwapchain);
void vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *pAllocator);
VkResult vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *pSwapchainImageCount, VkImage *pSwapchainImages);
VkResult vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t *pImageIndex);
VkResult vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkCommandPool *pCommandPool);
void vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *pAllocator);
VkResult vkResetCommandPool(VkDevice device, VkCommandPool commandPool, VkFlags flags);
VkResult vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo *pAllocateInfo, VkCommandBuffer *pCommandBuffers);
void vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer *pCommandBuffers);
VkResult vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo);
VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer);
VkResult vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkFlags flags);
void vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, uint32_t imageLayout, const VkClearColorValue *pColor, uint32_t rangeCount, const VkImageSubresourceRange *pRanges);
VkResult vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer);
void vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *pAllocator);
void vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *pMemoryRequirements);
VkResult vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo, const VkAllocationCallbacks *pAllocator, VkDeviceMemory *pMemory);
void vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *pAllocator);
VkResult vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);
VkResult vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkFlags flags, void **ppData);
void vkUnmapMemory(VkDevice device, VkDeviceMemory memory);
VkResult vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImage *pImage);
void vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *pAllocator);
void vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements *pMemoryRequirements);
VkResult vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset);
VkResult vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkImageView *pView);
void vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks *pAllocator);
VkResult vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkShaderModule *pShaderModule);
void vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks *pAllocator);
VkResult vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkRenderPass *pRenderPass);
void vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *pAllocator);
VkResult vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFramebuffer *pFramebuffer);
void vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *pAllocator);
VkResult vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout);
void vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks *pAllocator);
VkResult vkCreateGraphicsPipelines(VkDevice device, VkPipeline pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator, VkPipeline *pPipelines);
void vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *pAllocator);
void vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo *pRenderPassBegin, uint32_t contents);
void vkCmdEndRenderPass(VkCommandBuffer commandBuffer);
void vkCmdBindPipeline(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipeline pipeline);
void vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer *pBuffers, const VkDeviceSize *pOffsets);
void vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
void vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport *pViewports);
void vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D *pScissors);
void vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
void vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
void vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t imageLayout, uint32_t regionCount, const VkBufferImageCopy *pRegions);
void vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout, VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void *pRegions);
void vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout, VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void *pRegions, uint32_t filter);
void vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkFlags srcStageMask, VkFlags dstStageMask, VkFlags dependencyFlags, uint32_t memoryBarrierCount, const void *pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const void *pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier *pImageMemoryBarriers);
VkResult vkCreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkFence *pFence);
void vkDestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator);
VkResult vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences);
VkResult vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences, VkBool32 waitAll, uint64_t timeout);
VkResult vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSemaphore *pSemaphore);
void vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *pAllocator);
VkResult vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits, VkFence fence);
VkResult vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo);
VkResult vkQueueWaitIdle(VkQueue queue);
VkResult vkDeviceWaitIdle(VkDevice device);
PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance instance, const char *pName);
PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice device, const char *pName);

/* ── Descriptor sets ─────────────────────────────────────────────────────── */

typedef struct VkDescriptorSetLayoutBinding {
    uint32_t binding;
    uint32_t descriptorType;
    uint32_t descriptorCount;
    VkFlags  stageFlags;
    const VkSampler *pImmutableSamplers;
} VkDescriptorSetLayoutBinding;

typedef struct VkDescriptorSetLayoutCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t bindingCount;
    const VkDescriptorSetLayoutBinding *pBindings;
} VkDescriptorSetLayoutCreateInfo;

typedef struct VkDescriptorPoolSize {
    uint32_t type;
    uint32_t descriptorCount;
} VkDescriptorPoolSize;

typedef struct VkDescriptorPoolCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t maxSets;
    uint32_t poolSizeCount;
    const VkDescriptorPoolSize *pPoolSizes;
} VkDescriptorPoolCreateInfo;

typedef struct VkDescriptorSetAllocateInfo {
    uint32_t sType;
    const void *pNext;
    VkDescriptorPool descriptorPool;
    uint32_t descriptorSetCount;
    const VkDescriptorSetLayout *pSetLayouts;
} VkDescriptorSetAllocateInfo;

typedef struct VkDescriptorImageInfo {
    VkSampler sampler;
    VkImageView imageView;
    uint32_t imageLayout;
} VkDescriptorImageInfo;

typedef struct VkDescriptorBufferInfo {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize range;
} VkDescriptorBufferInfo;

typedef struct VkWriteDescriptorSet {
    uint32_t sType;
    const void *pNext;
    VkDescriptorSet dstSet;
    uint32_t dstBinding;
    uint32_t dstArrayElement;
    uint32_t descriptorCount;
    uint32_t descriptorType;
    const VkDescriptorImageInfo *pImageInfo;
    const VkDescriptorBufferInfo *pBufferInfo;
    const VkBuffer *pTexelBufferView;
} VkWriteDescriptorSet;

typedef struct VkCopyDescriptorSet {
    uint32_t sType;
    const void *pNext;
    VkDescriptorSet srcSet;
    uint32_t srcBinding;
    uint32_t srcArrayElement;
    VkDescriptorSet dstSet;
    uint32_t dstBinding;
    uint32_t dstArrayElement;
    uint32_t descriptorCount;
} VkCopyDescriptorSet;

VkResult vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout);
void     vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks *pAllocator);
VkResult vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool);
void     vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks *pAllocator);
VkResult vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, VkFlags flags);
VkResult vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo, VkDescriptorSet *pDescriptorSets);
VkResult vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets);
void     vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet *pDescriptorCopies);
void     vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, uint32_t pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t *pDynamicOffsets);
void     vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkFlags stageFlags, uint32_t offset, uint32_t size, const void *pValues);

/* ── Sampler ─────────────────────────────────────────────────────────────── */

typedef struct VkSamplerCreateInfo {
    uint32_t  sType;
    const void *pNext;
    VkFlags   flags;
    uint32_t  magFilter;
    uint32_t  minFilter;
    uint32_t  mipmapMode;
    uint32_t  addressModeU;
    uint32_t  addressModeV;
    uint32_t  addressModeW;
    float     mipLodBias;
    VkBool32  anisotropyEnable;
    float     maxAnisotropy;
    VkBool32  compareEnable;
    uint32_t  compareOp;
    float     minLod;
    float     maxLod;
    uint32_t  borderColor;
    VkBool32  unnormalizedCoordinates;
} VkSamplerCreateInfo;

VkResult vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkSampler *pSampler);
void     vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *pAllocator);

/* ── Query pool ──────────────────────────────────────────────────────────── */

typedef struct VkQueryPoolCreateInfo {
    uint32_t sType;
    const void *pNext;
    VkFlags flags;
    uint32_t queryType;
    uint32_t queryCount;
    VkFlags pipelineStatistics;
} VkQueryPoolCreateInfo;

VkResult vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkQueryPool *pQueryPool);
void     vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *pAllocator);
void     vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);
void     vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query, VkFlags flags);
void     vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t query);
VkResult vkGetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void *pData, VkDeviceSize stride, VkFlags flags);
void     vkCmdWriteTimestamp(VkCommandBuffer commandBuffer, VkFlags pipelineStage, VkQueryPool queryPool, uint32_t query);

/* ── Physical device features / format properties ───────────────────────── */

typedef struct VkPhysicalDeviceFeatures {
    VkBool32 robustBufferAccess;
    VkBool32 fullDrawIndexUint32;
    VkBool32 imageCubeArray;
    VkBool32 independentBlend;
    VkBool32 geometryShader;
    VkBool32 tessellationShader;
    VkBool32 sampleRateShading;
    VkBool32 dualSrcBlend;
    VkBool32 logicOp;
    VkBool32 multiDrawIndirect;
    VkBool32 drawIndirectFirstInstance;
    VkBool32 depthClamp;
    VkBool32 depthBiasClamp;
    VkBool32 fillModeNonSolid;
    VkBool32 depthBounds;
    VkBool32 wideLines;
    VkBool32 largePoints;
    VkBool32 alphaToOne;
    VkBool32 multiViewport;
    VkBool32 samplerAnisotropy;
    VkBool32 textureCompressionETC2;
    VkBool32 textureCompressionASTC_LDR;
    VkBool32 textureCompressionBC;
    VkBool32 occlusionQueryPrecise;
    VkBool32 pipelineStatisticsQuery;
    VkBool32 vertexPipelineStoresAndAtomics;
    VkBool32 fragmentStoresAndAtomics;
    VkBool32 shaderTessellationAndGeometryPointSize;
    VkBool32 shaderImageGatherExtended;
    VkBool32 shaderStorageImageExtendedFormats;
    VkBool32 shaderStorageImageMultisample;
    VkBool32 shaderStorageImageReadWithoutFormat;
    VkBool32 shaderStorageImageWriteWithoutFormat;
    VkBool32 shaderUniformBufferArrayDynamicIndexing;
    VkBool32 shaderSampledImageArrayDynamicIndexing;
    VkBool32 shaderStorageBufferArrayDynamicIndexing;
    VkBool32 shaderStorageImageArrayDynamicIndexing;
    VkBool32 shaderClipDistance;
    VkBool32 shaderCullDistance;
    VkBool32 shaderFloat64;
    VkBool32 shaderInt64;
    VkBool32 shaderInt16;
    VkBool32 shaderResourceResidency;
    VkBool32 shaderResourceMinLod;
    VkBool32 sparseBinding;
    VkBool32 sparseResidencyBuffer;
    VkBool32 sparseResidencyImage2D;
    VkBool32 sparseResidencyImage3D;
    VkBool32 sparseResidency2Samples;
    VkBool32 sparseResidency4Samples;
    VkBool32 sparseResidency8Samples;
    VkBool32 sparseResidency16Samples;
    VkBool32 sparseResidencyAliased;
    VkBool32 variableMultisampleRate;
    VkBool32 inheritedQueries;
} VkPhysicalDeviceFeatures;

typedef struct VkImageFormatProperties {
    struct { uint32_t width; uint32_t height; uint32_t depth; } maxExtent;
    uint32_t maxMipLevels;
    uint32_t maxArrayLayers;
    VkFlags  sampleCounts;
    VkDeviceSize maxResourceSize;
} VkImageFormatProperties;

typedef struct VkBufferCopy {
    VkDeviceSize srcOffset;
    VkDeviceSize dstOffset;
    VkDeviceSize size;
} VkBufferCopy;

void     vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures *pFeatures);
VkResult vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice physicalDevice, uint32_t format, uint32_t type, uint32_t tiling, VkFlags usage, VkFlags flags, VkImageFormatProperties *pImageFormatProperties);
void     vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferCopy *pRegions);

#ifdef __cplusplus
}
#endif

#endif
