#ifndef VEXOS_VULKAN_VEX_H
#define VEXOS_VULKAN_VEX_H

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Development-only platform WSI extension. This value is deliberately outside
 * the registered extension range currently in use, but it is NOT assigned by
 * Khronos. It must be replaced before a stable Vulkan ABI is published. */
#define VK_VEX_SURFACE_EXTENSION_NAME "VK_VEX_surface"
#define VK_VEX_SURFACE_SPEC_VERSION 0
#define VK_STRUCTURE_TYPE_VEX_SURFACE_CREATE_INFO_EXPERIMENTAL ((VkStructureType)0x6f000001)
#define VK_STRUCTURE_TYPE_VEX_MEMORY_IMPORT_INFO_EXPERIMENTAL ((VkStructureType)0x6f000002)

typedef struct VkVexSurfaceCreateInfoVEX {
    VkStructureType sType;
    const void *pNext;
    VkFlags flags;
    uint64_t surfaceCapability;
} VkVexSurfaceCreateInfoVEX;

/* Import one explicitly transferred Vex Surface capability as host-visible
 * Vulkan memory. The capability remains process-local and must be delivered
 * with Vex typed IPC; this structure never accepts an object identity. */
typedef struct VkVexMemoryImportInfoVEX {
    VkStructureType sType;
    const void *pNext;
    uint64_t surfaceCapability;
} VkVexMemoryImportInfoVEX;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateVexSurfaceVEX(
    VkInstance instance,
    const VkVexSurfaceCreateInfoVEX *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkSurfaceKHR *pSurface);

VKAPI_ATTR VkResult VKAPI_CALL vkGetVexMemoryCapabilityVEX(
    VkDevice device,
    VkDeviceMemory memory,
    uint64_t *pSurfaceCapability);

#ifdef __cplusplus
}
#endif

#endif
