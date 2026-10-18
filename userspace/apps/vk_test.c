#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <unos/vex_abi.h>
#include <unos/vex_legacy_compat.h>
#include <vulkan/vulkan_vex.h>
#include <vex_runtime.h>

#include "vex_syscall.h"

_Static_assert(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO == 5, "Khronos memory sType");
_Static_assert(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO == 39, "Khronos command-pool sType");
_Static_assert(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO == 43, "Khronos render-pass-begin sType");
_Static_assert(VK_STRUCTURE_TYPE_DISPLAY_MODE_CREATE_INFO_KHR == 1000002000,
               "The old Vex WSI number belongs to Khronos display mode");
_Static_assert(VK_STRUCTURE_TYPE_VEX_SURFACE_CREATE_INFO_EXPERIMENTAL !=
               VK_STRUCTURE_TYPE_DISPLAY_MODE_CREATE_INFO_KHR,
               "Experimental Vex WSI must not collide with Khronos");

static size_t string_length(const char *value) {
    size_t length = 0u;
    while (value[length] != '\0') {
        ++length;
    }
    return length;
}

static void write_text(const char *value) {
    (void)write(STDOUT_FILENO, value, string_length(value));
    (void)vex_syscall1(VEX_SYS_DEBUG_LOG, (uint64_t)(uintptr_t)value);
}

static void write_hex32(const char *label, uint32_t value) {
    static const char digits[] = "0123456789abcdef";
    char text[9];
    for (uint32_t i = 0u; i < 8u; ++i)
        text[i] = digits[(value >> ((7u - i) * 4u)) & 0xfu];
    text[8] = '\0';
    write_text(label);
    write_text(text);
    write_text("\n");
}

typedef struct vk_async_submit_probe {
    VkQueue queue;
    VkSubmitInfo submit;
    VkFence fence;
    uint64_t started_event;
    uint64_t returned_event;
    volatile uint32_t returned;
    VkResult result;
} vk_async_submit_probe_t;

static void vk_async_submit_probe_thread(void *opaque) {
    vk_async_submit_probe_t *probe = (vk_async_submit_probe_t *)opaque;
    (void)vex_event_counter_signal(probe->started_event, 1u);
    probe->result = vkQueueSubmit(probe->queue, 1u, &probe->submit,
                                  probe->fence);
    __atomic_store_n(&probe->returned, 1u, __ATOMIC_RELEASE);
    (void)vex_event_counter_signal(probe->returned_event, 1u);
}

static VkResult vk_test_queue_submit_raw(VkQueue queue, uint32_t submit_count,
        const VkSubmitInfo *submits, VkFence fence) {
    return vkQueueSubmit(queue, submit_count, submits, fence);
}

/* Most pixel/lifetime cases assert post-completion state. Keep those tests
   explicit about the host wait now that submit itself is asynchronous. The
   dedicated queue/WSI cases below call the raw helper to keep work in flight. */
static VkResult vk_test_queue_submit_complete(VkQueue queue,
        uint32_t submit_count, const VkSubmitInfo *submits, VkFence fence) {
    const VkResult submitted = vk_test_queue_submit_raw(queue, submit_count,
                                                         submits, fence);
    if (submitted != VK_SUCCESS) return submitted;
    return vkQueueWaitIdle(queue);
}

#define vkQueueSubmit vk_test_queue_submit_complete

#define SPV_INST(op, words) ((((uint32_t)(words)) << 16u) | (uint32_t)(op))
static const uint32_t triangle_vertex_spv[] = {
    0x07230203u, 0x00010000u, 0u, 26u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 7), 0u, 17u, 0x6e69616du, 0u, 9u, 10u,
    SPV_INST(71, 4), 9u, 11u, 42u,
    SPV_INST(71, 4), 10u, 11u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(21, 4), 2u, 32u, 1u,
    SPV_INST(20, 2), 3u,
    SPV_INST(22, 3), 4u, 32u,
    SPV_INST(23, 4), 5u, 4u, 4u,
    SPV_INST(32, 4), 6u, 1u, 2u,
    SPV_INST(32, 4), 7u, 3u, 5u,
    SPV_INST(33, 3), 8u, 1u,
    SPV_INST(59, 4), 6u, 9u, 1u,
    SPV_INST(59, 4), 7u, 10u, 3u,
    SPV_INST(43, 4), 2u, 11u, 0u,
    SPV_INST(43, 4), 2u, 12u, 1u,
    SPV_INST(43, 4), 2u, 13u, 2u,
    SPV_INST(43, 4), 4u, 14u, 0xbf800000u,
    SPV_INST(43, 4), 4u, 15u, 0u,
    SPV_INST(43, 4), 4u, 16u, 0x3f800000u,
    SPV_INST(54, 5), 1u, 17u, 0u, 8u,
    SPV_INST(248, 2), 18u,
    SPV_INST(61, 4), 2u, 19u, 9u,
    SPV_INST(170, 5), 3u, 20u, 19u, 12u,
    SPV_INST(170, 5), 3u, 21u, 19u, 13u,
    SPV_INST(169, 6), 4u, 22u, 20u, 16u, 14u,
    SPV_INST(169, 6), 4u, 23u, 21u, 15u, 22u,
    SPV_INST(169, 6), 4u, 24u, 21u, 16u, 14u,
    SPV_INST(80, 7), 5u, 25u, 23u, 24u, 15u, 16u,
    SPV_INST(62, 3), 10u, 25u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

/* The same three-vertex position shader with a std140-style column-major
   mat4 in a UBO. It exercises reflected matrix stride, a 64-byte descriptor
   read, matrix composition, and matrix/vector execution in deferred software. */
static const uint32_t matrix_uniform_vertex_spv[] = {
    0x07230203u, 0x00010000u, 0u, 36u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 7), 0u, 17u, 0x6e69616du, 0u, 9u, 10u,
    SPV_INST(71, 4), 9u, 11u, 42u,
    SPV_INST(71, 4), 10u, 11u, 0u,
    SPV_INST(71, 3), 27u, 2u,
    SPV_INST(72, 4), 27u, 0u, 5u,
    SPV_INST(72, 5), 27u, 0u, 7u, 16u,
    SPV_INST(72, 5), 27u, 0u, 35u, 0u,
    SPV_INST(71, 4), 30u, 33u, 0u,
    SPV_INST(71, 4), 30u, 34u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(21, 4), 2u, 32u, 1u,
    SPV_INST(20, 2), 3u,
    SPV_INST(22, 3), 4u, 32u,
    SPV_INST(23, 4), 5u, 4u, 4u,
    SPV_INST(24, 4), 26u, 5u, 4u,
    SPV_INST(30, 3), 27u, 26u,
    SPV_INST(32, 4), 6u, 1u, 2u,
    SPV_INST(32, 4), 7u, 3u, 5u,
    SPV_INST(32, 4), 28u, 2u, 27u,
    SPV_INST(32, 4), 29u, 2u, 26u,
    SPV_INST(33, 3), 8u, 1u,
    SPV_INST(59, 4), 6u, 9u, 1u,
    SPV_INST(59, 4), 7u, 10u, 3u,
    SPV_INST(59, 4), 28u, 30u, 2u,
    SPV_INST(43, 4), 2u, 11u, 0u,
    SPV_INST(43, 4), 2u, 12u, 1u,
    SPV_INST(43, 4), 2u, 13u, 2u,
    SPV_INST(43, 4), 4u, 14u, 0xbf800000u,
    SPV_INST(43, 4), 4u, 15u, 0u,
    SPV_INST(43, 4), 4u, 16u, 0x3f800000u,
    SPV_INST(54, 5), 1u, 17u, 0u, 8u,
    SPV_INST(248, 2), 18u,
    SPV_INST(61, 4), 2u, 19u, 9u,
    SPV_INST(170, 5), 3u, 20u, 19u, 12u,
    SPV_INST(170, 5), 3u, 21u, 19u, 13u,
    SPV_INST(169, 6), 4u, 22u, 20u, 16u, 14u,
    SPV_INST(169, 6), 4u, 23u, 21u, 15u, 22u,
    SPV_INST(169, 6), 4u, 24u, 21u, 16u, 14u,
    SPV_INST(80, 7), 5u, 25u, 23u, 24u, 15u, 16u,
    SPV_INST(65, 5), 29u, 32u, 30u, 11u,
    SPV_INST(61, 4), 26u, 33u, 32u,
    SPV_INST(146, 5), 26u, 35u, 33u, 33u,
    SPV_INST(145, 5), 5u, 34u, 35u, 25u,
    SPV_INST(62, 3), 10u, 34u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

static const uint32_t solid_fragment_spv[] = {
    0x07230203u, 0x00010000u, 0u, 14u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 6), 4u, 10u, 0x6e69616du, 0u, 11u,
    SPV_INST(16, 3), 10u, 7u,
    SPV_INST(71, 4), 11u, 30u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(22, 3), 2u, 32u,
    SPV_INST(23, 4), 3u, 2u, 4u,
    SPV_INST(32, 4), 4u, 3u, 3u,
    SPV_INST(33, 3), 5u, 1u,
    SPV_INST(43, 4), 2u, 6u, 0x3f800000u,
    SPV_INST(43, 4), 2u, 7u, 0u,
    SPV_INST(43, 4), 2u, 8u, 0u,
    SPV_INST(43, 4), 2u, 9u, 0x3f800000u,
    SPV_INST(44, 7), 3u, 12u, 6u, 7u, 8u, 9u,
    SPV_INST(59, 4), 4u, 11u, 3u,
    SPV_INST(54, 5), 1u, 10u, 0u, 5u,
    SPV_INST(248, 2), 13u,
    SPV_INST(62, 3), 11u, 12u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

/* Integer/control-value shader: exercises unsigned add/multiply/subtract,
   shifts, bitwise operations, comparisons, and scalar logical operators in
   the software SPIR-V execution path. The resulting fragment is opaque white. */
static const uint32_t integer_ops_fragment_spv[] = {
    0x07230203u, 0x00010000u, 0u, 62u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 6), 4u, 17u, 0x6e69616du, 0u, 9u,
    SPV_INST(16, 3), 17u, 7u,
    SPV_INST(71, 4), 9u, 30u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(21, 4), 2u, 32u, 0u,
    SPV_INST(20, 2), 3u,
    SPV_INST(22, 3), 4u, 32u,
    SPV_INST(23, 4), 5u, 4u, 4u,
    SPV_INST(32, 4), 6u, 3u, 5u,
    SPV_INST(33, 3), 8u, 1u,
    SPV_INST(21, 4), 45u, 32u, 1u,
    SPV_INST(59, 4), 6u, 9u, 3u,
    SPV_INST(43, 4), 2u, 10u, 6u,
    SPV_INST(43, 4), 2u, 11u, 3u,
    SPV_INST(43, 4), 2u, 12u, 12u,
    SPV_INST(43, 4), 2u, 13u, 1u,
    SPV_INST(43, 4), 2u, 14u, 0u,
    SPV_INST(43, 4), 4u, 15u, 0u,
    SPV_INST(43, 4), 4u, 16u, 0x3f800000u,
    SPV_INST(43, 4), 4u, 35u, 0x41880000u,
    SPV_INST(43, 4), 45u, 46u, 0xfffffff9u,
    SPV_INST(43, 4), 45u, 47u, 3u,
    SPV_INST(43, 4), 45u, 48u, 0xfffffffeu,
    SPV_INST(43, 4), 45u, 49u, 0xffffffffu,
    SPV_INST(43, 4), 45u, 50u, 2u,
    SPV_INST(54, 5), 1u, 17u, 0u, 8u,
    SPV_INST(248, 2), 18u,
    SPV_INST(132, 5), 2u, 19u, 10u, 11u,
    SPV_INST(130, 5), 2u, 20u, 19u, 10u,
    SPV_INST(170, 5), 3u, 21u, 20u, 12u,
    SPV_INST(171, 5), 3u, 22u, 20u, 10u,
    SPV_INST(196, 5), 2u, 23u, 10u, 13u,
    SPV_INST(170, 5), 3u, 24u, 23u, 12u,
    SPV_INST(199, 5), 2u, 25u, 19u, 12u,
    SPV_INST(170, 5), 3u, 26u, 25u, 14u,
    SPV_INST(168, 4), 3u, 27u, 21u,
    SPV_INST(167, 5), 3u, 28u, 22u, 24u,
    SPV_INST(167, 5), 3u, 29u, 28u, 26u,
    SPV_INST(166, 5), 3u, 30u, 27u, 29u,
    SPV_INST(112, 4), 4u, 36u, 19u,
    SPV_INST(186, 5), 3u, 37u, 36u, 35u,
    SPV_INST(167, 5), 3u, 38u, 30u, 37u,
    SPV_INST(134, 5), 2u, 39u, 19u, 11u,
    SPV_INST(170, 5), 3u, 40u, 39u, 10u,
    SPV_INST(137, 5), 2u, 41u, 19u, 10u,
    SPV_INST(170, 5), 3u, 42u, 41u, 14u,
    SPV_INST(167, 5), 3u, 43u, 38u, 40u,
    SPV_INST(167, 5), 3u, 44u, 43u, 42u,
    SPV_INST(135, 5), 45u, 51u, 46u, 47u,
    SPV_INST(170, 5), 3u, 52u, 51u, 48u,
    SPV_INST(138, 5), 45u, 53u, 46u, 47u,
    SPV_INST(170, 5), 3u, 54u, 53u, 49u,
    SPV_INST(139, 5), 45u, 55u, 46u, 47u,
    SPV_INST(170, 5), 3u, 56u, 55u, 50u,
    SPV_INST(177, 5), 3u, 57u, 46u, 47u,
    SPV_INST(167, 5), 3u, 58u, 44u, 52u,
    SPV_INST(167, 5), 3u, 59u, 58u, 54u,
    SPV_INST(167, 5), 3u, 60u, 59u, 56u,
    SPV_INST(167, 5), 3u, 61u, 60u, 57u,
    SPV_INST(169, 6), 4u, 31u, 61u, 16u, 15u,
    SPV_INST(169, 6), 4u, 32u, 21u, 16u, 15u,
    SPV_INST(169, 6), 4u, 33u, 24u, 16u, 15u,
    SPV_INST(80, 7), 5u, 34u, 31u, 32u, 33u, 16u,
    SPV_INST(62, 3), 9u, 34u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

static const uint32_t triangle_fragment_spv[] = {
    0x07230203u, 0x00010000u, 0u, 17u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 6), 4u, 13u, 0x6e69616du, 0u, 9u,
    SPV_INST(16, 3), 13u, 7u,
    SPV_INST(71, 4), 9u, 30u, 0u,
    SPV_INST(71, 3), 6u, 2u,
    SPV_INST(72, 5), 6u, 0u, 35u, 0u,
    SPV_INST(71, 4), 10u, 33u, 0u,
    SPV_INST(71, 4), 10u, 34u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(22, 3), 2u, 32u,
    SPV_INST(23, 4), 3u, 2u, 4u,
    SPV_INST(32, 4), 4u, 3u, 3u,
    SPV_INST(33, 3), 5u, 1u,
    SPV_INST(30, 3), 6u, 3u,
    SPV_INST(32, 4), 7u, 2u, 6u,
    SPV_INST(32, 4), 8u, 2u, 3u,
    SPV_INST(21, 4), 11u, 32u, 0u,
    SPV_INST(59, 4), 4u, 9u, 3u,
    SPV_INST(59, 4), 7u, 10u, 2u,
    SPV_INST(43, 4), 11u, 12u, 0u,
    SPV_INST(54, 5), 1u, 13u, 0u, 5u,
    SPV_INST(248, 2), 14u,
    SPV_INST(65, 5), 8u, 15u, 10u, 12u,
    SPV_INST(61, 4), 3u, 16u, 15u,
    SPV_INST(62, 3), 9u, 16u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

static const uint32_t push_constant_fragment_spv[] = {
    0x07230203u, 0x00010000u, 0u, 23u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 6), 4u, 13u, 0x6e69616du, 0u, 9u,
    SPV_INST(16, 3), 13u, 7u,
    SPV_INST(71, 4), 9u, 30u, 0u,
    SPV_INST(71, 3), 6u, 2u,
    SPV_INST(72, 5), 6u, 0u, 35u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(22, 3), 2u, 32u,
    SPV_INST(23, 4), 3u, 2u, 4u,
    SPV_INST(32, 4), 4u, 3u, 3u,
    SPV_INST(33, 3), 5u, 1u,
    SPV_INST(30, 3), 6u, 3u,
    SPV_INST(32, 4), 7u, 9u, 6u,
    SPV_INST(32, 4), 8u, 9u, 3u,
    SPV_INST(21, 4), 11u, 32u, 0u,
    SPV_INST(59, 4), 4u, 9u, 3u,
    SPV_INST(59, 4), 7u, 10u, 9u,
    SPV_INST(43, 4), 11u, 12u, 0u,
    SPV_INST(54, 5), 1u, 13u, 0u, 5u,
    SPV_INST(248, 2), 14u,
    SPV_INST(65, 5), 8u, 15u, 10u, 12u,
    SPV_INST(61, 4), 3u, 16u, 15u,
    SPV_INST(81, 5), 2u, 17u, 16u, 0u,
    SPV_INST(81, 5), 2u, 18u, 16u, 1u,
    SPV_INST(81, 5), 2u, 19u, 16u, 2u,
    SPV_INST(81, 5), 2u, 20u, 16u, 3u,
    SPV_INST(80, 7), 3u, 21u, 17u, 18u, 19u, 20u,
    SPV_INST(79, 9), 3u, 22u, 21u, 21u, 0u, 1u, 2u, 3u,
    SPV_INST(62, 3), 9u, 22u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

/* Exercises vector arithmetic, a structured conditional/phi merge, and the
   GLSL.std.450 FClamp instruction through the deferred fragment path. */
static const uint32_t vector_math_fragment_spv[] = {
    0x07230203u, 0x00010000u, 0u, 46u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(11, 6), 37u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0u,
    SPV_INST(15, 6), 4u, 13u, 0x6e69616du, 0u, 9u,
    SPV_INST(16, 3), 13u, 7u,
    SPV_INST(71, 4), 9u, 30u, 0u,
    SPV_INST(71, 3), 6u, 2u,
    SPV_INST(72, 5), 6u, 0u, 35u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(22, 3), 2u, 32u,
    SPV_INST(23, 4), 3u, 2u, 4u,
    SPV_INST(32, 4), 4u, 3u, 3u,
    SPV_INST(33, 3), 5u, 1u,
    SPV_INST(30, 3), 6u, 3u,
    SPV_INST(32, 4), 7u, 9u, 6u,
    SPV_INST(32, 4), 8u, 9u, 3u,
    SPV_INST(21, 4), 11u, 32u, 0u,
    SPV_INST(59, 4), 4u, 9u, 3u,
    SPV_INST(59, 4), 7u, 10u, 9u,
    SPV_INST(43, 4), 11u, 12u, 0u,
    SPV_INST(43, 4), 2u, 23u, 0x3e800000u,
    SPV_INST(43, 4), 2u, 24u, 0x3f800000u,
    SPV_INST(44, 7), 3u, 25u, 24u, 24u, 24u, 24u,
    SPV_INST(20, 2), 29u,
    SPV_INST(43, 4), 2u, 30u, 0x3f400000u,
    SPV_INST(43, 4), 2u, 31u, 0u,
    SPV_INST(43, 4), 2u, 38u, 0x3f000000u,
    SPV_INST(43, 4), 2u, 40u, 0xbe800000u,
    SPV_INST(54, 5), 1u, 13u, 0u, 5u,
    SPV_INST(248, 2), 14u,
    SPV_INST(65, 5), 8u, 15u, 10u, 12u,
    SPV_INST(61, 4), 3u, 16u, 15u,
    SPV_INST(81, 5), 2u, 17u, 16u, 0u,
    SPV_INST(81, 5), 2u, 18u, 16u, 1u,
    SPV_INST(81, 5), 2u, 19u, 16u, 2u,
    SPV_INST(81, 5), 2u, 20u, 16u, 3u,
    SPV_INST(80, 7), 3u, 21u, 17u, 18u, 19u, 20u,
    SPV_INST(79, 9), 3u, 22u, 21u, 21u, 0u, 1u, 2u, 3u,
    SPV_INST(142, 5), 3u, 26u, 22u, 23u,
    SPV_INST(148, 5), 2u, 27u, 26u, 25u,
    SPV_INST(184, 5), 29u, 32u, 27u, 30u,
    SPV_INST(247, 3), 35u, 0u,
    SPV_INST(250, 4), 32u, 33u, 34u,
    SPV_INST(248, 2), 33u,
    SPV_INST(249, 2), 35u,
    SPV_INST(248, 2), 34u,
    SPV_INST(249, 2), 35u,
    SPV_INST(248, 2), 35u,
    SPV_INST(245, 7), 2u, 36u, 27u, 33u, 31u, 34u,
    SPV_INST(12, 8), 2u, 39u, 37u, 43u, 36u, 31u, 38u,
    SPV_INST(12, 6), 2u, 41u, 37u, 4u, 40u,
    SPV_INST(12, 7), 2u, 42u, 37u, 37u, 41u, 39u,
    SPV_INST(12, 7), 2u, 43u, 37u, 40u, 41u, 39u,
    SPV_INST(12, 8), 2u, 44u, 37u, 46u, 41u, 43u, 38u,
    SPV_INST(12, 7), 2u, 45u, 37u, 37u, 39u, 44u,
    SPV_INST(80, 7), 3u, 28u, 45u, 45u, 45u, 45u,
    SPV_INST(62, 3), 9u, 28u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

static const uint32_t varying_vertex_spv[] = {
    0x07230203u, 0x00010000u, 0u, 32u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 9), 0u, 20u, 0x6e69616du, 0u, 8u, 9u, 10u, 31u,
    SPV_INST(71, 4), 8u, 11u, 0u,
    SPV_INST(71, 4), 9u, 30u, 0u,
    SPV_INST(71, 4), 31u, 30u, 1u,
    SPV_INST(71, 4), 10u, 11u, 42u,
    SPV_INST(19, 2), 1u,
    SPV_INST(21, 4), 2u, 32u, 0u,
    SPV_INST(22, 3), 3u, 32u,
    SPV_INST(23, 4), 4u, 3u, 4u,
    SPV_INST(32, 4), 5u, 1u, 2u,
    SPV_INST(32, 4), 6u, 3u, 4u,
    SPV_INST(33, 3), 7u, 1u,
    SPV_INST(59, 4), 6u, 8u, 3u,
    SPV_INST(59, 4), 6u, 9u, 3u,
    SPV_INST(59, 4), 5u, 10u, 1u,
    SPV_INST(59, 4), 6u, 31u, 3u,
    SPV_INST(43, 4), 2u, 11u, 0u,
    SPV_INST(43, 4), 2u, 12u, 1u,
    SPV_INST(43, 4), 3u, 13u, 0xbf800000u,
    SPV_INST(43, 4), 3u, 14u, 0u,
    SPV_INST(43, 4), 3u, 15u, 0x3f800000u,
    SPV_INST(44, 7), 4u, 16u, 15u, 14u, 14u, 15u,
    SPV_INST(44, 7), 4u, 17u, 14u, 15u, 14u, 15u,
    SPV_INST(44, 7), 4u, 18u, 14u, 14u, 15u, 15u,
    SPV_INST(20, 2), 19u,
    SPV_INST(54, 5), 1u, 20u, 0u, 7u,
    SPV_INST(248, 2), 21u,
    SPV_INST(61, 4), 2u, 22u, 10u,
    SPV_INST(170, 5), 19u, 23u, 22u, 11u,
    SPV_INST(170, 5), 19u, 24u, 22u, 12u,
    SPV_INST(169, 6), 3u, 25u, 23u, 15u, 13u,
    SPV_INST(169, 6), 3u, 26u, 24u, 14u, 25u,
    SPV_INST(169, 6), 3u, 27u, 24u, 15u, 13u,
    SPV_INST(80, 7), 4u, 28u, 26u, 27u, 14u, 15u,
    SPV_INST(62, 3), 8u, 28u,
    SPV_INST(169, 6), 4u, 29u, 23u, 16u, 18u,
    SPV_INST(169, 6), 4u, 30u, 24u, 17u, 29u,
    SPV_INST(62, 3), 9u, 30u,
    SPV_INST(62, 3), 31u, 30u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

static const uint32_t vertex_attribute_spv[] = {
    0x07230203u, 0x00010000u, 0u, 12u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 7), 0u, 7u, 0x6e69616du, 0u, 9u, 10u,
    SPV_INST(71, 4), 9u, 30u, 0u,
    SPV_INST(71, 4), 10u, 11u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(22, 3), 2u, 32u,
    SPV_INST(23, 4), 3u, 2u, 4u,
    SPV_INST(32, 4), 4u, 1u, 3u,
    SPV_INST(32, 4), 5u, 3u, 3u,
    SPV_INST(33, 3), 6u, 1u,
    SPV_INST(59, 4), 4u, 9u, 1u,
    SPV_INST(59, 4), 5u, 10u, 3u,
    SPV_INST(54, 5), 1u, 7u, 0u, 6u,
    SPV_INST(248, 2), 8u,
    SPV_INST(61, 4), 3u, 11u, 9u,
    SPV_INST(62, 3), 10u, 11u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

static const uint32_t multi_attribute_vertex_spv[] = {
    0x07230203u, 0x00010000u, 0u, 13u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 8), 0u, 7u, 0x6e69616du, 0u, 9u, 10u, 12u,
    SPV_INST(71, 4), 9u, 30u, 0u,
    SPV_INST(71, 4), 10u, 11u, 0u,
    SPV_INST(71, 4), 12u, 30u, 1u,
    SPV_INST(19, 2), 1u,
    SPV_INST(22, 3), 2u, 32u,
    SPV_INST(23, 4), 3u, 2u, 4u,
    SPV_INST(32, 4), 4u, 1u, 3u,
    SPV_INST(32, 4), 5u, 3u, 3u,
    SPV_INST(33, 3), 6u, 1u,
    SPV_INST(59, 4), 4u, 9u, 1u,
    SPV_INST(59, 4), 5u, 10u, 3u,
    SPV_INST(59, 4), 4u, 12u, 1u,
    SPV_INST(54, 5), 1u, 7u, 0u, 6u,
    SPV_INST(248, 2), 8u,
    SPV_INST(61, 4), 3u, 11u, 12u,
    SPV_INST(62, 3), 10u, 11u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

static const uint32_t vec3_attribute_vertex_spv[] = {
    0x07230203u, 0x00010000u, 0u, 18u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 7), 0u, 8u, 0x6e69616du, 0u, 9u, 10u,
    SPV_INST(71, 4), 9u, 30u, 0u,
    SPV_INST(71, 4), 10u, 11u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(22, 3), 2u, 32u,
    SPV_INST(23, 4), 3u, 2u, 3u,
    SPV_INST(23, 4), 4u, 2u, 4u,
    SPV_INST(32, 4), 5u, 1u, 3u,
    SPV_INST(32, 4), 6u, 3u, 4u,
    SPV_INST(33, 3), 7u, 1u,
    SPV_INST(59, 4), 5u, 9u, 1u,
    SPV_INST(59, 4), 6u, 10u, 3u,
    SPV_INST(43, 4), 2u, 16u, 0x3f800000u,
    SPV_INST(54, 5), 1u, 8u, 0u, 7u,
    SPV_INST(248, 2), 11u,
    SPV_INST(61, 4), 3u, 12u, 9u,
    SPV_INST(81, 5), 2u, 13u, 12u, 0u,
    SPV_INST(81, 5), 2u, 14u, 12u, 1u,
    SPV_INST(81, 5), 2u, 15u, 12u, 2u,
    SPV_INST(80, 7), 4u, 17u, 13u, 14u, 15u, 16u,
    SPV_INST(62, 3), 10u, 17u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

static const uint32_t varying_fragment_spv[] = {
    0x07230203u, 0x00010000u, 0u, 13u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 8), 4u, 9u, 0x6e69616du, 0u, 7u, 8u, 12u,
    SPV_INST(16, 3), 9u, 7u,
    SPV_INST(71, 4), 7u, 30u, 0u,
    SPV_INST(71, 4), 8u, 30u, 0u,
    SPV_INST(71, 4), 12u, 30u, 1u,
    SPV_INST(19, 2), 1u,
    SPV_INST(22, 3), 2u, 32u,
    SPV_INST(23, 4), 3u, 2u, 4u,
    SPV_INST(32, 4), 4u, 1u, 3u,
    SPV_INST(32, 4), 5u, 3u, 3u,
    SPV_INST(33, 3), 6u, 1u,
    SPV_INST(59, 4), 4u, 7u, 1u,
    SPV_INST(59, 4), 5u, 8u, 3u,
    SPV_INST(59, 4), 4u, 12u, 1u,
    SPV_INST(54, 5), 1u, 9u, 0u, 6u,
    SPV_INST(248, 2), 10u,
    SPV_INST(61, 4), 3u, 11u, 12u,
    SPV_INST(62, 3), 8u, 11u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

static const uint32_t sampled_image_fragment_spv[] = {
    0x07230203u, 0x00010000u, 0u, 19u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 6), 4u, 15u, 0x6e69616du, 0u, 11u,
    SPV_INST(16, 3), 15u, 7u,
    SPV_INST(71, 4), 10u, 34u, 0u,
    SPV_INST(71, 4), 10u, 33u, 0u,
    SPV_INST(71, 4), 11u, 30u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(22, 3), 2u, 32u,
    SPV_INST(23, 4), 3u, 2u, 2u,
    SPV_INST(23, 4), 4u, 2u, 4u,
    SPV_INST(25, 9), 5u, 2u, 1u, 0u, 0u, 0u, 1u, 0u,
    SPV_INST(27, 3), 6u, 5u,
    SPV_INST(32, 4), 7u, 0u, 6u,
    SPV_INST(32, 4), 8u, 3u, 4u,
    SPV_INST(33, 3), 9u, 1u,
    SPV_INST(59, 4), 7u, 10u, 0u,
    SPV_INST(59, 4), 8u, 11u, 3u,
    SPV_INST(43, 4), 2u, 12u, 0x3f000000u,
    SPV_INST(44, 5), 3u, 13u, 12u, 12u,
    SPV_INST(54, 5), 1u, 15u, 0u, 9u,
    SPV_INST(248, 2), 16u,
    SPV_INST(61, 4), 6u, 17u, 10u,
    SPV_INST(87, 5), 4u, 18u, 17u, 13u,
    SPV_INST(62, 3), 11u, 18u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

/* Reads one member from each of two multi-vec4 UBO blocks, exercising block
   offsets, descriptor reflection, layout matching, and deferred reads. */
static const uint32_t two_uniform_fragment_spv[] = {
    0x07230203u, 0x00010000u, 0u, 22u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 6), 4u, 15u, 0x6e69616du, 0u, 9u,
    SPV_INST(16, 3), 15u, 7u,
    SPV_INST(71, 4), 9u, 30u, 0u,
    SPV_INST(71, 3), 6u, 2u,
    SPV_INST(72, 5), 6u, 0u, 35u, 0u,
    SPV_INST(72, 5), 6u, 1u, 35u, 16u,
    SPV_INST(71, 4), 10u, 33u, 0u,
    SPV_INST(71, 4), 10u, 34u, 0u,
    SPV_INST(71, 4), 11u, 33u, 1u,
    SPV_INST(71, 4), 11u, 34u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(22, 3), 2u, 32u,
    SPV_INST(23, 4), 3u, 2u, 4u,
    SPV_INST(32, 4), 4u, 3u, 3u,
    SPV_INST(33, 3), 5u, 1u,
    SPV_INST(30, 4), 6u, 3u, 3u,
    SPV_INST(32, 4), 7u, 2u, 6u,
    SPV_INST(32, 4), 8u, 2u, 3u,
    SPV_INST(21, 4), 12u, 32u, 0u,
    SPV_INST(59, 4), 4u, 9u, 3u,
    SPV_INST(59, 4), 7u, 10u, 2u,
    SPV_INST(59, 4), 7u, 11u, 2u,
    SPV_INST(43, 4), 12u, 13u, 0u,
    SPV_INST(43, 4), 12u, 14u, 1u,
    SPV_INST(54, 5), 1u, 15u, 0u, 5u,
    SPV_INST(248, 2), 16u,
    SPV_INST(65, 5), 8u, 17u, 10u, 13u,
    SPV_INST(61, 4), 3u, 18u, 17u,
    SPV_INST(65, 5), 8u, 19u, 11u, 14u,
    SPV_INST(61, 4), 3u, 20u, 19u,
    SPV_INST(129, 5), 3u, 21u, 18u, 20u,
    SPV_INST(62, 3), 9u, 21u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

/* Fragment color loaded from a std140 vec3 UBO member. The final block size is
   rounded to 16 bytes even though only 12 bytes carry vector components. */
static const uint32_t uniform_vec3_fragment_spv[] = {
    0x07230203u, 0x00010000u, 0u, 25u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 6), 4u, 17u, 0x6e69616du, 0u, 11u,
    SPV_INST(16, 3), 17u, 7u,
    SPV_INST(71, 4), 11u, 30u, 0u,
    SPV_INST(71, 3), 6u, 2u,
    SPV_INST(72, 5), 6u, 0u, 35u, 0u,
    SPV_INST(71, 4), 12u, 33u, 1u,
    SPV_INST(71, 4), 12u, 34u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(22, 3), 2u, 32u,
    SPV_INST(23, 4), 3u, 2u, 4u,
    SPV_INST(23, 4), 4u, 2u, 3u,
    SPV_INST(21, 4), 5u, 32u, 0u,
    SPV_INST(30, 3), 6u, 4u,
    SPV_INST(32, 4), 7u, 3u, 3u,
    SPV_INST(32, 4), 8u, 2u, 6u,
    SPV_INST(32, 4), 9u, 2u, 4u,
    SPV_INST(33, 3), 10u, 1u,
    SPV_INST(59, 4), 7u, 11u, 3u,
    SPV_INST(59, 4), 8u, 12u, 2u,
    SPV_INST(43, 4), 5u, 13u, 0u,
    SPV_INST(43, 4), 2u, 14u, 0x3f800000u,
    SPV_INST(54, 5), 1u, 17u, 0u, 10u,
    SPV_INST(248, 2), 18u,
    SPV_INST(65, 5), 9u, 19u, 12u, 13u,
    SPV_INST(61, 4), 4u, 20u, 19u,
    SPV_INST(81, 5), 2u, 21u, 20u, 0u,
    SPV_INST(81, 5), 2u, 22u, 20u, 1u,
    SPV_INST(81, 5), 2u, 23u, 20u, 2u,
    SPV_INST(80, 7), 3u, 24u, 21u, 22u, 23u, 14u,
    SPV_INST(62, 3), 11u, 24u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

/* Three-iteration structured loop: accumulate a quarter in every output lane. */
static const uint32_t loop_fragment_spv[] = {
    0x07230203u, 0x00010000u, 0u, 28u, 0u,
    SPV_INST(17, 2), 1u,
    SPV_INST(14, 3), 0u, 1u,
    SPV_INST(15, 6), 4u, 17u, 0x6e69616du, 0u, 8u,
    SPV_INST(16, 3), 17u, 7u,
    SPV_INST(71, 4), 8u, 30u, 0u,
    SPV_INST(19, 2), 1u,
    SPV_INST(20, 2), 2u,
    SPV_INST(21, 4), 3u, 32u, 1u,
    SPV_INST(22, 3), 4u, 32u,
    SPV_INST(23, 4), 5u, 4u, 4u,
    SPV_INST(32, 4), 6u, 3u, 5u,
    SPV_INST(33, 3), 7u, 1u,
    SPV_INST(43, 4), 4u, 9u, 0x3e800000u,
    SPV_INST(43, 4), 3u, 11u, 0u,
    SPV_INST(43, 4), 3u, 12u, 1u,
    SPV_INST(43, 4), 3u, 13u, 3u,
    SPV_INST(43, 4), 4u, 15u, 0u,
    SPV_INST(44, 7), 5u, 14u, 15u, 15u, 15u, 15u,
    SPV_INST(44, 7), 5u, 16u, 9u, 9u, 9u, 9u,
    SPV_INST(59, 4), 6u, 8u, 3u,
    SPV_INST(54, 5), 1u, 17u, 0u, 7u,
    SPV_INST(248, 2), 18u,
    SPV_INST(249, 2), 19u,
    SPV_INST(248, 2), 19u,
    SPV_INST(245, 7), 3u, 20u, 11u, 18u, 26u, 25u,
    SPV_INST(245, 7), 5u, 21u, 14u, 18u, 24u, 25u,
    SPV_INST(246, 4), 27u, 25u, 0u,
    SPV_INST(177, 5), 2u, 22u, 20u, 13u,
    SPV_INST(250, 4), 22u, 23u, 27u,
    SPV_INST(248, 2), 23u,
    SPV_INST(129, 5), 5u, 24u, 21u, 16u,
    SPV_INST(249, 2), 25u,
    SPV_INST(248, 2), 25u,
    SPV_INST(128, 5), 3u, 26u, 20u, 12u,
    SPV_INST(249, 2), 19u,
    SPV_INST(248, 2), 27u,
    SPV_INST(62, 3), 8u, 21u,
    SPV_INST(253, 1),
    SPV_INST(56, 1)
};

#undef SPV_INST

#include "vk_subresources_test.h"
#include "vk_descriptor_arrays_test.h"
#include "vk_stencil_test.h"
#include "vk_msaa_test.h"
#include "vk_api_contract_test.h"
#include "vk_coverage_test.h"
#include "vk_syscall_domain_test.h"
#include "vk_compute_test.h"
#include "vk_texel_test.h"
#include "vk_compute_graphics_test.h"

int main(void) {
    if (!vk_syscall_domain_smoke()) {
        write_text("vk_test: syscall domain contract FAILED\n");
        return 2;
    }
    if (!vk_syscall_regression_smoke()) return 2;
    if (!vk_native_heap_extent_smoke()) {
        write_text("vk_test: native heap extent reuse FAILED\n");
        return 2;
    }
    const char *instance_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_VEX_SURFACE_EXTENSION_NAME
    };
    const char *device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkInstanceCreateInfo instance_info = {0};
    VkDeviceQueueCreateInfo queue_info = {0};
    VkDeviceCreateInfo device_info = {0};
    VkBufferCreateInfo buffer_info = {0};
    VkCommandPoolCreateInfo pool_info = {0};
    VkCommandBufferAllocateInfo cmd_alloc = {0};
    VkCommandBufferBeginInfo cmd_begin = {0};
    VkSubmitInfo submit = {0};
    VkBufferCopy copy_region = {0};
    VkBufferImageCopy image_copy = {0};
    VkImageCopy image_region = {0};
    VkImageMemoryBarrier copy_barriers[2] = {{0}, {0}};
    VkMemoryAllocateInfo memory_info = {0};
    VkMemoryAllocateInfo imported_memory_info = {0};
    VkImageCreateInfo image_info = {0};
    VkImageSubresourceRange image_range = {0};
    VkImageMemoryBarrier image_barrier = {0};
    VkImageMemoryBarrier texture_transition = {0};
    VkClearColorValue clear_color = {0};
    VkFenceCreateInfo fence_info = {0};
    VkShaderModuleCreateInfo shader_info = {0};
    VkPipelineShaderStageCreateInfo graphics_stages[2] = {{0}, {0}};
    VkPipelineVertexInputStateCreateInfo vertex_input = {0};
    VkVertexInputBindingDescription vertex_binding = {0};
    VkVertexInputAttributeDescription vertex_attribute = {0};
    VkVertexInputAttributeDescription multi_attribute_descriptions[2] = {{0}, {0}};
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {0};
    VkPipelineViewportStateCreateInfo viewport_state = {0};
    VkViewport graphics_viewport = {0};
    VkRect2D graphics_scissor = {0};
    VkPipelineRasterizationStateCreateInfo rasterization = {0};
    VkPipelineMultisampleStateCreateInfo multisample = {0};
    VkPipelineColorBlendAttachmentState color_attachment_state = {0};
    VkPipelineColorBlendStateCreateInfo color_blend = {0};
    VkPipelineLayoutCreateInfo graphics_layout_info = {0};
    VkPipelineLayoutCreateInfo texture_pipeline_layout_info = {0};
    VkGraphicsPipelineCreateInfo graphics_pipeline_info = {0};
    VkPipelineDynamicStateCreateInfo dynamic_state_info = {0};
    VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT,
                                        VK_DYNAMIC_STATE_SCISSOR};
    VkGraphicsPipelineCreateInfo pipeline_info = {0};
    VkSemaphoreCreateInfo semaphore_info = {0};
    VkRenderPassCreateInfo render_pass_info = {0};
    VkAttachmentDescription render_attachment = {0};
    VkAttachmentReference render_color_ref = {0};
    VkSubpassDescription render_subpass = {0};
    VkFramebufferCreateInfo framebuffer_info = {0};
    VkImageViewCreateInfo image_view_info = {0};
    VkImageViewCreateInfo texture_view_info = {0};
    VkRenderPassBeginInfo render_begin = {0};
    VkClearValue render_clear = {0};
    VkDescriptorSetLayoutBinding descriptor_binding = {0};
    VkDescriptorSetLayoutBinding texture_binding = {0};
    VkDescriptorSetLayoutCreateInfo descriptor_layout_info = {0};
    VkDescriptorPoolSize descriptor_pool_size = {0};
    VkDescriptorPoolSize texture_pool_size = {0};
    VkDescriptorPoolCreateInfo descriptor_pool_info = {0};
    VkDescriptorSetAllocateInfo descriptor_alloc_info = {0};
    VkDescriptorImageInfo texture_descriptor_info = {0};
    VkSamplerCreateInfo texture_sampler_info = {0};
    VkDescriptorBufferInfo descriptor_buffer_info = {0};
    VkWriteDescriptorSet descriptor_write = {0};
    VkPushConstantRange push_range = {0};
    VkPipelineLayoutCreateInfo pipeline_layout_info = {0};
    VkVexSurfaceCreateInfoVEX surface_info = {0};
    VkVexMemoryImportInfoVEX memory_import_info = {0};
    VkExtensionProperties instance_extension_properties[2] = {{0}, {0}};
    VkExtensionProperties device_extension_properties[1] = {{0}};
    VkSwapchainCreateInfoKHR swapchain_create_info = {0};
    VkPresentInfoKHR present_info = {0};
    VkImageMemoryBarrier wsi_barrier = {0};
    VkFenceCreateInfo wsi_fence_info = {0};
    VkSurfaceCapabilitiesKHR surface_capabilities = {0};
    VkSurfaceFormatKHR surface_format = {0};
    VkBool32 supports_present = VK_FALSE;
    vex_gpu_buffer_create_req_t wsi_buffer = {0};
    vex_gpu_surface_create_req_t wsi_vex_surface = {0};
    vex_surface_query_req_t wsi_surface_query = {0};
    vex_surface_map_req_t wsi_read_map = {0};
    vex_surface_unmap_req_t wsi_read_unmap = {0};
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physical_properties = {0};
    VkPhysicalDeviceFeatures disabled_features = {0};
    VkPhysicalDeviceFeatures unsupported_features = {0};
    VkFormatProperties format_properties = {0};
    VkFormatProperties rgba_format_properties = {0};
    VkFormatProperties vertex_format_properties = {0};
    VkFormatProperties depth_format_properties = {0};
    VkImageFormatProperties depth_image_properties = {0};
    VkDevice device = VK_NULL_HANDLE;
    VkDevice feature_probe = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkBuffer dst_buffer = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceMemory dst_memory = VK_NULL_HANDLE;
    VkDeviceMemory index_memory = VK_NULL_HANDLE;
    VkDeviceMemory image_memory = VK_NULL_HANDLE;
    VkDeviceMemory copied_image_memory = VK_NULL_HANDLE;
    VkDeviceMemory imported_memory = VK_NULL_HANDLE;
    VkDeviceMemory exported_memory = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImage copied_image = VK_NULL_HANDLE;
    VkImage wsi_image = VK_NULL_HANDLE;
    VkImage wsi_images[4] = {VK_NULL_HANDLE, VK_NULL_HANDLE,
        VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkFence wsi_fence = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    VkShaderModule integer_ops_fragment_shader = VK_NULL_HANDLE;
    VkShaderModule push_fragment_shader = VK_NULL_HANDLE;
    VkShaderModule vector_math_fragment_shader = VK_NULL_HANDLE;
    VkShaderModule varying_vertex_shader = VK_NULL_HANDLE;
    VkShaderModule varying_fragment_shader = VK_NULL_HANDLE;
    VkShaderModule vertex_attribute_shader = VK_NULL_HANDLE;
    VkShaderModule multi_attribute_shader = VK_NULL_HANDLE;
    VkShaderModule vec3_attribute_shader = VK_NULL_HANDLE;
    VkShaderModule texture_vertex_shader = VK_NULL_HANDLE;
    VkShaderModule texture_fragment_shader = VK_NULL_HANDLE;
    VkShaderModule two_uniform_shader = VK_NULL_HANDLE;
    VkShaderModule two_uniform_vertex_shader = VK_NULL_HANDLE;
    VkShaderModule loop_fragment_shader = VK_NULL_HANDLE;
    VkShaderModule loop_vertex_shader = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline graphics_pipeline = VK_NULL_HANDLE;
    VkPipeline integer_ops_pipeline = VK_NULL_HANDLE;
    VkPipeline push_pipeline = VK_NULL_HANDLE;
    VkPipeline vector_math_pipeline = VK_NULL_HANDLE;
    VkPipeline alpha_pipeline = VK_NULL_HANDLE;
    VkPipeline varying_pipeline = VK_NULL_HANDLE;
    VkPipeline vertex_attribute_pipeline = VK_NULL_HANDLE;
    VkPipeline multi_attribute_pipeline = VK_NULL_HANDLE;
    VkPipeline normalized_attribute_pipeline = VK_NULL_HANDLE;
    VkPipeline vec3_attribute_pipeline = VK_NULL_HANDLE;
    VkPipeline strip_pipeline = VK_NULL_HANDLE;
    VkPipeline fan_pipeline = VK_NULL_HANDLE;
    VkPipeline point_pipeline = VK_NULL_HANDLE;
    VkPipeline line_list_pipeline = VK_NULL_HANDLE;
    VkPipeline line_strip_pipeline = VK_NULL_HANDLE;
    VkPipeline dynamic_state_pipeline = VK_NULL_HANDLE;
    VkPipeline texture_pipeline = VK_NULL_HANDLE;
    VkPipeline two_uniform_pipeline = VK_NULL_HANDLE;
    VkPipeline loop_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout graphics_layout = VK_NULL_HANDLE;
    VkPipelineLayout texture_pipeline_layout = VK_NULL_HANDLE;
    VkPipelineLayout two_uniform_pipeline_layout = VK_NULL_HANDLE;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    VkSemaphore wsi_render_semaphore = VK_NULL_HANDLE;
    VkSemaphore async_wait_semaphore = VK_NULL_HANDLE;
    VkSemaphore async_chain_semaphore = VK_NULL_HANDLE;
    VkSemaphore async_done_semaphore = VK_NULL_HANDLE;
    VkFence async_wait_fence = VK_NULL_HANDLE;
    uint64_t async_started_event = 0u;
    uint64_t async_returned_event = 0u;
    uint32_t async_probe_tid = 0u;
    uint32_t async_acquired = 0u;
    vk_async_submit_probe_t async_probe = {0};
    uint64_t timeline_queue = 0u;
    uint64_t timeline_serial = 0u;
    uint64_t timeline_serial_second = 0u;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkImageView image_view = VK_NULL_HANDLE;
    VkImageView texture_view = VK_NULL_HANDLE;
    VkSampler texture_sampler = VK_NULL_HANDLE;
    VkSampler linear_texture_sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout texture_descriptor_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout two_uniform_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorPool texture_descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorPool two_uniform_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSet uninitialized_descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSet texture_descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSet two_uniform_set = VK_NULL_HANDLE;
    VkDescriptorSet shader_sets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSetLayout shader_set_layouts[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkSwapchainKHR replacement_swapchain = VK_NULL_HANDLE;
    VkSwapchainCreateInfoKHR swapchain_info = {0};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkMemoryRequirements requirements = {0};
    uint32_t malformed_spirv[6] = {0x07230203u, 0x00010000u, 0u, 1u, 0u, 0u};
    uint32_t count = 1u;
    uint32_t wsi_image_count = 4u;
    uint32_t wsi_image_index = UINT32_MAX;
    uint32_t wsi_first_image_index = UINT32_MAX;
    uint32_t wsi_second_image_index = UINT32_MAX;
    uint32_t pixels_before_invalid_descriptor[4];
    float priority = 1.0f;
    float push_color[4] = {1.0f, 0.25f, 0.0f, 1.0f};
    const float vertex_positions[12] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
         0.0f,  1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 0.0f, 1.0f
    };
    void *mapped = NULL;
    void *dst_mapped = NULL;
    void *index_mapped = NULL;
    void *image_mapped = NULL;
    void *copied_image_mapped = NULL;
    void *exported_memory_mapped = NULL;
    void *imported_memory_mapped = NULL;
    int core_ok = 0;

    write_text("vk_test: begin\n");
    count = 1u;
    if (vkEnumerateInstanceExtensionProperties(NULL, &count,
            instance_extension_properties) != VK_INCOMPLETE || count != 1u ||
        strcmp(instance_extension_properties[0].extensionName,
               VK_KHR_SURFACE_EXTENSION_NAME) != 0) {
        write_text("vk_test: instance extension enumeration partial result invalid\n");
        goto done;
    }
    count = 2u;
    if (vkEnumerateInstanceExtensionProperties(NULL, &count,
            instance_extension_properties) != VK_SUCCESS || count != 2u ||
        strcmp(instance_extension_properties[1].extensionName,
               VK_VEX_SURFACE_EXTENSION_NAME) != 0 ||
        vkGetInstanceProcAddr(VK_NULL_HANDLE,
            "vkEnumerateInstanceExtensionProperties") == NULL ||
        vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumeratePhysicalDevices") != NULL) {
        write_text("vk_test: instance extension enumeration invalid\n");
        goto done;
    }
    instance_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    if (vkCreateInstance(&instance_info, NULL, &instance) == VK_SUCCESS ||
        instance != VK_NULL_HANDLE) {
        write_text("vk_test: invalid instance accepted\n");
        goto done;
    }
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.enabledExtensionCount = 2u;
    instance_info.ppEnabledExtensionNames = instance_extensions;
    if (vkCreateInstance(&instance_info, NULL, &instance) != VK_SUCCESS ||
        instance == VK_NULL_HANDLE) {
        write_text("vk_test: instance failed\n");
        goto done;
    }
    count = 1u;
    if (vkEnumeratePhysicalDevices(instance, &count, &physical_device) != VK_SUCCESS ||
        count != 1u || physical_device == VK_NULL_HANDLE) {
        write_text("vk_test: physical device failed\n");
        goto done;
    }
    vkGetPhysicalDeviceProperties(physical_device, &physical_properties);
    vkGetPhysicalDeviceFormatProperties(physical_device,
        VK_FORMAT_B8G8R8A8_UNORM, &format_properties);
    vkGetPhysicalDeviceFormatProperties(physical_device, VK_FORMAT_R8G8B8A8_UNORM,
                                        &rgba_format_properties);
    vkGetPhysicalDeviceFormatProperties(physical_device, VK_FORMAT_R32G32B32_SFLOAT,
                                        &vertex_format_properties);
    vkGetPhysicalDeviceFormatProperties(physical_device, VK_FORMAT_D32_SFLOAT,
                                        &depth_format_properties);
    if (physical_properties.limits.maxUniformBufferRange < 16u ||
        physical_properties.limits.maxVertexInputAttributes < 2u ||
        physical_properties.limits.maxVertexInputBindings != 1u ||
        physical_properties.limits.maxVertexInputAttributeOffset < 16u ||
        physical_properties.limits.maxVertexInputBindingStride < 32u ||
        (vertex_format_properties.bufferFeatures &
         VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) == 0u ||
        (rgba_format_properties.bufferFeatures &
         VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) == 0u ||
        physical_properties.limits.maxPerStageDescriptorUniformBuffers < 2u ||
        physical_properties.limits.maxPerStageResources < 2u ||
        physical_properties.limits.maxDescriptorSetUniformBuffers < 2u ||
        physical_properties.limits.minUniformBufferOffsetAlignment != 16u ||
        (format_properties.optimalTilingFeatures &
         VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) == 0u ||
        (rgba_format_properties.optimalTilingFeatures &
         (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
          VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) !=
         (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
          VK_FORMAT_FEATURE_TRANSFER_DST_BIT) ||
        depth_format_properties.linearTilingFeatures != 0u ||
        (depth_format_properties.optimalTilingFeatures &
         (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
          VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) !=
         (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
          VK_FORMAT_FEATURE_TRANSFER_DST_BIT) ||
        vkGetPhysicalDeviceImageFormatProperties(physical_device, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0u,
            &depth_image_properties) != VK_SUCCESS ||
        vkGetPhysicalDeviceImageFormatProperties(physical_device, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_LINEAR,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0u,
            &depth_image_properties) != VK_ERROR_FORMAT_NOT_SUPPORTED ||
        vkGetPhysicalDeviceImageFormatProperties(physical_device, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            0u, &depth_image_properties) != VK_SUCCESS ||
        vkGetPhysicalDeviceImageFormatProperties(physical_device, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            0u, &depth_image_properties) != VK_SUCCESS) {
        write_text("vk_test: advertised UBO/depth clear format limits disagree with renderer\n");
        goto done;
    }
    count = 99u;
    if (vkEnumerateDeviceExtensionProperties(physical_device, NULL, &count,
            device_extension_properties) != VK_SUCCESS || count != 1u ||
        strcmp(device_extension_properties[0].extensionName,
               VK_KHR_SWAPCHAIN_EXTENSION_NAME) != 0) {
        write_text("vk_test: device extension enumeration invalid\n");
        goto done;
    }
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = 0u;
    queue_info.queueCount = 1u;
    queue_info.pQueuePriorities = &priority;
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1u;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1u;
    device_info.ppEnabledExtensionNames = device_extensions;
    device_info.pEnabledFeatures = &disabled_features;
    if (vkCreateDevice(physical_device, &device_info, NULL, &feature_probe) !=
            VK_SUCCESS || feature_probe == VK_NULL_HANDLE) {
        write_text("vk_test: all-disabled feature structure rejected\n");
        goto done;
    }
    vkDestroyDevice(feature_probe, NULL);
    feature_probe = VK_NULL_HANDLE;
    unsupported_features.geometryShader = VK_TRUE;
    device_info.pEnabledFeatures = &unsupported_features;
    if (vkCreateDevice(physical_device, &device_info, NULL, &feature_probe) !=
            VK_ERROR_FEATURE_NOT_PRESENT || feature_probe != VK_NULL_HANDLE) {
        write_text("vk_test: unsupported feature was advertised/accepted\n");
        goto done;
    }
    vkGetPhysicalDeviceFeatures(physical_device, &disabled_features);
    if (!disabled_features.shaderUniformBufferArrayDynamicIndexing ||
        !disabled_features.shaderSampledImageArrayDynamicIndexing) {
        write_text("vk_test: implemented descriptor array features missing\n");
        goto done;
    }
    device_info.pEnabledFeatures = &disabled_features;
    write_text("vk_test: device feature enablement matches feature query\n");
    if (vkCreateDevice(physical_device, &device_info, NULL, &device) != VK_SUCCESS ||
        device == VK_NULL_HANDLE) {
        write_text("vk_test: device failed\n");
        goto done;
    }
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    if (vkGetInstanceProcAddr(instance, "vkCreateVexSurfaceVEX") == NULL ||
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") == NULL ||
        vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR") == NULL ||
        vkGetDeviceProcAddr(device, "vkQueuePresentKHR") == NULL) {
        write_text("vk_test: enabled Surface/Swapchain dispatch missing\n");
        goto done;
    }
    if (vkGetDeviceProcAddr(device, "vkCmdBlitImage") !=
            (PFN_vkVoidFunction)vkCmdBlitImage ||
        vkGetDeviceProcAddr(device, "vkCmdCopyImageToBuffer") !=
            (PFN_vkVoidFunction)vkCmdCopyImageToBuffer) {
        write_text("vk_test: implemented image commands missing from device dispatch\n");
        goto done;
    }

    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = 4096u;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &buffer_info, NULL, &buffer) != VK_SUCCESS ||
        buffer == VK_NULL_HANDLE) {
        write_text("vk_test: buffer failed\n");
        goto done;
    }
    vkGetBufferMemoryRequirements(device, buffer, &requirements);
    if (requirements.size < buffer_info.size || requirements.memoryTypeBits == 0u) {
        write_text("vk_test: buffer requirements invalid\n");
        goto done;
    }
    memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory_info.allocationSize = requirements.size;
    memory_info.memoryTypeIndex = 0u;
    if (vkAllocateMemory(device, &memory_info, NULL, &memory) != VK_SUCCESS ||
        memory == VK_NULL_HANDLE ||
        vkBindBufferMemory(device, buffer, memory, 0u) != VK_SUCCESS ||
        vkMapMemory(device, memory, 0u, VK_WHOLE_SIZE, 0u, &mapped) != VK_SUCCESS || mapped == NULL) {
        write_text("vk_test: memory/binding/mapping failed\n");
        goto done;
    }
    if (vkBindBufferMemory(device, buffer, memory, 0u) == VK_SUCCESS) {
        write_text("vk_test: double buffer bind accepted\n");
        goto done;
    }
    ((uint8_t *)mapped)[0] = 0x5au;
    if (((uint8_t *)mapped)[0] != 0x5au) {
        write_text("vk_test: mapped bytes mismatch\n");
        goto done;
    }
    vkUnmapMemory(device, memory);

    if (vkCreateBuffer(device, &buffer_info, NULL, &dst_buffer) != VK_SUCCESS ||
        vkAllocateMemory(device, &memory_info, NULL, &dst_memory) != VK_SUCCESS ||
        vkBindBufferMemory(device, dst_buffer, dst_memory, 0u) != VK_SUCCESS ||
        vkMapMemory(device, dst_memory, 0u, 16u, 0u, &dst_mapped) != VK_SUCCESS) {
        write_text("vk_test: copy destination failed\n");
        goto done;
    }
    ((uint8_t *)dst_mapped)[0] = 0u;
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = 0u;
    if (vkCreateCommandPool(device, &pool_info, NULL, &pool) != VK_SUCCESS) {
        write_text("vk_test: command pool failed\n");
        goto done;
    }
    cmd_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_alloc.commandPool = pool;
    cmd_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc.commandBufferCount = 1u;
    cmd_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkAllocateCommandBuffers(device, &cmd_alloc, &cmd) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: command begin failed\n");
        goto done;
    }
    copy_region.size = 4u;
    vkCmdCopyBuffer(cmd, buffer, dst_buffer, 1u, &copy_region);
    if (((uint8_t *)dst_mapped)[0] != 0u || vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        write_text("vk_test: copy executed while recording\n");
        goto done;
    }
    vkGetDeviceQueue(device, 0u, 0u, &queue);
    if(!vk_compute_smoke(device,queue)||!vk_compute_image_smoke(device,queue)||!vk_compute_ops_smoke(device,queue)||!vk_compute_arrays_smoke(device,queue)||!vk_compute_negative_smoke(physical_device)||!vk_texel_smoke(device,queue))goto done;
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1u;
    submit.pCommandBuffers = &cmd;
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device, &fence_info, NULL, &fence) != VK_SUCCESS ||
        queue == VK_NULL_HANDLE || vkGetFenceStatus(device, fence) != VK_NOT_READY ||
        vkQueueSubmit(queue, 1u, &submit, fence) != VK_SUCCESS ||
        vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS ||
        vkGetFenceStatus(device, fence) != VK_SUCCESS ||
        vkWaitForFences(device, 1u, &fence, VK_TRUE, 0u) != VK_SUCCESS ||
        ((uint8_t *)dst_mapped)[0] != 0x5au) {
        write_text("vk_test: deferred buffer copy failed\n");
        goto done;
    }
    if (vkResetFences(device, 1u, &fence) != VK_SUCCESS ||
        vkGetFenceStatus(device, fence) != VK_NOT_READY ||
        vkWaitForFences(device, 1u, &fence, VK_TRUE, 0u) != VK_TIMEOUT) {
        write_text("vk_test: fence timeout semantics failed\n");
        goto done;
    }

    descriptor_binding.binding = 0u;
    descriptor_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_binding.descriptorCount = 1u;
    descriptor_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    descriptor_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_layout_info.bindingCount = 1u;
    descriptor_layout_info.pBindings = &descriptor_binding;
    descriptor_pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_pool_size.descriptorCount = 1u;
    descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptor_pool_info.maxSets = 1u;
    descriptor_pool_info.poolSizeCount = 1u;
    descriptor_pool_info.pPoolSizes = &descriptor_pool_size;
    descriptor_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_buffer_info.buffer = buffer;
    descriptor_buffer_info.offset = 0u;
    descriptor_buffer_info.range = 16u;
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.dstBinding = 0u;
    descriptor_write.descriptorCount = 1u;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_write.pBufferInfo = &descriptor_buffer_info;
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0u;
    push_range.size = 4u;
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1u;
    pipeline_layout_info.pSetLayouts = &descriptor_layout;
    pipeline_layout_info.pushConstantRangeCount = 1u;
    pipeline_layout_info.pPushConstantRanges = &push_range;
    if (vkCreateDescriptorSetLayout(device, &descriptor_layout_info, NULL,
                                    &descriptor_layout) != VK_SUCCESS ||
        vkCreateDescriptorPool(device, &descriptor_pool_info, NULL,
                               &descriptor_pool) != VK_SUCCESS) {
        write_text("vk_test: descriptor layout/pool creation failed\n");
        goto done;
    }
    descriptor_alloc_info.descriptorPool = descriptor_pool;
    descriptor_alloc_info.descriptorSetCount = 1u;
    descriptor_alloc_info.pSetLayouts = &descriptor_layout;
    if (vkAllocateDescriptorSets(device, &descriptor_alloc_info,
                                 &descriptor_set) != VK_SUCCESS ||
        vkAllocateDescriptorSets(device, &descriptor_alloc_info,
                                 &(VkDescriptorSet){VK_NULL_HANDLE}) != VK_ERROR_OUT_OF_POOL_MEMORY ||
        vkCreatePipelineLayout(device, &pipeline_layout_info, NULL,
                               &pipeline_layout) != VK_SUCCESS) {
        write_text("vk_test: descriptor allocation/pipeline layout failed\n");
        goto done;
    }
    descriptor_write.dstSet = descriptor_set;
    vkUpdateDescriptorSets(device, 1u, &descriptor_write, 0u, NULL);
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: descriptor command begin failed\n");
        goto done;
    }
    uint32_t push_value = 0x12345678u;
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout,
                            0u, 1u, &descriptor_set, 0u, NULL);
    vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0u, sizeof(push_value), &push_value);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        vkFreeDescriptorSets(device, descriptor_pool, 1u,
                             &descriptor_set) != VK_SUCCESS) {
        write_text("vk_test: descriptor write/bind/deferred submit failed\n");
        goto done;
    }
    vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    descriptor_pool = VK_NULL_HANDLE;
    vkDestroyDescriptorSetLayout(device, descriptor_layout, NULL);
    descriptor_layout = VK_NULL_HANDLE;
    vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    pipeline_layout = VK_NULL_HANDLE;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
        write_text("vk_test: descriptor command-held lifetime failed\n");
        goto done;
    }
    descriptor_set = VK_NULL_HANDLE;
    write_text("vk_test: buffer descriptors and deferred set lifetime ok\n");
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.extent.width = 2u;
    image_info.extent.height = 2u;
    image_info.extent.depth = 1u;
    image_info.mipLevels = 1u;
    image_info.arrayLayers = 1u;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    memory_info.allocationSize = 16u;
    if (vkCreateImage(device, &image_info, NULL, &image) != VK_SUCCESS ||
        vkAllocateMemory(device, &memory_info, NULL, &image_memory) != VK_SUCCESS ||
        vkBindImageMemory(device, image, image_memory, 0u) != VK_SUCCESS ||
        vkMapMemory(device, image_memory, 0u, 16u, 0u, &image_mapped) != VK_SUCCESS) {
        write_text("vk_test: offscreen image failed\n");
        goto done;
    }
    memset(image_mapped, 0, 16u);
    image_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_range.levelCount = 1u;
    image_range.layerCount = 1u;
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = image;
    image_barrier.subresourceRange = image_range;
    clear_color.float32[0] = 1.0f;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: clear command begin failed\n");
        goto done;
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL,
                         0u, NULL, 1u, &image_barrier);
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, &clear_color, 1u, &image_range);
    if (((uint32_t *)image_mapped)[0] != 0u) {
        write_text("vk_test: clear executed during recording\n");
        goto done;
    }
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        write_text("vk_test: clear command end failed\n");
        goto done;
    }
    if (vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
        write_text("vk_test: clear queue submit failed\n");
        goto done;
    }
    if (((uint32_t *)image_mapped)[0] != 0x00ff0000u) {
        write_text("vk_test: deferred offscreen clear pixel0 failed\n");
        goto done;
    }
    if (((uint32_t *)image_mapped)[3] != 0x00ff0000u) {
        write_text("vk_test: deferred offscreen clear pixel3 failed\n");
        goto done;
    }
    if (vkMapMemory(device, memory, 0u, 4u, 0u, &mapped) != VK_SUCCESS) {
        write_text("vk_test: source remap failed\n");
        goto done;
    }
    ((uint32_t *)mapped)[0] = 0xff123456u;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_copy.imageSubresource.layerCount = 1u;
    image_copy.imageExtent.width = 1u;
    image_copy.imageExtent.height = 1u;
    image_copy.imageExtent.depth = 1u;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: image copy begin failed\n");
        goto done;
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL,
                         0u, NULL, 1u, &image_barrier);
    vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1u, &image_copy);
    if (((uint32_t *)image_mapped)[0] != 0x00ff0000u ||
        vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        ((uint32_t *)image_mapped)[0] != 0xff123456u ||
        ((uint32_t *)image_mapped)[1] != 0x00ff0000u) {
        write_text("vk_test: deferred buffer-to-image copy failed\n");
        goto done;
    }
    if (vkCreateImage(device, &image_info, NULL, &copied_image) != VK_SUCCESS ||
        vkAllocateMemory(device, &memory_info, NULL, &copied_image_memory) != VK_SUCCESS ||
        vkBindImageMemory(device, copied_image, copied_image_memory, 0u) != VK_SUCCESS ||
        vkMapMemory(device, copied_image_memory, 0u, 16u, 0u,
                    &copied_image_mapped) != VK_SUCCESS) {
        write_text("vk_test: second image failed\n");
        goto done;
    }
    memset(copied_image_mapped, 0, 16u);
    copy_barriers[0] = image_barrier;
    copy_barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copy_barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    copy_barriers[1] = image_barrier;
    copy_barriers[1].image = copied_image;
    copy_barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    copy_barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_region.srcSubresource.layerCount = 1u;
    image_region.dstSubresource = image_region.srcSubresource;
    image_region.extent.width = 2u;
    image_region.extent.height = 2u;
    image_region.extent.depth = 1u;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: image copy begin failed\n");
        goto done;
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL,
                         0u, NULL, 2u, copy_barriers);
    vkCmdCopyImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   copied_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1u, &image_region);
    if (((uint32_t *)copied_image_mapped)[0] != 0u ||
        vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        ((uint32_t *)copied_image_mapped)[0] != 0xff123456u ||
        ((uint32_t *)copied_image_mapped)[3] != 0x00ff0000u) {
        write_text("vk_test: deferred image copy failed\n");
        goto done;
    }
    {
        VkImageBlit blit_region = {0};
        blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.srcSubresource.layerCount = 1u;
        blit_region.dstSubresource = blit_region.srcSubresource;
        blit_region.srcOffsets[1].x = 2;
        blit_region.srcOffsets[1].y = 2;
        blit_region.srcOffsets[1].z = 1;
        blit_region.dstOffsets[1] = blit_region.srcOffsets[1];
        memset(copied_image_mapped, 0, 16u);
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: image blit begin failed\n");
            goto done;
        }
        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       copied_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1u, &blit_region, VK_FILTER_NEAREST);
        if (((uint32_t *)copied_image_mapped)[0] != 0u ||
            vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            ((uint32_t *)copied_image_mapped)[0] != 0xff123456u ||
            ((uint32_t *)copied_image_mapped)[3] != 0x00ff0000u) {
            write_text("vk_test: deferred nearest image blit failed\n");
            goto done;
        }
        write_text("vk_test: deferred nearest image blit ok\n");

        blit_region.dstOffsets[1].x = 1;
        blit_region.dstOffsets[1].y = 1;
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: linear image blit begin failed\n");
            goto done;
        }
        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       copied_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1u, &blit_region, VK_FILTER_LINEAR);
        if (((uint32_t *)copied_image_mapped)[0] != 0xff123456u ||
            vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            ((uint32_t *)copied_image_mapped)[0] != 0x40c40d16u) {
            write_text("vk_test: deferred linear image blit failed\n");
            goto done;
        }
        write_text("vk_test: deferred linear image blit ok\n");
    }
    memset(dst_mapped, 0, 16u);
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.image = copied_image;
    image_copy.imageExtent.width = 2u;
    image_copy.imageExtent.height = 2u;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: image readback begin failed\n");
        goto done;
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL,
                         0u, NULL, 1u, &image_barrier);
    vkCmdCopyImageToBuffer(cmd, copied_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           dst_buffer, 1u, &image_copy);
    if (((uint32_t *)dst_mapped)[0] != 0u) {
        write_text("vk_test: image-to-buffer executed during recording\n");
        goto done;
    }
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        write_text("vk_test: image-to-buffer command validation failed\n");
        goto done;
    }
    if (vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
        write_text("vk_test: image-to-buffer submit failed\n");
        goto done;
    }
    if (((uint32_t *)dst_mapped)[0] != 0x40c40d16u ||
        ((uint32_t *)dst_mapped)[3] != 0x00ff0000u) {
        write_text("vk_test: deferred image-to-buffer pixels failed\n");
        goto done;
    }
    write_text("vk_test: deferred image-to-buffer readback ok\n");
    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.image = image;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    image_view_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.subresourceRange = image_range;
    render_attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
    render_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    render_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    render_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    render_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    render_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    render_attachment.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    render_attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    render_color_ref.attachment = 0u;
    render_color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    render_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    render_subpass.colorAttachmentCount = 1u;
    render_subpass.pColorAttachments = &render_color_ref;
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1u;
    render_pass_info.pAttachments = &render_attachment;
    render_pass_info.subpassCount = 1u;
    render_pass_info.pSubpasses = &render_subpass;
    if (vkCreateImageView(device, &image_view_info, NULL, &image_view) != VK_SUCCESS) {
        write_text("vk_test: color attachment image view creation failed\n");
        goto done;
    }
    if (vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass) != VK_SUCCESS) {
        write_text("vk_test: color render pass creation failed\n");
        goto done;
    }
    descriptor_binding.binding = 0u;
    descriptor_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_binding.descriptorCount = 1u;
    descriptor_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptor_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_layout_info.bindingCount = 1u;
    descriptor_layout_info.pBindings = &descriptor_binding;
    descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptor_pool_info.maxSets = 2u;
    descriptor_pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_pool_size.descriptorCount = 2u;
    descriptor_pool_info.poolSizeCount = 1u;
    descriptor_pool_info.pPoolSizes = &descriptor_pool_size;
    if (vkCreateDescriptorSetLayout(device, &descriptor_layout_info, NULL,
                                    &descriptor_layout) != VK_SUCCESS ||
        vkCreateDescriptorPool(device, &descriptor_pool_info, NULL,
                               &descriptor_pool) != VK_SUCCESS) {
        write_text("vk_test: shader descriptor layout/pool failed\n");
        goto done;
    }
    descriptor_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_alloc_info.descriptorPool = descriptor_pool;
    descriptor_alloc_info.descriptorSetCount = 2u;
    shader_set_layouts[0] = descriptor_layout;
    shader_set_layouts[1] = descriptor_layout;
    descriptor_alloc_info.pSetLayouts = shader_set_layouts;
    if (vkAllocateDescriptorSets(device, &descriptor_alloc_info,
                                 shader_sets) != VK_SUCCESS) {
        write_text("vk_test: shader descriptor set allocation failed\n");
        goto done;
    }
    descriptor_set = shader_sets[0];
    uninitialized_descriptor_set = shader_sets[1];
    descriptor_buffer_info.buffer = buffer;
    descriptor_buffer_info.offset = 0u;
    descriptor_buffer_info.range = 16u;
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.dstSet = descriptor_set;
    descriptor_write.dstBinding = 0u;
    descriptor_write.dstArrayElement = 0u;
    descriptor_write.descriptorCount = 1u;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptor_write.pBufferInfo = &descriptor_buffer_info;
    descriptor_write.pImageInfo = NULL;
    vkUpdateDescriptorSets(device, 1u, &descriptor_write, 0u, NULL);
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render_pass;
    framebuffer_info.attachmentCount = 1u;
    framebuffer_info.pAttachments = &image_view;
    framebuffer_info.width = 2u;
    framebuffer_info.height = 2u;
    framebuffer_info.layers = 1u;
    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = sizeof(triangle_vertex_spv);
    shader_info.pCode = triangle_vertex_spv;
    if (vkCreateShaderModule(device, &shader_info, NULL, &vertex_shader) != VK_SUCCESS) {
        write_text("vk_test: SPIR-V vertex module rejected\n");
        goto done;
    }
    shader_info.codeSize = sizeof(triangle_fragment_spv);
    shader_info.pCode = triangle_fragment_spv;
    if (vkCreateShaderModule(device, &shader_info, NULL, &fragment_shader) != VK_SUCCESS) {
        write_text("vk_test: SPIR-V fragment module rejected\n");
        goto done;
    }
    graphics_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    graphics_layout_info.setLayoutCount = 1u;
    graphics_layout_info.pSetLayouts = &descriptor_layout;
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0u;
    push_range.size = 16u;
    graphics_layout_info.pushConstantRangeCount = 1u;
    graphics_layout_info.pPushConstantRanges = &push_range;
    if (vkCreatePipelineLayout(device, &graphics_layout_info, NULL, &graphics_layout) != VK_SUCCESS) {
        write_text("vk_test: graphics pipeline layout failed\n");
        goto done;
    }
    graphics_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    graphics_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    graphics_stages[0].module = vertex_shader;
    graphics_stages[0].pName = "main";
    graphics_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    graphics_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    graphics_stages[1].module = fragment_shader;
    graphics_stages[1].pName = "main";
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1u;
    viewport_state.pViewports = &graphics_viewport;
    viewport_state.scissorCount = 1u;
    viewport_state.pScissors = &graphics_scissor;
    graphics_viewport.width = 2.0f;
    graphics_viewport.height = 2.0f;
    graphics_viewport.maxDepth = 1.0f;
    graphics_scissor.extent.width = 2u;
    graphics_scissor.extent.height = 2u;
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment_state.blendEnable = VK_FALSE;
    color_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1u;
    color_blend.pAttachments = &color_attachment_state;
    graphics_pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    graphics_pipeline_info.stageCount = 2u;
    graphics_pipeline_info.pStages = graphics_stages;
    graphics_pipeline_info.pVertexInputState = &vertex_input;
    graphics_pipeline_info.pInputAssemblyState = &input_assembly;
    graphics_pipeline_info.pViewportState = &viewport_state;
    graphics_pipeline_info.pRasterizationState = &rasterization;
    graphics_pipeline_info.pMultisampleState = &multisample;
    graphics_pipeline_info.pColorBlendState = &color_blend;
    graphics_pipeline_info.layout = graphics_layout;
    graphics_pipeline_info.renderPass = render_pass;
    graphics_pipeline_info.basePipelineIndex = -1;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &graphics_pipeline) != VK_SUCCESS) {
        write_text("vk_test: supported SPIR-V graphics pipeline rejected\n");
        goto done;
    }
    shader_info.codeSize = sizeof(integer_ops_fragment_spv);
    shader_info.pCode = integer_ops_fragment_spv;
    if (vkCreateShaderModule(device, &shader_info, NULL,
                            &integer_ops_fragment_shader) != VK_SUCCESS) {
        write_text("vk_test: integer-ops SPIR-V module rejected\n");
        goto done;
    }
    graphics_stages[1].module = integer_ops_fragment_shader;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u,
                                  &graphics_pipeline_info, NULL,
                                  &integer_ops_pipeline) != VK_SUCCESS) {
        write_text("vk_test: integer-ops SPIR-V pipeline rejected\n");
        goto done;
    }
    graphics_stages[1].module = fragment_shader;
    color_attachment_state.blendEnable = VK_TRUE;
    color_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
    color_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &alpha_pipeline) != VK_SUCCESS) {
        write_text("vk_test: alpha blend pipeline rejected\n");
        goto done;
    }
    color_attachment_state.blendEnable = VK_FALSE;
    color_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    shader_info.codeSize = sizeof(push_constant_fragment_spv);
    shader_info.pCode = push_constant_fragment_spv;
    if (vkCreateShaderModule(device, &shader_info, NULL, &push_fragment_shader) != VK_SUCCESS) {
        write_text("vk_test: push-constant SPIR-V module rejected\n");
        goto done;
    }
    graphics_stages[1].module = push_fragment_shader;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &push_pipeline) != VK_SUCCESS) {
        write_text("vk_test: push-constant graphics pipeline rejected\n");
        goto done;
    }
    graphics_stages[1].module = fragment_shader;
    shader_info.codeSize = sizeof(vector_math_fragment_spv);
    shader_info.pCode = vector_math_fragment_spv;
    if (vkCreateShaderModule(device, &shader_info, NULL, &vector_math_fragment_shader) != VK_SUCCESS) {
        write_text("vk_test: vector-math SPIR-V module rejected\n");
        goto done;
    }
    graphics_stages[1].module = vector_math_fragment_shader;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &vector_math_pipeline) != VK_SUCCESS) {
        write_text("vk_test: vector-math graphics pipeline rejected\n");
        goto done;
    }
    graphics_stages[1].module = fragment_shader;
    vkDestroyShaderModule(device, vector_math_fragment_shader, NULL);
    vector_math_fragment_shader = VK_NULL_HANDLE;
    shader_info.codeSize = sizeof(varying_vertex_spv);
    shader_info.pCode = varying_vertex_spv;
    if (vkCreateShaderModule(device, &shader_info, NULL, &varying_vertex_shader) != VK_SUCCESS) {
        write_text("vk_test: varying vertex SPIR-V module rejected\n");
        goto done;
    }
    shader_info.codeSize = sizeof(varying_fragment_spv);
    shader_info.pCode = varying_fragment_spv;
    if (vkCreateShaderModule(device, &shader_info, NULL, &varying_fragment_shader) != VK_SUCCESS) {
        write_text("vk_test: varying fragment SPIR-V module rejected\n");
        goto done;
    }
    graphics_stages[0].module = varying_vertex_shader;
    graphics_stages[1].module = varying_fragment_shader;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &varying_pipeline) != VK_SUCCESS) {
        write_text("vk_test: interpolated varying graphics pipeline rejected\n");
        goto done;
    }
    shader_info.codeSize = sizeof(vertex_attribute_spv);
    shader_info.pCode = vertex_attribute_spv;
    if (vkCreateShaderModule(device, &shader_info, NULL, &vertex_attribute_shader) != VK_SUCCESS) {
        write_text("vk_test: vertex-attribute SPIR-V module rejected\n");
        goto done;
    }
    graphics_stages[0].module = vertex_attribute_shader;
    graphics_stages[1].module = push_fragment_shader;
    vertex_binding.binding = 0u;
    vertex_binding.stride = sizeof(float) * 4u;
    vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertex_attribute.location = 0u;
    vertex_attribute.binding = 0u;
    vertex_attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertex_attribute.offset = 0u;
    vertex_input.vertexBindingDescriptionCount = 1u;
    vertex_input.pVertexBindingDescriptions = &vertex_binding;
    vertex_input.vertexAttributeDescriptionCount = 1u;
    vertex_input.pVertexAttributeDescriptions = &vertex_attribute;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &vertex_attribute_pipeline) != VK_SUCCESS) {
        write_text("vk_test: vertex-buffer graphics pipeline rejected\n");
        goto done;
    }
    shader_info.codeSize = sizeof(multi_attribute_vertex_spv);
    shader_info.pCode = multi_attribute_vertex_spv;
    if (vkCreateShaderModule(device, &shader_info, NULL,
                             &multi_attribute_shader) != VK_SUCCESS) {
        write_text("vk_test: multi-attribute SPIR-V module rejected\n");
        goto done;
    }
    graphics_stages[0].module = multi_attribute_shader;
    vertex_binding.stride = sizeof(float) * 8u;
    multi_attribute_descriptions[0].location = 0u;
    multi_attribute_descriptions[0].binding = 0u;
    multi_attribute_descriptions[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    multi_attribute_descriptions[0].offset = 0u;
    multi_attribute_descriptions[1].location = 1u;
    multi_attribute_descriptions[1].binding = 0u;
    multi_attribute_descriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    multi_attribute_descriptions[1].offset = sizeof(float) * 4u;
    vertex_input.vertexAttributeDescriptionCount = 2u;
    vertex_input.pVertexAttributeDescriptions = multi_attribute_descriptions;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u,
            &graphics_pipeline_info, NULL, &multi_attribute_pipeline) != VK_SUCCESS) {
        write_text("vk_test: multi-attribute vertex pipeline rejected\n");
        goto done;
    }
    vertex_binding.stride = sizeof(float) * 4u + sizeof(uint32_t);
    multi_attribute_descriptions[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u,
            &graphics_pipeline_info, NULL, &normalized_attribute_pipeline) != VK_SUCCESS) {
        write_text("vk_test: normalized vertex pipeline rejected\n");
        goto done;
    }
    multi_attribute_descriptions[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertex_binding.stride = sizeof(float) * 8u;
    shader_info.codeSize = sizeof(vec3_attribute_vertex_spv);
    shader_info.pCode = vec3_attribute_vertex_spv;
    if (vkCreateShaderModule(device, &shader_info, NULL,
                             &vec3_attribute_shader) != VK_SUCCESS) {
        write_text("vk_test: vec3 vertex SPIR-V module rejected\n");
        goto done;
    }
    graphics_stages[0].module = vec3_attribute_shader;
    vertex_binding.stride = sizeof(float) * 4u;
    vertex_attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    vertex_input.vertexAttributeDescriptionCount = 1u;
    vertex_input.pVertexAttributeDescriptions = &vertex_attribute;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u,
            &graphics_pipeline_info, NULL, &vec3_attribute_pipeline) != VK_SUCCESS) {
        write_text("vk_test: vec3 vertex pipeline rejected\n");
        goto done;
    }
    graphics_stages[0].module = vertex_attribute_shader;
    vertex_binding.stride = sizeof(float) * 4u;
    vertex_attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    vertex_input.vertexAttributeDescriptionCount = 1u;
    vertex_input.pVertexAttributeDescriptions = &vertex_attribute;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &strip_pipeline) != VK_SUCCESS) {
        write_text("vk_test: triangle-strip pipeline rejected\n");
        goto done;
    }
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &fan_pipeline) != VK_SUCCESS) {
        write_text("vk_test: triangle-fan pipeline rejected\n");
        goto done;
    }
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &point_pipeline) != VK_SUCCESS) {
        write_text("vk_test: point-list pipeline rejected\n");
        goto done;
    }
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &line_list_pipeline) != VK_SUCCESS) {
        write_text("vk_test: line-list pipeline rejected\n");
        goto done;
    }
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &line_strip_pipeline) != VK_SUCCESS) {
        write_text("vk_test: line-strip pipeline rejected\n");
        goto done;
    }
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    vertex_input.vertexBindingDescriptionCount = 0u;
    vertex_input.pVertexBindingDescriptions = NULL;
    vertex_input.vertexAttributeDescriptionCount = 0u;
    vertex_input.pVertexAttributeDescriptions = NULL;
    graphics_stages[0].module = vertex_shader;
    graphics_stages[1].module = fragment_shader;
    dynamic_state_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state_info.dynamicStateCount = 2u;
    dynamic_state_info.pDynamicStates = dynamic_states;
    viewport_state.pViewports = NULL;
    viewport_state.pScissors = NULL;
    graphics_pipeline_info.pDynamicState = &dynamic_state_info;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &dynamic_state_pipeline) != VK_SUCCESS) {
        write_text("vk_test: dynamic viewport/scissor pipeline rejected\n");
        goto done;
    }
    graphics_pipeline_info.pDynamicState = NULL;
    viewport_state.pViewports = &graphics_viewport;
    viewport_state.pScissors = &graphics_scissor;
    vkDestroyShaderModule(device, vec3_attribute_shader, NULL);
    vec3_attribute_shader = VK_NULL_HANDLE;
    vkDestroyShaderModule(device, multi_attribute_shader, NULL);
    multi_attribute_shader = VK_NULL_HANDLE;
    vkDestroyShaderModule(device, vertex_attribute_shader, NULL);
    vertex_attribute_shader = VK_NULL_HANDLE;
    vkDestroyShaderModule(device, push_fragment_shader, NULL);
    push_fragment_shader = VK_NULL_HANDLE;
    vkDestroyShaderModule(device, varying_vertex_shader, NULL);
    varying_vertex_shader = VK_NULL_HANDLE;
    vkDestroyShaderModule(device, varying_fragment_shader, NULL);
    varying_fragment_shader = VK_NULL_HANDLE;
    vkDestroyShaderModule(device, vertex_shader, NULL);
    vertex_shader = VK_NULL_HANDLE;
    vkDestroyShaderModule(device, fragment_shader, NULL);
    fragment_shader = VK_NULL_HANDLE;
    vkDestroyShaderModule(device, integer_ops_fragment_shader, NULL);
    integer_ops_fragment_shader = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffer) != VK_SUCCESS ||
        vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: framebuffer creation/record begin failed\n");
        goto done;
    }
    render_clear.color.float32[3] = 1.0f;
    render_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_begin.renderPass = render_pass;
    render_begin.framebuffer = framebuffer;
    render_begin.renderArea.offset.x = 0;
    render_begin.renderArea.offset.y = 0;
    render_begin.renderArea.extent.width = 2u;
    render_begin.renderArea.extent.height = 2u;
    render_begin.clearValueCount = 1u;
    render_begin.pClearValues = &render_clear;
    vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, push_pipeline);
    vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_ERROR_FEATURE_NOT_PRESENT ||
        ((uint32_t *)image_mapped)[0] != 0xff123456u ||
        vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: missing push constants were not rejected safely\n");
        goto done;
    }
    vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_layout,
                            0u, 1u, &descriptor_set, 0u, NULL);
    vkCmdDraw(cmd, 6u, 1u, 0u, 0u);
    vkCmdEndRenderPass(cmd);
    ((float *)mapped)[0] = 0.25f;
    ((float *)mapped)[1] = 0.5f;
    ((float *)mapped)[2] = 0.75f;
    ((float *)mapped)[3] = 1.0f;
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        ((uint32_t *)image_mapped)[0] != 0xff123456u ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        ((uint32_t *)image_mapped)[0] != 0xff4080bfu ||
        ((uint32_t *)image_mapped)[1] != 0xff4080bfu ||
        ((uint32_t *)image_mapped)[2] != 0xff000000u ||
        ((uint32_t *)image_mapped)[3] != 0xff000000u) {
        write_text("vk_test: deferred SPIR-V triangle pixels/layout failed\n");
        goto done;
    }
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: integer-ops command begin failed\n");
        goto done;
    }
    memset(&image_barrier, 0, sizeof(image_barrier));
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = image;
    image_barrier.subresourceRange = image_range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                         0u, NULL, 0u, NULL, 1u, &image_barrier);
    vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, integer_ops_pipeline);
    vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        ((uint32_t *)image_mapped)[0] != 0xffffffffu ||
        ((uint32_t *)image_mapped)[1] != 0xffffffffu) {
        write_text("vk_test: integer/logical SPIR-V execution pixels failed\n");
        goto done;
    }
    write_text("vk_test: SPIR-V integer arithmetic/bitwise/logical ops execute\n");
    {
        render_clear.color.float32[0] = 0.0f;
        render_clear.color.float32[1] = 0.0f;
        render_clear.color.float32[2] = 1.0f;
        render_clear.color.float32[3] = 1.0f;
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: alpha blend command begin failed\n");
            goto done;
        }
        memset(&image_barrier, 0, sizeof(image_barrier));
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.image = image;
        image_barrier.subresourceRange = image_range;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, alpha_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_layout,
                                0u, 1u, &descriptor_set, 0u, NULL);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        ((float *)mapped)[0] = 1.0f;
        ((float *)mapped)[1] = 0.0f;
        ((float *)mapped)[2] = 0.0f;
        ((float *)mapped)[3] = 0.5f;
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            write_text("vk_test: alpha blend command end failed\n");
            goto done;
        }
        if (vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
            write_text("vk_test: alpha blend submission failed\n");
            goto done;
        }
        if (((uint32_t *)image_mapped)[0] != 0xff800080u ||
            ((uint32_t *)image_mapped)[1] != 0xff800080u ||
            ((uint32_t *)image_mapped)[2] != 0xff0000ffu ||
            ((uint32_t *)image_mapped)[3] != 0xff0000ffu) {
            write_text("vk_test: deferred alpha blending pixels failed\n");
            goto done;
        }
        ((float *)mapped)[0] = 0.25f;
        ((float *)mapped)[1] = 0.5f;
        ((float *)mapped)[2] = 0.75f;
        ((float *)mapped)[3] = 1.0f;
    }
    write_text("vk_test: deferred SPIR-V alpha blend and color output ok\n");
    {
        VkViewport dynamic_viewport = {0};
        VkRect2D dynamic_scissor = {0};
        dynamic_viewport.width = 1.0f;
        dynamic_viewport.height = 1.0f;
        dynamic_viewport.maxDepth = 1.0f;
        dynamic_scissor.extent.width = 2u;
        dynamic_scissor.extent.height = 2u;
        memcpy(pixels_before_invalid_descriptor, image_mapped,
               sizeof(pixels_before_invalid_descriptor));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: dynamic state missing-set begin failed\n");
            goto done;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dynamic_state_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_layout,
                                0u, 1u, &descriptor_set, 0u, NULL);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_ERROR_FEATURE_NOT_PRESENT ||
            memcmp(pixels_before_invalid_descriptor, image_mapped,
                   sizeof(pixels_before_invalid_descriptor)) != 0 ||
            vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: dynamic pipeline accepted unset state or mutated target\n");
            goto done;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdSetViewport(cmd, 0u, 1u, &dynamic_viewport);
        vkCmdSetScissor(cmd, 0u, 1u, &dynamic_scissor);
        render_clear.color.float32[0] = 0.0f;
        render_clear.color.float32[1] = 0.0f;
        render_clear.color.float32[2] = 0.0f;
        render_clear.color.float32[3] = 1.0f;
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, dynamic_state_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_layout,
                                0u, 1u, &descriptor_set, 0u, NULL);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            memcmp(pixels_before_invalid_descriptor, image_mapped,
                   sizeof(pixels_before_invalid_descriptor)) != 0 ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            ((uint32_t *)image_mapped)[0] != 0xff4080bfu ||
            ((uint32_t *)image_mapped)[1] != 0xff000000u ||
            ((uint32_t *)image_mapped)[2] != 0xff000000u ||
            ((uint32_t *)image_mapped)[3] != 0xff000000u) {
            write_text("vk_test: deferred dynamic viewport/scissor execution failed\n");
            goto done;
        }
        write_text("vk_test: dynamic viewport/scissor state and preflight ok\n");
    }
    {
        const float math_push_color[4] = {1.0f, 0.25f, 0.0f, 1.0f};
        VkResult math_result;
        memcpy(pixels_before_invalid_descriptor, image_mapped,
               sizeof(pixels_before_invalid_descriptor));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: vector-math command begin failed\n");
            goto done;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &image_barrier);
        render_clear.color.float32[0] = 0.0f;
        render_clear.color.float32[1] = 0.0f;
        render_clear.color.float32[2] = 0.0f;
        render_clear.color.float32[3] = 1.0f;
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vector_math_pipeline);
        vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0u, sizeof(math_push_color), math_push_color);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        math_result = vkEndCommandBuffer(cmd);
        if (math_result != VK_SUCCESS) {
            write_text("vk_test: vector-math command end failed\n");
            write_hex32("vk_test: vector-math end result=0x", (uint32_t)math_result);
            goto done;
        }
        if (memcmp(pixels_before_invalid_descriptor, image_mapped,
                   sizeof(pixels_before_invalid_descriptor)) != 0) {
            write_text("vk_test: vector-math mutated target before submit\n");
            goto done;
        }
        math_result = vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE);
        if (math_result != VK_SUCCESS) {
            write_text("vk_test: vector-math submit failed\n");
            write_hex32("vk_test: vector-math submit result=0x", (uint32_t)math_result);
            goto done;
        }
        if (((uint32_t *)image_mapped)[0] != 0x60606060u ||
            ((uint32_t *)image_mapped)[1] != 0x60606060u ||
            ((uint32_t *)image_mapped)[2] != 0xff000000u ||
            ((uint32_t *)image_mapped)[3] != 0xff000000u) {
            write_text("vk_test: deferred SPIR-V vector-scalar/dot result failed\n");
            write_hex32("vk_test: vector-math pixel0=0x", ((uint32_t *)image_mapped)[0]);
            write_hex32("vk_test: vector-math pixel1=0x", ((uint32_t *)image_mapped)[1]);
            write_hex32("vk_test: vector-math pixel2=0x", ((uint32_t *)image_mapped)[2]);
            write_hex32("vk_test: vector-math pixel3=0x", ((uint32_t *)image_mapped)[3]);
            goto done;
        }
        write_text("vk_test: deferred SPIR-V vector math, control flow, and GLSL clamp ok\n");
        {
            const float math_push_white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            memcpy(pixels_before_invalid_descriptor, image_mapped,
                   sizeof(pixels_before_invalid_descriptor));
            if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
                vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
                write_text("vk_test: vector-math false-branch begin failed\n");
                goto done;
            }
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                                 0u, NULL, 0u, NULL, 1u, &image_barrier);
            render_clear.color.float32[0] = 0.0f;
            render_clear.color.float32[1] = 0.0f;
            render_clear.color.float32[2] = 1.0f;
            render_clear.color.float32[3] = 1.0f;
            vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vector_math_pipeline);
            vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                               0u, sizeof(math_push_white), math_push_white);
            vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
            vkCmdEndRenderPass(cmd);
            if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
                memcmp(pixels_before_invalid_descriptor, image_mapped,
                       sizeof(pixels_before_invalid_descriptor)) != 0 ||
                vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
                ((uint32_t *)image_mapped)[0] != 0x00000000u ||
                ((uint32_t *)image_mapped)[1] != 0x00000000u ||
                ((uint32_t *)image_mapped)[2] != 0xff0000ffu ||
                ((uint32_t *)image_mapped)[3] != 0xff0000ffu) {
                write_text("vk_test: deferred SPIR-V conditional false edge failed\n");
                goto done;
            }
            write_text("vk_test: deferred SPIR-V conditional true/false edges and phi ok\n");
        }
    }
    /* An uninitialized but layout-compatible descriptor is rejected during
       submit preflight, before the clear or draw can mutate the target. */
    {
        VkDescriptorSetLayoutBinding bindings[2] = {{0}, {0}};
        VkDescriptorPoolSize pool_size = {0};
        VkDescriptorSetLayoutCreateInfo layout_info = {0};
        VkDescriptorPoolCreateInfo pool_info = {0};
        VkPipelineLayoutCreateInfo pipeline_layout_info_local = {0};
        VkDescriptorBufferInfo buffer_infos[2] = {{0}, {0}};
        VkWriteDescriptorSet writes[2] = {{0}, {0}};
        VkResult two_uniform_pipeline_result;
        float uniform_values[16] = {
            0.25f, 0.50f, 0.75f, 0.50f,
            0.00f, 0.00f, 0.00f, 0.00f,
            0.00f, 0.00f, 0.00f, 0.00f,
            0.25f, 0.25f, 0.25f, 0.50f
        };
        const uint32_t expected_pixel = 0xff80bfffu;
        if (mapped == NULL) {
            write_text("vk_test: two-UBO backing is not mapped\n");
            goto done;
        }
        memcpy((uint8_t *)mapped + 128u, uniform_values, sizeof(uniform_values));
        for (uint32_t binding = 0u; binding < 2u; ++binding) {
            bindings[binding].binding = binding;
            bindings[binding].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            bindings[binding].descriptorCount = 1u;
            bindings[binding].stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                           VK_SHADER_STAGE_FRAGMENT_BIT;
            buffer_infos[binding].buffer = buffer;
            buffer_infos[binding].offset = 128u + binding * 32u;
            buffer_infos[binding].range = 32u;
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1u;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[binding].pBufferInfo = &buffer_infos[binding];
        }
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 2u;
        layout_info.pBindings = bindings;
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = 2u;
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.maxSets = 1u;
        pool_info.poolSizeCount = 1u;
        pool_info.pPoolSizes = &pool_size;
        pipeline_layout_info_local.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info_local.setLayoutCount = 1u;
        pipeline_layout_info_local.pSetLayouts = &two_uniform_layout;
        if (vkCreateDescriptorSetLayout(device, &layout_info, NULL,
                                        &two_uniform_layout) != VK_SUCCESS ||
            vkCreateDescriptorPool(device, &pool_info, NULL,
                                   &two_uniform_pool) != VK_SUCCESS) {
            write_text("vk_test: two-UBO layout/pool creation failed\n");
            goto done;
        }
        descriptor_alloc_info.descriptorPool = two_uniform_pool;
        descriptor_alloc_info.descriptorSetCount = 1u;
        descriptor_alloc_info.pSetLayouts = &two_uniform_layout;
        if (vkAllocateDescriptorSets(device, &descriptor_alloc_info,
                                    &two_uniform_set) != VK_SUCCESS) {
            write_text("vk_test: two-UBO descriptor allocation failed\n");
            goto done;
        }
        for (uint32_t binding = 0u; binding < 2u; ++binding)
            writes[binding].dstSet = two_uniform_set;
        vkUpdateDescriptorSets(device, 2u, writes, 0u, NULL);
        pipeline_layout_info_local.pSetLayouts = &two_uniform_layout;
        if (vkCreatePipelineLayout(device, &pipeline_layout_info_local, NULL,
                                  &two_uniform_pipeline_layout) != VK_SUCCESS) {
            write_text("vk_test: two-UBO pipeline layout creation failed\n");
            goto done;
        }
        shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_info.codeSize = sizeof(triangle_vertex_spv);
        shader_info.pCode = triangle_vertex_spv;
        if (vkCreateShaderModule(device, &shader_info, NULL,
                                 &two_uniform_vertex_shader) != VK_SUCCESS) {
            write_text("vk_test: two-UBO vertex shader rejected\n");
            goto done;
        }
        shader_info.codeSize = sizeof(two_uniform_fragment_spv);
        shader_info.pCode = two_uniform_fragment_spv;
        if (vkCreateShaderModule(device, &shader_info, NULL,
                                 &two_uniform_shader) != VK_SUCCESS) {
            write_text("vk_test: two-UBO shader rejected\n");
            goto done;
        }
        graphics_stages[0].module = two_uniform_vertex_shader;
        graphics_stages[1].module = two_uniform_shader;
        graphics_pipeline_info.layout = two_uniform_pipeline_layout;
        graphics_pipeline_info.pDynamicState = NULL;
        vertex_input.vertexBindingDescriptionCount = 0u;
        vertex_input.pVertexBindingDescriptions = NULL;
        vertex_input.vertexAttributeDescriptionCount = 0u;
        vertex_input.pVertexAttributeDescriptions = NULL;
        two_uniform_pipeline_result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u,
                                      &graphics_pipeline_info, NULL,
                                      &two_uniform_pipeline);
        if (two_uniform_pipeline_result != VK_SUCCESS) {
            write_text("vk_test: two-UBO pipeline rejected\n");
            write_hex32("vk_test: two-UBO pipeline result=0x",
                        (uint32_t)two_uniform_pipeline_result);
            goto done;
        }
        buffer_infos[1].range = 16u;
        vkUpdateDescriptorSets(device, 1u, &writes[1], 0u, NULL);
        memcpy(pixels_before_invalid_descriptor, image_mapped,
               sizeof(pixels_before_invalid_descriptor));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: short UBO-range command begin failed\n");
            goto done;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, two_uniform_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                two_uniform_pipeline_layout, 0u, 1u,
                                &two_uniform_set, 0u, NULL);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) !=
                VK_ERROR_INITIALIZATION_FAILED ||
            memcmp(pixels_before_invalid_descriptor, image_mapped,
                   sizeof(pixels_before_invalid_descriptor)) != 0 ||
            vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
            write_text("vk_test: undersized multi-member UBO was not rejected in preflight\n");
            goto done;
        }
        buffer_infos[1].range = 32u;
        vkUpdateDescriptorSets(device, 1u, &writes[1], 0u, NULL);
        memcpy(pixels_before_invalid_descriptor, image_mapped,
               sizeof(pixels_before_invalid_descriptor));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: two-UBO command begin failed\n");
            goto done;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &image_barrier);
        render_clear.color.float32[0] = 0.0f;
        render_clear.color.float32[1] = 0.0f;
        render_clear.color.float32[2] = 0.0f;
        render_clear.color.float32[3] = 1.0f;
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, two_uniform_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                two_uniform_pipeline_layout, 0u, 1u,
                                &two_uniform_set, 0u, NULL);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            memcmp(pixels_before_invalid_descriptor, image_mapped,
                   sizeof(pixels_before_invalid_descriptor)) != 0 ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            ((uint32_t *)image_mapped)[0] != expected_pixel ||
            ((uint32_t *)image_mapped)[3] != 0xff000000u ||
            vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
            write_text("vk_test: deferred two-UBO descriptor draw failed\n");
            write_hex32("vk_test: two-UBO pixel0=0x", ((uint32_t *)image_mapped)[0]);
            goto done;
        }
        {
            const float identity_matrix[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
            float translated_matrix[16];
            uint32_t identity_pixels[4];
            uint32_t malformed_matrix[sizeof(matrix_uniform_vertex_spv) /
                                      sizeof(matrix_uniform_vertex_spv[0])];
            VkShaderModule vector_matrix_shader = VK_NULL_HANDLE;
            VkPipeline vector_matrix_pipeline = VK_NULL_HANDLE;
            VkShaderModule malformed_module = VK_NULL_HANDLE;
            VkPipeline malformed_pipeline = VK_NULL_HANDLE;
            VkResult malformed_result;
            VkResult matrix_pipeline_result;
            int malformed_pipeline_accepted;
            memcpy((uint8_t *)mapped + 128u, identity_matrix, sizeof(identity_matrix));
            buffer_infos[0].range = 64u;
            vkUpdateDescriptorSets(device, 1u, &writes[0], 0u, NULL);
            shader_info.codeSize = sizeof(matrix_uniform_vertex_spv);
            shader_info.pCode = matrix_uniform_vertex_spv;
            if (vkCreateShaderModule(device, &shader_info, NULL,
                                     &two_uniform_vertex_shader) != VK_SUCCESS) {
                write_text("vk_test: mat4 UBO vertex module rejected\n");
                goto done;
            }
            shader_info.codeSize = sizeof(solid_fragment_spv);
            shader_info.pCode = solid_fragment_spv;
            if (vkCreateShaderModule(device, &shader_info, NULL,
                                     &two_uniform_shader) != VK_SUCCESS) {
                write_text("vk_test: solid fragment module rejected\n");
                goto done;
            }
            graphics_stages[0].module = two_uniform_vertex_shader;
            graphics_stages[1].module = two_uniform_shader;
            matrix_pipeline_result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u,
                &graphics_pipeline_info, NULL, &two_uniform_pipeline);
            if (matrix_pipeline_result != VK_SUCCESS) {
                write_text("vk_test: mat4 UBO pipeline rejected\n");
                write_hex32("vk_test: mat4 pipeline result=0x",
                            (uint32_t)matrix_pipeline_result);
                goto done;
            }
            memcpy(pixels_before_invalid_descriptor, image_mapped,
                   sizeof(pixels_before_invalid_descriptor));
            buffer_infos[0].range = 32u;
            vkUpdateDescriptorSets(device, 1u, &writes[0], 0u, NULL);
            if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
                vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
                write_text("vk_test: short mat4 UBO command begin failed\n");
                goto done;
            }
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                                 0u, NULL, 0u, NULL, 1u, &image_barrier);
            vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, two_uniform_pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    two_uniform_pipeline_layout, 0u, 1u,
                                    &two_uniform_set, 0u, NULL);
            vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
            vkCmdEndRenderPass(cmd);
            if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
                vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) !=
                    VK_ERROR_INITIALIZATION_FAILED ||
                memcmp(pixels_before_invalid_descriptor, image_mapped,
                       sizeof(pixels_before_invalid_descriptor)) != 0 ||
                vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
                write_text("vk_test: undersized mat4 UBO escaped submit preflight\n");
                goto done;
            }
            buffer_infos[0].range = 64u;
            vkUpdateDescriptorSets(device, 1u, &writes[0], 0u, NULL);
            if (vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
                write_text("vk_test: identity mat4 command begin failed\n");
                goto done;
            }
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                                 0u, NULL, 0u, NULL, 1u, &image_barrier);
            render_clear.color.float32[0] = 0.0f;
            render_clear.color.float32[1] = 0.0f;
            render_clear.color.float32[2] = 0.0f;
            render_clear.color.float32[3] = 1.0f;
            vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, two_uniform_pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    two_uniform_pipeline_layout, 0u, 1u,
                                    &two_uniform_set, 0u, NULL);
            vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
            vkCmdEndRenderPass(cmd);
            if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
                vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
                write_text("vk_test: identity mat4 deferred draw failed\n");
                goto done;
            }
            memcpy(identity_pixels, image_mapped, sizeof(identity_pixels));
            if (identity_pixels[0] == 0xff000000u) {
                write_text("vk_test: identity mat4 did not rasterize vertex triangle\n");
                goto done;
            }
            memcpy(malformed_matrix, matrix_uniform_vertex_spv,
                   sizeof(malformed_matrix));
            for (uint32_t offset = 5u;
                 offset < sizeof(malformed_matrix) / sizeof(malformed_matrix[0]);) {
                const uint32_t wc = malformed_matrix[offset] >> 16u;
                const uint32_t op = malformed_matrix[offset] & 0xffffu;
                if (wc == 0u || wc > sizeof(malformed_matrix) /
                                     sizeof(malformed_matrix[0]) - offset) break;
                if (op == 145u && wc == 5u) {
                    malformed_matrix[offset] = (5u << 16u) | 144u;
                    malformed_matrix[offset + 3u] = 25u; /* vector */
                    malformed_matrix[offset + 4u] = 33u; /* matrix */
                    break;
                }
                offset += wc;
            }
            shader_info.codeSize = sizeof(malformed_matrix);
            shader_info.pCode = malformed_matrix;
            if (vkCreateShaderModule(device, &shader_info, NULL,
                                     &vector_matrix_shader) != VK_SUCCESS) {
                write_text("vk_test: vector-times-matrix shader module rejected\n");
                goto done;
            }
            graphics_stages[0].module = vector_matrix_shader;
            if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u,
                    &graphics_pipeline_info, NULL, &vector_matrix_pipeline) != VK_SUCCESS) {
                graphics_stages[0].module = two_uniform_vertex_shader;
                vkDestroyShaderModule(device, vector_matrix_shader, NULL);
                write_text("vk_test: vector-times-matrix pipeline rejected\n");
                goto done;
            }
            graphics_stages[0].module = two_uniform_vertex_shader;
            if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
                vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
                vkDestroyPipeline(device, vector_matrix_pipeline, NULL);
                vkDestroyShaderModule(device, vector_matrix_shader, NULL);
                write_text("vk_test: vector-times-matrix command begin failed\n");
                goto done;
            }
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                                 0u, NULL, 0u, NULL, 1u, &image_barrier);
            vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vector_matrix_pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    two_uniform_pipeline_layout, 0u, 1u,
                                    &two_uniform_set, 0u, NULL);
            vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
            vkCmdEndRenderPass(cmd);
            if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
                vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
                memcmp(identity_pixels, image_mapped, sizeof(identity_pixels)) != 0) {
                vkDestroyPipeline(device, vector_matrix_pipeline, NULL);
                vkDestroyShaderModule(device, vector_matrix_shader, NULL);
                write_text("vk_test: deferred vector-times-matrix identity result mismatch\n");
                goto done;
            }
            vkDestroyPipeline(device, vector_matrix_pipeline, NULL);
            vkDestroyShaderModule(device, vector_matrix_shader, NULL);
            memcpy(translated_matrix, identity_matrix, sizeof(identity_matrix));
            translated_matrix[12] = 1.0f;
            memcpy((uint8_t *)mapped + 128u, translated_matrix,
                   sizeof(translated_matrix));
            if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
                vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
                write_text("vk_test: translated mat4 command begin failed\n");
                goto done;
            }
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                                 0u, NULL, 0u, NULL, 1u, &image_barrier);
            vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, two_uniform_pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    two_uniform_pipeline_layout, 0u, 1u,
                                    &two_uniform_set, 0u, NULL);
            vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
            vkCmdEndRenderPass(cmd);
            if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
                vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
                ((uint32_t *)image_mapped)[0] != 0xff000000u ||
                ((uint32_t *)image_mapped)[1] != 0xff000000u ||
                ((uint32_t *)image_mapped)[2] != 0xff000000u ||
                ((uint32_t *)image_mapped)[3] != 0xff000000u ||
                memcmp(identity_pixels, image_mapped, sizeof(identity_pixels)) == 0) {
                write_text("vk_test: deferred mat4 composition/translation result mismatch\n");
                goto done;
            }

            memcpy(malformed_matrix, matrix_uniform_vertex_spv,
                   sizeof(malformed_matrix));
            for (uint32_t offset = 5u;
                 offset < sizeof(malformed_matrix) / sizeof(malformed_matrix[0]);) {
                const uint32_t wc = malformed_matrix[offset] >> 16u;
                const uint32_t op = malformed_matrix[offset] & 0xffffu;
                if (wc == 0u || wc > sizeof(malformed_matrix) /
                                     sizeof(malformed_matrix[0]) - offset) break;
                if (op == 72u && wc == 5u && malformed_matrix[offset + 3u] == 7u) {
                    malformed_matrix[offset + 4u] = 8u;
                    break;
                }
                offset += wc;
            }
            shader_info.codeSize = sizeof(malformed_matrix);
            shader_info.pCode = malformed_matrix;
            if (vkCreateShaderModule(device, &shader_info, NULL,
                                     &malformed_module) != VK_SUCCESS) {
                write_text("vk_test: malformed mat4 module validation failed too early\n");
                goto done;
            }
            graphics_stages[0].module = malformed_module;
            malformed_result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u,
                &graphics_pipeline_info, NULL, &malformed_pipeline);
            malformed_pipeline_accepted = malformed_pipeline != VK_NULL_HANDLE;
            vkDestroyShaderModule(device, malformed_module, NULL);
            malformed_module = VK_NULL_HANDLE;
            graphics_stages[0].module = two_uniform_vertex_shader;
            if (malformed_pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(device, malformed_pipeline, NULL);
                malformed_pipeline = VK_NULL_HANDLE;
            }
            if (malformed_result != VK_ERROR_FEATURE_NOT_PRESENT ||
                malformed_pipeline_accepted != 0) {
                write_text("vk_test: unsupported mat4 layout was accepted\n");
                goto done;
            }
            {
                const float rgb[3] = {1.0f, 0.0f, 0.0f};
                VkShaderModule vec3_fragment = VK_NULL_HANDLE;
                VkPipeline vec3_pipeline = VK_NULL_HANDLE;
                VkResult short_range_result;
                VkResult vec3_pipeline_result;
                memcpy((uint8_t *)mapped + 128u, identity_matrix,
                       sizeof(identity_matrix));
                memcpy((uint8_t *)mapped + 160u, rgb, sizeof(rgb));
                shader_info.codeSize = sizeof(uniform_vec3_fragment_spv);
                shader_info.pCode = uniform_vec3_fragment_spv;
                if (vkCreateShaderModule(device, &shader_info, NULL,
                                         &vec3_fragment) != VK_SUCCESS) {
                    write_text("vk_test: vec3 UBO fragment module rejected\n");
                    goto done;
                }
                graphics_stages[0].module = two_uniform_vertex_shader;
                graphics_stages[1].module = vec3_fragment;
                vec3_pipeline_result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE,
                    1u, &graphics_pipeline_info, NULL, &vec3_pipeline);
                if (vec3_pipeline_result != VK_SUCCESS) {
                    graphics_stages[0].module = two_uniform_vertex_shader;
                    graphics_stages[1].module = two_uniform_shader;
                    vkDestroyShaderModule(device, vec3_fragment, NULL);
                    write_text("vk_test: vec3 UBO pipeline rejected\n");
                    write_hex32("vk_test: vec3 pipeline result=0x",
                                (uint32_t)vec3_pipeline_result);
                    goto done;
                }
                graphics_stages[0].module = two_uniform_vertex_shader;
                graphics_stages[1].module = two_uniform_shader;
                memcpy(pixels_before_invalid_descriptor, image_mapped,
                       sizeof(pixels_before_invalid_descriptor));
                buffer_infos[1].range = 12u;
                vkUpdateDescriptorSets(device, 1u, &writes[1], 0u, NULL);
                if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
                    vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
                    vkDestroyPipeline(device, vec3_pipeline, NULL);
                    vkDestroyShaderModule(device, vec3_fragment, NULL);
                    write_text("vk_test: short vec3 UBO command begin failed\n");
                    goto done;
                }
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                                     0u, NULL, 0u, NULL, 1u, &image_barrier);
                vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vec3_pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        two_uniform_pipeline_layout, 0u, 1u,
                                        &two_uniform_set, 0u, NULL);
                vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
                vkCmdEndRenderPass(cmd);
                if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
                    (short_range_result = vkQueueSubmit(queue, 1u, &submit,
                                            VK_NULL_HANDLE)) !=
                        VK_ERROR_INITIALIZATION_FAILED ||
                    memcmp(pixels_before_invalid_descriptor, image_mapped,
                           sizeof(pixels_before_invalid_descriptor)) != 0 ||
                    vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
                    vkDestroyPipeline(device, vec3_pipeline, NULL);
                    vkDestroyShaderModule(device, vec3_fragment, NULL);
                    write_text("vk_test: short vec3 UBO range escaped submit preflight\n");
                    goto done;
                }
                buffer_infos[1].range = 16u;
                vkUpdateDescriptorSets(device, 1u, &writes[1], 0u, NULL);
                if (vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
                    vkDestroyPipeline(device, vec3_pipeline, NULL);
                    vkDestroyShaderModule(device, vec3_fragment, NULL);
                    write_text("vk_test: vec3 UBO command begin failed\n");
                    goto done;
                }
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                                     0u, NULL, 0u, NULL, 1u, &image_barrier);
                vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vec3_pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        two_uniform_pipeline_layout, 0u, 1u,
                                        &two_uniform_set, 0u, NULL);
                vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
                vkCmdEndRenderPass(cmd);
                if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
                    vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
                    ((uint32_t *)image_mapped)[0] != 0xffff0000u) {
                    vkDestroyPipeline(device, vec3_pipeline, NULL);
                    vkDestroyShaderModule(device, vec3_fragment, NULL);
                    write_text("vk_test: deferred std140 vec3 UBO color mismatch\n");
                    goto done;
                }
                vkDestroyPipeline(device, vec3_pipeline, NULL);
                vkDestroyShaderModule(device, vec3_fragment, NULL);
                write_text("vk_test: deferred std140 vec3 UBO read and rounded range preflight ok\n");
            }
            vkDestroyPipeline(device, two_uniform_pipeline, NULL);
            two_uniform_pipeline = VK_NULL_HANDLE;
            vkDestroyShaderModule(device, two_uniform_shader, NULL);
            two_uniform_shader = VK_NULL_HANDLE;
            vkDestroyShaderModule(device, two_uniform_vertex_shader, NULL);
            two_uniform_vertex_shader = VK_NULL_HANDLE;
            write_text("vk_test: deferred column-major mat4 composition, transform, and bounds ok\n");
        }
        vkDestroyPipeline(device, two_uniform_pipeline, NULL);
        two_uniform_pipeline = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, two_uniform_shader, NULL);
        two_uniform_shader = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, two_uniform_vertex_shader, NULL);
        two_uniform_vertex_shader = VK_NULL_HANDLE;
        vkDestroyPipelineLayout(device, two_uniform_pipeline_layout, NULL);
        two_uniform_pipeline_layout = VK_NULL_HANDLE;
        vkDestroyDescriptorPool(device, two_uniform_pool, NULL);
        two_uniform_pool = VK_NULL_HANDLE;
        vkDestroyDescriptorSetLayout(device, two_uniform_layout, NULL);
        two_uniform_layout = VK_NULL_HANDLE;
        two_uniform_set = VK_NULL_HANDLE;
        write_text("vk_test: multiple UBO bindings and deferred shader reads ok\n");
    }
    {
        if (vkCreateShaderModule(device, &(VkShaderModuleCreateInfo){
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = sizeof(triangle_vertex_spv),
                .pCode = triangle_vertex_spv
            }, NULL, &loop_vertex_shader) != VK_SUCCESS) {
            write_text("vk_test: loop vertex shader rejected\n");
            goto done;
        }
        shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_info.codeSize = sizeof(loop_fragment_spv);
        shader_info.pCode = loop_fragment_spv;
        if (vkCreateShaderModule(device, &shader_info, NULL,
                                 &loop_fragment_shader) != VK_SUCCESS) {
            write_text("vk_test: structured-loop shader rejected\n");
            goto done;
        }
        graphics_stages[0].module = loop_vertex_shader;
        graphics_stages[1].module = loop_fragment_shader;
        graphics_pipeline_info.layout = graphics_layout;
        graphics_pipeline_info.pDynamicState = NULL;
        vertex_input.vertexBindingDescriptionCount = 0u;
        vertex_input.pVertexBindingDescriptions = NULL;
        vertex_input.vertexAttributeDescriptionCount = 0u;
        vertex_input.pVertexAttributeDescriptions = NULL;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u,
                                      &graphics_pipeline_info, NULL,
                                      &loop_pipeline) != VK_SUCCESS) {
            write_text("vk_test: structured-loop graphics pipeline rejected\n");
            goto done;
        }
        memcpy(pixels_before_invalid_descriptor, image_mapped,
               sizeof(pixels_before_invalid_descriptor));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: structured-loop command begin failed\n");
            goto done;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &image_barrier);
        render_clear.color.float32[0] = 0.0f;
        render_clear.color.float32[1] = 0.0f;
        render_clear.color.float32[2] = 0.0f;
        render_clear.color.float32[3] = 1.0f;
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, loop_pipeline);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            memcmp(pixels_before_invalid_descriptor, image_mapped,
                   sizeof(pixels_before_invalid_descriptor)) != 0 ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            ((uint32_t *)image_mapped)[0] != 0xbfbfbfbfu ||
            ((uint32_t *)image_mapped)[3] != 0xff000000u ||
            vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
            write_text("vk_test: deferred structured-loop execution failed\n");
            write_hex32("vk_test: loop pixel0=0x", ((uint32_t *)image_mapped)[0]);
            goto done;
        }
        vkDestroyPipeline(device, loop_pipeline, NULL);
        loop_pipeline = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, loop_fragment_shader, NULL);
        loop_fragment_shader = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, loop_vertex_shader, NULL);
        loop_vertex_shader = VK_NULL_HANDLE;
        write_text("vk_test: bounded structured SPIR-V loop deferred through submit ok\n");
    }
    /* An uninitialized but layout-compatible descriptor is rejected during
       submit preflight, before the clear or draw can mutate the target. */
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: invalid descriptor command begin failed\n");
        goto done;
    }
    memset(&image_barrier, 0, sizeof(image_barrier));
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = image;
    image_barrier.subresourceRange = image_range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                         0u, NULL, 0u, NULL, 1u, &image_barrier);
    render_clear.color.float32[0] = 1.0f;
    render_clear.color.float32[1] = 0.0f;
    render_clear.color.float32[2] = 0.0f;
    render_clear.color.float32[3] = 1.0f;
    memcpy(pixels_before_invalid_descriptor, image_mapped,
           sizeof(pixels_before_invalid_descriptor));
    vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_layout,
                            0u, 1u, &uninitialized_descriptor_set, 0u, NULL);
    vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_ERROR_INITIALIZATION_FAILED ||
        memcmp(pixels_before_invalid_descriptor, image_mapped,
               sizeof(pixels_before_invalid_descriptor)) != 0 ||
        vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
        write_text("vk_test: invalid descriptor changed render target\n");
        goto done;
    }
    render_clear.color.float32[0] = 0.0f;
    render_clear.color.float32[1] = 0.0f;
    render_clear.color.float32[2] = 0.0f;
    render_clear.color.float32[3] = 1.0f;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: push-constant command begin failed\n");
        goto done;
    }
    memset(&image_barrier, 0, sizeof(image_barrier));
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = image;
    image_barrier.subresourceRange = image_range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                         0u, NULL, 0u, NULL, 1u, &image_barrier);
    memcpy(pixels_before_invalid_descriptor, image_mapped,
           sizeof(pixels_before_invalid_descriptor));
    vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, push_pipeline);
    vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0u, sizeof(push_color), push_color);
    vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(cmd);
    push_color[0] = 0.0f;
    push_color[1] = 0.0f;
    push_color[2] = 0.0f;
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        memcmp(pixels_before_invalid_descriptor, image_mapped,
               sizeof(pixels_before_invalid_descriptor)) != 0 ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        ((uint32_t *)image_mapped)[0] != 0xffff4000u ||
        ((uint32_t *)image_mapped)[1] != 0xffff4000u ||
        ((uint32_t *)image_mapped)[2] != 0xff000000u ||
        ((uint32_t *)image_mapped)[3] != 0xff000000u) {
        write_text("vk_test: deferred push-constant shader result failed\n");
        goto done;
    }
    write_text("vk_test: deferred SPIR-V push/extract/shuffle stage operations ok\n");
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: varying command begin failed\n");
        goto done;
    }
    memset(&image_barrier, 0, sizeof(image_barrier));
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = image;
    image_barrier.subresourceRange = image_range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                         0u, NULL, 0u, NULL, 1u, &image_barrier);
    vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, varying_pipeline);
    vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        ((uint32_t *)image_mapped)[0] != 0xffff4000u ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        ((uint32_t *)image_mapped)[0] == ((uint32_t *)image_mapped)[1] ||
        ((uint32_t *)image_mapped)[0] == 0xff000000u ||
        ((uint32_t *)image_mapped)[1] == 0xff000000u ||
        ((uint32_t *)image_mapped)[2] != 0xff000000u ||
        ((uint32_t *)image_mapped)[3] != 0xff000000u) {
        write_text("vk_test: perspective-correct SPIR-V varying interpolation failed\n");
        goto done;
    }
    write_text("vk_test: deferred SPIR-V location-0/1 perspective interpolation ok\n");
    memcpy(mapped, vertex_positions, sizeof(vertex_positions));
    push_color[0] = 0.0f;
    push_color[1] = 1.0f;
    push_color[2] = 0.0f;
    render_clear.color.float32[0] = 0.0f;
    render_clear.color.float32[1] = 0.0f;
    render_clear.color.float32[2] = 0.0f;
    render_clear.color.float32[3] = 1.0f;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: vertex-buffer command begin failed\n");
        goto done;
    }
    memset(&image_barrier, 0, sizeof(image_barrier));
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = image;
    image_barrier.subresourceRange = image_range;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                         0u, NULL, 0u, NULL, 1u, &image_barrier);
    vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vertex_attribute_pipeline);
    VkDeviceSize vertex_offset = 0u;
    vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
    vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0u, sizeof(push_color), push_color);
    vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        ((uint32_t *)image_mapped)[0] != 0xff00ff00u ||
        ((uint32_t *)image_mapped)[1] != 0xff00ff00u ||
        ((uint32_t *)image_mapped)[2] != 0xff000000u ||
        ((uint32_t *)image_mapped)[3] != 0xff000000u) {
        write_text("vk_test: deferred vertex-buffer input/fetch failed\n");
        goto done;
    }
    write_text("vk_test: deferred SPIR-V vertex buffer input ok\n");
    {
        const uint32_t green = 0xff00ff00u;
        const uint32_t black = 0xff000000u;
        uint32_t previous_pixels[4];
        float *positions = (float *)mapped;
        push_color[0] = 0.0f;
        push_color[1] = 1.0f;
        push_color[2] = 0.0f;
        push_color[3] = 1.0f;
        render_clear.color.float32[0] = 0.0f;
        render_clear.color.float32[1] = 0.0f;
        render_clear.color.float32[2] = 0.0f;
        render_clear.color.float32[3] = 1.0f;

        /* One point at the center of the 2x2 target uses the lower pixel on
           the exact half-pixel tie, matching the renderer's documented rule. */
        positions[0] = 0.0f; positions[1] = 0.0f;
        positions[2] = 0.0f; positions[3] = 1.0f;
        positions[4] = 0.4f; positions[5] = 0.4f;
        positions[6] = 0.0f; positions[7] = 1.0f;
        memcpy(previous_pixels, image_mapped, sizeof(previous_pixels));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: point-list command begin failed\n");
            goto done;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
            0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, point_pipeline);
        vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
        vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0u, sizeof(push_color), push_color);
        vkCmdDraw(cmd, 2u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            memcmp(previous_pixels, image_mapped, sizeof(previous_pixels)) != 0 ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            ((uint32_t *)image_mapped)[0] != green ||
            ((uint32_t *)image_mapped)[1] != black ||
            ((uint32_t *)image_mapped)[2] != black ||
            ((uint32_t *)image_mapped)[3] != green) {
            write_text("vk_test: deferred point-list pixel coverage failed\n");
            goto done;
        }

        /* A horizontal line crossing the centers of the top row exercises
           pixel selection at both ends without relying on a synthetic image. */
        positions[0] = -0.75f; positions[1] = -0.5f;
        positions[4] =  0.75f; positions[5] = -0.5f;
        memcpy(previous_pixels, image_mapped, sizeof(previous_pixels));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: line-list command begin failed\n");
            goto done;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
            0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, line_list_pipeline);
        vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
        vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0u, sizeof(push_color), push_color);
        vkCmdDraw(cmd, 2u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            memcmp(previous_pixels, image_mapped, sizeof(previous_pixels)) != 0 ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            ((uint32_t *)image_mapped)[0] != green ||
            ((uint32_t *)image_mapped)[1] != green ||
            ((uint32_t *)image_mapped)[2] != black ||
            ((uint32_t *)image_mapped)[3] != black) {
            write_text("vk_test: deferred line-list pixel coverage failed\n");
            goto done;
        }

        positions[8] = 0.0f; positions[9] = -0.5f;
        memcpy(previous_pixels, image_mapped, sizeof(previous_pixels));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: line-strip command begin failed\n");
            goto done;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
            0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, line_strip_pipeline);
        vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
        vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0u, sizeof(push_color), push_color);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            memcmp(previous_pixels, image_mapped, sizeof(previous_pixels)) != 0 ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            ((uint32_t *)image_mapped)[0] != green ||
            ((uint32_t *)image_mapped)[1] != green ||
            ((uint32_t *)image_mapped)[2] != black ||
            ((uint32_t *)image_mapped)[3] != black) {
            write_text("vk_test: deferred line-strip pixel coverage failed\n");
            goto done;
        }
        memcpy(mapped, vertex_positions, sizeof(vertex_positions));
        write_text("vk_test: deferred point/line-list/line-strip pixels ok\n");
    }
    {
        uint32_t previous_pixels[4];
        memcpy(previous_pixels, image_mapped, sizeof(previous_pixels));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: vec3 vertex command begin failed\n");
            goto done;
        }
        memset(&image_barrier, 0, sizeof(image_barrier));
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.image = image;
        image_barrier.subresourceRange = image_range;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
            0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          vec3_attribute_pipeline);
        vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
        vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0u, sizeof(push_color), push_color);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            memcmp(previous_pixels, image_mapped, sizeof(previous_pixels)) != 0 ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            ((uint32_t *)image_mapped)[0] != 0xff00ff00u ||
            ((uint32_t *)image_mapped)[1] != 0xff00ff00u ||
            ((uint32_t *)image_mapped)[2] != 0xff000000u ||
            ((uint32_t *)image_mapped)[3] != 0xff000000u) {
            write_text("vk_test: vec3 vertex format/conversion failed\n");
            goto done;
        }
        write_text("vk_test: deferred vec3 R32G32B32 vertex fetch ok\n");
    }
    {
        const uint8_t normalized_vertices[60] = {
            [17] = 64u, [18] = 128u, [19] = 255u,
            [36] = 255u, [37] = 64u, [38] = 128u, [39] = 255u,
            [56] = 128u, [57] = 192u, [58] = 128u, [59] = 255u
        };
        uint32_t previous_pixels[4];
        uint32_t green_pixel_count = 0u;
        memcpy(mapped, normalized_vertices, sizeof(normalized_vertices));
        memcpy(previous_pixels, image_mapped, sizeof(previous_pixels));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: normalized vertex command begin failed\n");
            memcpy(mapped, vertex_positions, sizeof(vertex_positions));
            goto done;
        }
        memset(&image_barrier, 0, sizeof(image_barrier));
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.image = image;
        image_barrier.subresourceRange = image_range;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
            0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          normalized_attribute_pipeline);
        vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
        vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0u, sizeof(push_color), push_color);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            memcmp(previous_pixels, image_mapped, sizeof(previous_pixels)) != 0 ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
            write_text("vk_test: normalized vertex submit failed\n");
            memcpy(mapped, vertex_positions, sizeof(vertex_positions));
            goto done;
        }
        for (uint32_t pixel = 0u; pixel < 4u; ++pixel)
            if (((uint32_t *)image_mapped)[pixel] == 0xff00ff00u)
                ++green_pixel_count;
        if (green_pixel_count == 0u || green_pixel_count >= 4u) {
            write_text("vk_test: normalized byte vertex conversion failed\n");
            memcpy(mapped, vertex_positions, sizeof(vertex_positions));
            goto done;
        }
        memcpy(mapped, vertex_positions, sizeof(vertex_positions));
        write_text("vk_test: deferred normalized R8G8B8A8 vertex conversion ok\n");
    }
    {
        const float multi_vertices[24] = {
            0.0f, 0.0f, 0.0f, 0.0f, -0.75f, -0.75f, 0.5f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f,  0.75f, -0.75f, 0.5f, 1.0f,
            0.0f, 0.0f, 0.0f, 0.0f,  0.00f,  0.75f, 0.5f, 1.0f
        };
        uint32_t previous_pixels[4];
        memcpy(mapped, multi_vertices, sizeof(multi_vertices));
        memcpy(previous_pixels, image_mapped, sizeof(previous_pixels));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: multi-attribute command begin failed\n");
            memcpy(mapped, vertex_positions, sizeof(vertex_positions));
            goto done;
        }
        memset(&image_barrier, 0, sizeof(image_barrier));
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.image = image;
        image_barrier.subresourceRange = image_range;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
            0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          multi_attribute_pipeline);
        vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
        push_color[0] = 0.0f;
        push_color[1] = 1.0f;
        push_color[2] = 0.0f;
        vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0u, sizeof(push_color), push_color);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            memcmp(previous_pixels, image_mapped, sizeof(previous_pixels)) != 0 ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            ((uint32_t *)image_mapped)[0] != 0xff00ff00u ||
            ((uint32_t *)image_mapped)[1] != 0xff00ff00u ||
            ((uint32_t *)image_mapped)[2] != 0xff000000u ||
            ((uint32_t *)image_mapped)[3] != 0xff000000u) {
            write_text("vk_test: multi-location vertex attributes failed\n");
            memcpy(mapped, vertex_positions, sizeof(vertex_positions));
            goto done;
        }
        memcpy(mapped, vertex_positions, sizeof(vertex_positions));
        write_text("vk_test: deferred multi-location vertex fetch ok\n");
    }
    {
        const float clipped_positions[12] = {
            -2.0f, -0.5f, 0.5f, 1.0f,
             0.5f, -0.5f, 0.5f, 1.0f,
             0.5f,  0.5f, 0.5f, 1.0f
        };
        uint32_t previous_pixels[4];
        uint32_t green_pixels = 0u;
        memcpy(mapped, clipped_positions, sizeof(clipped_positions));
        memcpy(previous_pixels, image_mapped, sizeof(previous_pixels));
        render_clear.color.float32[0] = 0.0f;
        render_clear.color.float32[1] = 0.0f;
        render_clear.color.float32[2] = 0.0f;
        render_clear.color.float32[3] = 1.0f;
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: clip command begin failed\n");
            memcpy(mapped, vertex_positions, sizeof(vertex_positions));
            goto done;
        }
        memset(&image_barrier, 0, sizeof(image_barrier));
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.image = image;
        image_barrier.subresourceRange = image_range;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vertex_attribute_pipeline);
        vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
        vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0u, sizeof(push_color), push_color);
        vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            memcmp(previous_pixels, image_mapped, sizeof(previous_pixels)) != 0 ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
            write_text("vk_test: clipped triangle was not deferred/rendered\n");
            memcpy(mapped, vertex_positions, sizeof(vertex_positions));
            goto done;
        }
        for (uint32_t i = 0u; i < 4u; ++i)
            if (((uint32_t *)image_mapped)[i] == 0xff00ff00u) ++green_pixels;
        memcpy(mapped, vertex_positions, sizeof(vertex_positions));
        if (green_pixels == 0u) {
            write_text("vk_test: partially clipped triangle disappeared\n");
            goto done;
        }
        write_text("vk_test: deferred homogeneous triangle clipping ok\n");
    }
    {
        const float quad_positions[16] = {
            -1.0f, -1.0f, 0.5f, 1.0f,
             1.0f, -1.0f, 0.5f, 1.0f,
            -1.0f,  1.0f, 0.5f, 1.0f,
             1.0f,  1.0f, 0.5f, 1.0f
        };
        VkShaderModuleCreateInfo module_info = {0};
        VkShaderModule edge_vertex_shader = VK_NULL_HANDLE;
        VkShaderModule edge_fragment_shader = VK_NULL_HANDLE;
        VkPipeline edge_pipeline = VK_NULL_HANDLE;
        VkPipeline cull_front_pipeline = VK_NULL_HANDLE;
        VkShaderModule saved_module0 = graphics_stages[0].module;
        VkShaderModule saved_module1 = graphics_stages[1].module;
        VkVertexInputBindingDescription edge_binding = {0};
        VkVertexInputAttributeDescription edge_attribute = {0};
        uint32_t previous_pixels[4];
        float saved_push_color[4];
        uint32_t valid = 1u;
        memcpy(saved_push_color, push_color, sizeof(saved_push_color));
        module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module_info.codeSize = sizeof(vertex_attribute_spv);
        module_info.pCode = vertex_attribute_spv;
        if (vkCreateShaderModule(device, &module_info, NULL, &edge_vertex_shader) != VK_SUCCESS) {
            write_text("vk_test: shared-edge vertex shader creation failed\n");
            goto done;
        }
        module_info.codeSize = sizeof(push_constant_fragment_spv);
        module_info.pCode = push_constant_fragment_spv;
        if (vkCreateShaderModule(device, &module_info, NULL, &edge_fragment_shader) != VK_SUCCESS) {
            vkDestroyShaderModule(device, edge_vertex_shader, NULL);
            write_text("vk_test: shared-edge fragment shader creation failed\n");
            goto done;
        }
        graphics_stages[0].module = edge_vertex_shader;
        graphics_stages[1].module = edge_fragment_shader;
        edge_binding.binding = 0u;
        edge_binding.stride = sizeof(float) * 4u;
        edge_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        edge_attribute.location = 0u;
        edge_attribute.binding = 0u;
        edge_attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        vertex_input.vertexBindingDescriptionCount = 1u;
        vertex_input.pVertexBindingDescriptions = &edge_binding;
        vertex_input.vertexAttributeDescriptionCount = 1u;
        vertex_input.pVertexAttributeDescriptions = &edge_attribute;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        color_attachment_state.blendEnable = VK_TRUE;
        color_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        color_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_attachment_state.colorBlendOp = VK_BLEND_OP_ADD;
        color_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color_attachment_state.alphaBlendOp = VK_BLEND_OP_ADD;
        rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                      NULL, &edge_pipeline) != VK_SUCCESS) valid = 0u;
        rasterization.cullMode = VK_CULL_MODE_FRONT_BIT;
        if (valid != 0u &&
            vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                      NULL, &cull_front_pipeline) != VK_SUCCESS) valid = 0u;
        graphics_stages[0].module = saved_module0;
        graphics_stages[1].module = saved_module1;
        vertex_input.vertexBindingDescriptionCount = 0u;
        vertex_input.pVertexBindingDescriptions = NULL;
        vertex_input.vertexAttributeDescriptionCount = 0u;
        vertex_input.pVertexAttributeDescriptions = NULL;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        color_attachment_state.blendEnable = VK_FALSE;
        color_attachment_state.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_attachment_state.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_attachment_state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        color_attachment_state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        if (valid == 0u) {
            vkDestroyPipeline(device, edge_pipeline, NULL);
            vkDestroyPipeline(device, cull_front_pipeline, NULL);
            vkDestroyShaderModule(device, edge_fragment_shader, NULL);
            vkDestroyShaderModule(device, edge_vertex_shader, NULL);
            write_text("vk_test: shared-edge alpha pipeline rejected\n");
            goto done;
        }
        memcpy(mapped, quad_positions, sizeof(quad_positions));
        memcpy(previous_pixels, image_mapped, sizeof(previous_pixels));
        push_color[0] = 1.0f;
        push_color[1] = 0.0f;
        push_color[2] = 0.0f;
        push_color[3] = 0.5f;
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) valid = 0u;
        if (valid != 0u) {
            memset(&image_barrier, 0, sizeof(image_barrier));
            image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            image_barrier.image = image;
            image_barrier.subresourceRange = image_range;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                                 0u, NULL, 0u, NULL, 1u, &image_barrier);
            render_clear.color.float32[0] = 0.0f;
            render_clear.color.float32[1] = 0.0f;
            render_clear.color.float32[2] = 0.0f;
            render_clear.color.float32[3] = 1.0f;
            vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, edge_pipeline);
            vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
            vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                               0u, sizeof(push_color), push_color);
            vkCmdDraw(cmd, 4u, 1u, 0u, 0u);
            vkCmdEndRenderPass(cmd);
            if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
                memcmp(previous_pixels, image_mapped, sizeof(previous_pixels)) != 0 ||
                vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS)
                valid = 0u;
        }
        memcpy(mapped, vertex_positions, sizeof(vertex_positions));
        if (valid == 0u ||
            ((uint32_t *)image_mapped)[0] != 0xff800000u ||
            ((uint32_t *)image_mapped)[1] != 0xff800000u ||
            ((uint32_t *)image_mapped)[2] != 0xff800000u ||
            ((uint32_t *)image_mapped)[3] != 0xff800000u) {
            write_text("vk_test: shared triangle edge shaded more than once or lost coverage\n");
            write_hex32("vk_test: edge pixel0=0x", ((uint32_t *)image_mapped)[0]);
            vkDestroyPipeline(device, edge_pipeline, NULL);
            vkDestroyPipeline(device, cull_front_pipeline, NULL);
            vkDestroyShaderModule(device, edge_fragment_shader, NULL);
            vkDestroyShaderModule(device, edge_vertex_shader, NULL);
            goto done;
        }
        write_text("vk_test: exclusive top-left coverage across blended strip ok\n");
        memcpy(mapped, quad_positions, sizeof(quad_positions));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: front-face cull command begin failed\n");
            vkDestroyPipeline(device, edge_pipeline, NULL);
            vkDestroyPipeline(device, cull_front_pipeline, NULL);
            vkDestroyShaderModule(device, edge_fragment_shader, NULL);
            vkDestroyShaderModule(device, edge_vertex_shader, NULL);
            goto done;
        }
        memset(&image_barrier, 0, sizeof(image_barrier));
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.image = image;
        image_barrier.subresourceRange = image_range;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, cull_front_pipeline);
        vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
        vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0u, sizeof(push_color), push_color);
        vkCmdDraw(cmd, 4u, 1u, 0u, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            ((uint32_t *)image_mapped)[0] != 0xff000000u ||
            ((uint32_t *)image_mapped)[1] != 0xff000000u ||
            ((uint32_t *)image_mapped)[2] != 0xff000000u ||
            ((uint32_t *)image_mapped)[3] != 0xff000000u) {
            write_text("vk_test: front-face culling did not discard the strip\n");
            write_hex32("vk_test: culled pixel0=0x", ((uint32_t *)image_mapped)[0]);
            write_hex32("vk_test: culled pixel1=0x", ((uint32_t *)image_mapped)[1]);
            write_hex32("vk_test: culled pixel2=0x", ((uint32_t *)image_mapped)[2]);
            write_hex32("vk_test: culled pixel3=0x", ((uint32_t *)image_mapped)[3]);
            vkDestroyPipeline(device, edge_pipeline, NULL);
            vkDestroyPipeline(device, cull_front_pipeline, NULL);
            vkDestroyShaderModule(device, edge_fragment_shader, NULL);
            vkDestroyShaderModule(device, edge_vertex_shader, NULL);
            goto done;
        }
        vkDestroyPipeline(device, edge_pipeline, NULL);
        vkDestroyPipeline(device, cull_front_pipeline, NULL);
        vkDestroyShaderModule(device, edge_fragment_shader, NULL);
        vkDestroyShaderModule(device, edge_vertex_shader, NULL);
        memcpy(mapped, vertex_positions, sizeof(vertex_positions));
        memcpy(push_color, saved_push_color, sizeof(push_color));
        write_text("vk_test: front-face culling and strip winding ok\n");
    }
    {
        VkImageCreateInfo depth_image_info = {0};
        VkImage depth_image = VK_NULL_HANDLE;
        VkDeviceMemory depth_memory = VK_NULL_HANDLE;
        VkMemoryRequirements depth_requirements = {0};
        VkImageMemoryBarrier depth_clear_barrier = {0};
        VkImageSubresourceRange depth_clear_range = {0};
        VkClearDepthStencilValue depth_clear = {0};
        void *depth_mapped = NULL;
        VkImageViewCreateInfo depth_view_info = {0};
        VkImageView depth_view = VK_NULL_HANDLE;
        VkAttachmentDescription depth_attachments[2] = {{0}, {0}};
        VkAttachmentReference depth_color_ref = {0};
        VkAttachmentReference depth_attachment_ref = {0};
        VkSubpassDescription depth_subpass = {0};
        VkRenderPassCreateInfo depth_render_pass_info = {0};
        VkRenderPass depth_render_pass = VK_NULL_HANDLE;
        VkFramebufferCreateInfo depth_framebuffer_info = {0};
        VkImageView depth_framebuffer_attachments[2] = {image_view, VK_NULL_HANDLE};
        VkFramebuffer depth_framebuffer = VK_NULL_HANDLE;
        VkPipelineDepthStencilStateCreateInfo depth_state = {0};
        VkPipeline depth_pipeline = VK_NULL_HANDLE;
        VkShaderModule depth_vertex_shader = VK_NULL_HANDLE;
        VkShaderModule depth_fragment_shader = VK_NULL_HANDLE;
        VkVertexInputBindingDescription depth_binding = {0};
        VkVertexInputAttributeDescription depth_attribute = {0};
        VkClearValue depth_clears[2] = {{0}, {0}};
        VkDeviceSize depth_vertex_offset = 0u;
        VkShaderModule saved_depth_vertex = graphics_stages[0].module;
        VkShaderModule saved_depth_fragment = graphics_stages[1].module;
        VkRenderPass saved_render_pass = render_begin.renderPass;
        VkFramebuffer saved_framebuffer = render_begin.framebuffer;
        uint32_t saved_clear_value_count = render_begin.clearValueCount;
        const VkClearValue *saved_clear_values = render_begin.pClearValues;
        float saved_push_color[4];
        uint32_t previous_depth_pixels[4];
        uint32_t depth_ok = 1u;

        memcpy(saved_push_color, push_color, sizeof(saved_push_color));

        depth_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depth_image_info.imageType = VK_IMAGE_TYPE_2D;
        depth_image_info.format = VK_FORMAT_D32_SFLOAT;
        depth_image_info.extent.width = 2u;
        depth_image_info.extent.height = 2u;
        depth_image_info.extent.depth = 1u;
        depth_image_info.mipLevels = 1u;
        depth_image_info.arrayLayers = 1u;
        depth_image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        depth_image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        depth_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(device, &depth_image_info, NULL, &depth_image) != VK_SUCCESS)
            depth_ok = 0u;
        if (depth_ok != 0u) {
            vkGetImageMemoryRequirements(device, depth_image, &depth_requirements);
            memory_info.allocationSize = depth_requirements.size;
            memory_info.memoryTypeIndex = 0u;
            if (depth_requirements.size < 16u || depth_requirements.memoryTypeBits == 0u ||
                vkAllocateMemory(device, &memory_info, NULL, &depth_memory) != VK_SUCCESS ||
                vkBindImageMemory(device, depth_image, depth_memory, 0u) != VK_SUCCESS)
                depth_ok = 0u;
        }
        depth_clear_range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depth_clear_range.levelCount = 1u;
        depth_clear_range.layerCount = 1u;
        depth_clear_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depth_clear_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        depth_clear_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_clear_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        depth_clear_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depth_clear_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depth_clear_barrier.image = depth_image;
        depth_clear_barrier.subresourceRange = depth_clear_range;
        depth_clear.depth = 0.25f;
        cmd_begin.flags = 0u;
        if (depth_ok != 0u && (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS)) depth_ok = 0u;
        if (depth_ok != 0u) {
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL,
                1u, &depth_clear_barrier);
            vkCmdClearDepthStencilImage(cmd, depth_image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &depth_clear, 1u,
                &depth_clear_range);
            if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
                vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
                vkMapMemory(device, depth_memory, 0u,
                    depth_requirements.size, 0u, &depth_mapped) != VK_SUCCESS) {
                depth_ok = 0u;
            } else {
                const float *depth_values = (const float *)depth_mapped;
                for (uint32_t pixel = 0u; pixel < 4u; ++pixel) {
                    if (depth_values[pixel] != 0.25f) depth_ok = 0u;
                }
                vkUnmapMemory(device, depth_memory);
                depth_mapped = NULL;
            }
        }
        if (depth_ok == 0u) {
            write_text("vk_test: deferred D32 transfer clear or depth readback failed\n");
            goto depth_test_cleanup;
        }
        write_text("vk_test: deferred D32 depth-aspect clear and transfer layout ok\n");
        depth_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depth_view_info.image = depth_image;
        depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depth_view_info.format = VK_FORMAT_D32_SFLOAT;
        depth_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depth_view_info.subresourceRange.levelCount = 1u;
        depth_view_info.subresourceRange.layerCount = 1u;
        if (depth_ok != 0u) {
            depth_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            if (vkCreateImageView(device, &depth_view_info, NULL, &depth_view) == VK_SUCCESS ||
                depth_view != VK_NULL_HANDLE) depth_ok = 0u;
            depth_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (depth_ok != 0u &&
                vkCreateImageView(device, &depth_view_info, NULL, &depth_view) != VK_SUCCESS)
                depth_ok = 0u;
        }
        depth_attachments[0].format = VK_FORMAT_B8G8R8A8_UNORM;
        depth_attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        depth_attachments[1].format = VK_FORMAT_D32_SFLOAT;
        depth_attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        depth_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth_attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_color_ref.attachment = 0u;
        depth_color_ref.layout = VK_IMAGE_LAYOUT_GENERAL;
        depth_attachment_ref.attachment = 1u;
        depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        depth_subpass.colorAttachmentCount = 1u;
        depth_subpass.pColorAttachments = &depth_color_ref;
        depth_subpass.pDepthStencilAttachment = &depth_attachment_ref;
        depth_render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        depth_render_pass_info.attachmentCount = 2u;
        depth_render_pass_info.pAttachments = depth_attachments;
        depth_render_pass_info.subpassCount = 1u;
        depth_render_pass_info.pSubpasses = &depth_subpass;
        if (depth_ok != 0u &&
            vkCreateRenderPass(device, &depth_render_pass_info, NULL, &depth_render_pass) != VK_SUCCESS)
            depth_ok = 0u;
        depth_framebuffer_attachments[1] = depth_view;
        depth_framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        depth_framebuffer_info.renderPass = depth_render_pass;
        depth_framebuffer_info.attachmentCount = 2u;
        depth_framebuffer_info.pAttachments = depth_framebuffer_attachments;
        depth_framebuffer_info.width = 2u;
        depth_framebuffer_info.height = 2u;
        depth_framebuffer_info.layers = 1u;
        if (depth_ok != 0u && vkCreateFramebuffer(device, &depth_framebuffer_info, NULL,
                                                  &depth_framebuffer) != VK_SUCCESS)
            depth_ok = 0u;

        shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader_info.codeSize = sizeof(vertex_attribute_spv);
        shader_info.pCode = vertex_attribute_spv;
        if (depth_ok != 0u && vkCreateShaderModule(device, &shader_info, NULL,
                                                    &depth_vertex_shader) != VK_SUCCESS)
            depth_ok = 0u;
        shader_info.codeSize = sizeof(push_constant_fragment_spv);
        shader_info.pCode = push_constant_fragment_spv;
        if (depth_ok != 0u && vkCreateShaderModule(device, &shader_info, NULL,
                                                    &depth_fragment_shader) != VK_SUCCESS)
            depth_ok = 0u;
        depth_binding.binding = 0u;
        depth_binding.stride = sizeof(float) * 4u;
        depth_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        depth_attribute.location = 0u;
        depth_attribute.binding = 0u;
        depth_attribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        vertex_input.vertexBindingDescriptionCount = 1u;
        vertex_input.pVertexBindingDescriptions = &depth_binding;
        vertex_input.vertexAttributeDescriptionCount = 1u;
        vertex_input.pVertexAttributeDescriptions = &depth_attribute;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        graphics_stages[0].module = depth_vertex_shader;
        graphics_stages[1].module = depth_fragment_shader;
        graphics_pipeline_info.renderPass = depth_render_pass;
        depth_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth_state.depthTestEnable = VK_TRUE;
        depth_state.depthWriteEnable = VK_TRUE;
        depth_state.depthCompareOp = VK_COMPARE_OP_LESS;
        graphics_pipeline_info.pDepthStencilState = &depth_state;
        if (depth_ok != 0u && vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u,
                &graphics_pipeline_info, NULL, &depth_pipeline) != VK_SUCCESS)
            depth_ok = 0u;

        if (depth_ok != 0u) {
            memcpy(previous_depth_pixels, image_mapped, sizeof(previous_depth_pixels));
            depth_clears[0].color.float32[3] = 1.0f;
            depth_clears[1].depthStencil.depth = 1.0f;
            render_begin.renderPass = depth_render_pass;
            render_begin.framebuffer = depth_framebuffer;
            render_begin.clearValueCount = 2u;
            render_begin.pClearValues = depth_clears;
            if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
                vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) depth_ok = 0u;
            if (depth_ok != 0u) {
                vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_pipeline);
                VkDeviceSize vertex_offset = 0u;
                vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
                push_color[0] = 0.0f; push_color[1] = 1.0f;
                push_color[2] = 0.0f; push_color[3] = 1.0f;
                vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0u, sizeof(push_color), push_color);
                vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
                push_color[0] = 1.0f; push_color[1] = 0.0f;
                push_color[2] = 0.0f; push_color[3] = 1.0f;
                vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0u, sizeof(push_color), push_color);
                vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
                vkCmdEndRenderPass(cmd);
                if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
                    memcmp(previous_depth_pixels, image_mapped, sizeof(previous_depth_pixels)) != 0 ||
                    vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
                    ((uint32_t *)image_mapped)[0] != 0xff00ff00u ||
                    ((uint32_t *)image_mapped)[1] != 0xff00ff00u ||
                    ((uint32_t *)image_mapped)[2] != 0xff000000u ||
                    ((uint32_t *)image_mapped)[3] != 0xff000000u)
                    depth_ok = 0u;
            }
        }
depth_test_cleanup:
        if (depth_mapped != NULL) vkUnmapMemory(device, depth_memory);
        (void)vkResetCommandBuffer(cmd, 0u);
        vkDestroyPipeline(device, depth_pipeline, NULL);
        vkDestroyShaderModule(device, depth_fragment_shader, NULL);
        vkDestroyShaderModule(device, depth_vertex_shader, NULL);
        vkDestroyFramebuffer(device, depth_framebuffer, NULL);
        vkDestroyRenderPass(device, depth_render_pass, NULL);
        vkDestroyImageView(device, depth_view, NULL);
        vkDestroyImage(device, depth_image, NULL);
        vkFreeMemory(device, depth_memory, NULL);
        graphics_pipeline_info.pDepthStencilState = NULL;
        graphics_pipeline_info.renderPass = render_pass;
        graphics_stages[0].module = saved_depth_vertex;
        graphics_stages[1].module = saved_depth_fragment;
        render_begin.renderPass = saved_render_pass;
        render_begin.framebuffer = saved_framebuffer;
        render_begin.clearValueCount = saved_clear_value_count;
        render_begin.pClearValues = saved_clear_values;
        memcpy(push_color, saved_push_color, sizeof(push_color));
        vertex_input.vertexBindingDescriptionCount = 0u;
        vertex_input.pVertexBindingDescriptions = NULL;
        vertex_input.vertexAttributeDescriptionCount = 0u;
        vertex_input.pVertexAttributeDescriptions = NULL;
        if (depth_ok == 0u) {
            write_text("vk_test: deferred D32 depth clear/test/write failed\n");
            goto done;
        }
        write_text("vk_test: deferred D32 depth clear/LESS/write ok\n");
    }
    {
        VkImageCreateInfo rgba_info = {0};
        VkImage rgba_image = VK_NULL_HANDLE;
        VkDeviceMemory rgba_memory = VK_NULL_HANDLE;
        VkImageMemoryBarrier rgba_barriers[2] = {{0}, {0}};
        VkImageBlit rgba_blit = {0};
        VkClearColorValue rgba_clear = {{0}};
        VkImageSubresourceRange rgba_range = {0};
        void *rgba_mapped = NULL;
        uint32_t previous_rgba_pixels[4];
        uint32_t rgba_ok = 1u;
        rgba_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        rgba_info.imageType = VK_IMAGE_TYPE_2D;
        rgba_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        rgba_info.extent.width = 2u;
        rgba_info.extent.height = 2u;
        rgba_info.extent.depth = 1u;
        rgba_info.mipLevels = 1u;
        rgba_info.arrayLayers = 1u;
        rgba_info.samples = VK_SAMPLE_COUNT_1_BIT;
        rgba_info.tiling = VK_IMAGE_TILING_LINEAR;
        rgba_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        rgba_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        rgba_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &rgba_info, NULL, &rgba_image) != VK_SUCCESS)
            rgba_ok = 0u;
        if (rgba_ok != 0u) {
            vkGetImageMemoryRequirements(device, rgba_image, &requirements);
            memory_info.allocationSize = requirements.size;
            memory_info.memoryTypeIndex = 0u;
            if (requirements.size != 16u || requirements.memoryTypeBits == 0u ||
                vkAllocateMemory(device, &memory_info, NULL, &rgba_memory) != VK_SUCCESS ||
                vkBindImageMemory(device, rgba_image, rgba_memory, 0u) != VK_SUCCESS ||
                vkMapMemory(device, rgba_memory, 0u, 16u, 0u, &rgba_mapped) != VK_SUCCESS)
                rgba_ok = 0u;
        }
        if (rgba_ok != 0u) {
            memcpy(previous_rgba_pixels, image_mapped, sizeof(previous_rgba_pixels));
            rgba_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            rgba_range.levelCount = 1u;
            rgba_range.layerCount = 1u;
            rgba_barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            rgba_barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            rgba_barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            rgba_barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            rgba_barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            rgba_barriers[0].image = rgba_image;
            rgba_barriers[0].subresourceRange = rgba_range;
            rgba_barriers[1] = rgba_barriers[0];
            rgba_barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            rgba_barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            rgba_clear.float32[0] = 1.0f;
            rgba_clear.float32[3] = 1.0f;
            rgba_blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            rgba_blit.srcSubresource.layerCount = 1u;
            rgba_blit.dstSubresource = rgba_blit.srcSubresource;
            rgba_blit.srcOffsets[1].x = 2;
            rgba_blit.srcOffsets[1].y = 2;
            rgba_blit.srcOffsets[1].z = 1;
            rgba_blit.dstOffsets[1] = rgba_blit.srcOffsets[1];
            if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
                vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) rgba_ok = 0u;
        }
        if (rgba_ok != 0u) {
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL, 1u,
                &rgba_barriers[0]);
            vkCmdClearColorImage(cmd, rgba_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 &rgba_clear, 1u, &rgba_range);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL, 1u,
                &rgba_barriers[1]);
            vkCmdBlitImage(cmd, rgba_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image, VK_IMAGE_LAYOUT_GENERAL, 1u, &rgba_blit, VK_FILTER_NEAREST);
            if (memcmp(previous_rgba_pixels, image_mapped, sizeof(previous_rgba_pixels)) != 0 ||
                vkEndCommandBuffer(cmd) != VK_SUCCESS ||
                vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
                ((uint32_t *)rgba_mapped)[0] != 0xff0000ffu ||
                ((uint32_t *)image_mapped)[0] != 0xffff0000u ||
                ((uint32_t *)image_mapped)[3] != 0xffff0000u)
                rgba_ok = 0u;
        }
        (void)vkResetCommandBuffer(cmd, 0u);
        if (rgba_mapped != NULL) {
            vkUnmapMemory(device, rgba_memory);
        }
        vkDestroyImage(device, rgba_image, NULL);
        vkFreeMemory(device, rgba_memory, NULL);
        if (rgba_ok == 0u) {
            write_text("vk_test: deferred RGBA clear and cross-format blit failed\n");
            goto done;
        }
        write_text("vk_test: deferred RGBA storage and BGRA cross-format blit ok\n");
    }
    {
        VkBufferCreateInfo index_info = {0};
        VkDeviceMemory index_mem = VK_NULL_HANDLE;
        VkMemoryRequirements index_requirements = {0};
        VkDeviceSize index_offset = 0u;
        uint16_t *indices;
        uint32_t previous_pixels[4];

        index_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        index_info.size = 64u;
        index_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        index_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &index_info, NULL, &index_buffer) != VK_SUCCESS) {
            write_text("vk_test: index buffer creation failed\n");
            goto done;
        }
        vkGetBufferMemoryRequirements(device, index_buffer, &index_requirements);
        memory_info.allocationSize = index_requirements.size;
        if (vkAllocateMemory(device, &memory_info, NULL, &index_mem) != VK_SUCCESS ||
            vkBindBufferMemory(device, index_buffer, index_mem, 0u) != VK_SUCCESS ||
            vkMapMemory(device, index_mem, 0u, VK_WHOLE_SIZE, 0u, &index_mapped) != VK_SUCCESS ||
            index_mapped == NULL) {
            if (index_mem != VK_NULL_HANDLE) vkFreeMemory(device, index_mem, NULL);
            write_text("vk_test: index memory setup failed\n");
            goto done;
        }
        index_memory = index_mem;
        indices = (uint16_t *)index_mapped;
        indices[0] = 0u;
        indices[1] = 2u;
        indices[2] = 1u;
        indices[3] = UINT16_MAX;
        memcpy(previous_pixels, image_mapped, sizeof(previous_pixels));
        if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
            vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: indexed bounds command begin failed\n");
            goto done;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, strip_pipeline);
        vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
        vkCmdBindIndexBuffer(cmd, index_buffer, index_offset, VK_INDEX_TYPE_UINT16);
        vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0u, sizeof(push_color), push_color);
        vkCmdDrawIndexed(cmd, 3u, 1u, 1u, 0, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) !=
                VK_ERROR_INITIALIZATION_FAILED ||
            memcmp(previous_pixels, image_mapped, sizeof(previous_pixels)) != 0 ||
            vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
            write_text("vk_test: invalid indexed fetch mutated target or succeeded\n");
            goto done;
        }

        indices[3] = 0u;
        if (vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
            write_text("vk_test: indexed draw command begin failed\n");
            goto done;
        }
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                             0u, NULL, 0u, NULL, 1u, &image_barrier);
        vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, fan_pipeline);
        vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
        vkCmdBindIndexBuffer(cmd, index_buffer, index_offset, VK_INDEX_TYPE_UINT16);
        vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0u, sizeof(push_color), push_color);
        vkCmdDrawIndexed(cmd, 3u, 1u, 1u, 0, 0u);
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
            memcmp(previous_pixels, image_mapped, sizeof(previous_pixels)) != 0) {
            write_text("vk_test: indexed draw executed during recording\n");
            goto done;
        }
        vkUnmapMemory(device, index_mem);
        index_mapped = NULL;
        /* Recorded draw and bound state retain the index backing through submit. */
        vkDestroyBuffer(device, index_buffer, NULL);
        index_buffer = VK_NULL_HANDLE;
        vkFreeMemory(device, index_mem, NULL);
        index_memory = VK_NULL_HANDLE;
        if (vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            ((uint32_t *)image_mapped)[0] != 0xff00ff00u ||
            ((uint32_t *)image_mapped)[1] != 0xff00ff00u ||
            ((uint32_t *)image_mapped)[2] != 0xff000000u ||
            ((uint32_t *)image_mapped)[3] != 0xff000000u ||
            vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
            write_text("vk_test: deferred indexed draw/lifetime failed\n");
            goto done;
        }
    }
    write_text("vk_test: deferred indexed list/strip/fan draw and bounds preflight ok\n");
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: vertex range preflight begin failed\n");
        goto done;
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                         0u, NULL, 0u, NULL, 1u, &image_barrier);
    vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vertex_attribute_pipeline);
    vkCmdBindVertexBuffers(cmd, 0u, 1u, &buffer, &vertex_offset);
    vkCmdPushConstants(cmd, graphics_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0u, sizeof(push_color), push_color);
    vkCmdDraw(cmd, 3u, 1u, 256u, 0u);
    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_ERROR_INITIALIZATION_FAILED ||
        ((uint32_t *)image_mapped)[0] != 0xff00ff00u ||
        ((uint32_t *)image_mapped)[1] != 0xff00ff00u ||
        ((uint32_t *)image_mapped)[2] != 0xff000000u ||
        ((uint32_t *)image_mapped)[3] != 0xff000000u ||
        vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
        write_text("vk_test: out-of-range vertex buffer mutated target or succeeded\n");
        goto done;
    }
    write_text("vk_test: vertex fetch bounds preflight before render mutation ok\n");
    ((uint32_t *)copied_image_mapped)[0] = 0xffff0000u;
    ((uint32_t *)copied_image_mapped)[1] = 0xff00ff00u;
    ((uint32_t *)copied_image_mapped)[2] = 0xff0000ffu;
    ((uint32_t *)copied_image_mapped)[3] = 0xffffffffu;
    texture_view_info = image_view_info;
    texture_view_info.image = copied_image;
    if (vkCreateImageView(device, &texture_view_info, NULL, &texture_view) != VK_SUCCESS) {
        write_text("vk_test: sampled texture view creation failed\n");
        goto done;
    }
    texture_sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    texture_sampler_info.magFilter = VK_FILTER_NEAREST;
    texture_sampler_info.minFilter = VK_FILTER_NEAREST;
    texture_sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    texture_sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    texture_sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    texture_sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    texture_sampler_info.maxAnisotropy = 1.0f;
    texture_sampler_info.maxLod = 0.0f;
    texture_sampler_info.magFilter = VK_FILTER_LINEAR;
    if (vkCreateSampler(device, &texture_sampler_info, NULL, &texture_sampler) !=
            VK_ERROR_FEATURE_NOT_PRESENT || texture_sampler != VK_NULL_HANDLE) {
        write_text("vk_test: unsupported mixed sampler filters were accepted\n");
        goto done;
    }
    texture_sampler_info.magFilter = VK_FILTER_NEAREST;
    if (vkCreateSampler(device, &texture_sampler_info, NULL, &texture_sampler) != VK_SUCCESS) {
        write_text("vk_test: nearest texture sampler creation failed\n");
        goto done;
    }
    texture_binding.binding = 0u;
    texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture_binding.descriptorCount = 1u;
    texture_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    descriptor_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_layout_info.bindingCount = 1u;
    descriptor_layout_info.pBindings = &texture_binding;
    texture_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texture_pool_size.descriptorCount = 1u;
    descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descriptor_pool_info.maxSets = 1u;
    descriptor_pool_info.poolSizeCount = 1u;
    descriptor_pool_info.pPoolSizes = &texture_pool_size;
    if (vkCreateDescriptorSetLayout(device, &descriptor_layout_info, NULL,
                                    &texture_descriptor_layout) != VK_SUCCESS ||
        vkCreateDescriptorPool(device, &descriptor_pool_info, NULL,
                               &texture_descriptor_pool) != VK_SUCCESS) {
        write_text("vk_test: sampled image descriptor layout/pool failed\n");
        goto done;
    }
    descriptor_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_alloc_info.descriptorPool = texture_descriptor_pool;
    descriptor_alloc_info.descriptorSetCount = 1u;
    descriptor_alloc_info.pSetLayouts = &texture_descriptor_layout;
    if (vkAllocateDescriptorSets(device, &descriptor_alloc_info,
                                 &texture_descriptor_set) != VK_SUCCESS) {
        write_text("vk_test: sampled image descriptor set allocation failed\n");
        goto done;
    }
    texture_descriptor_info.sampler = texture_sampler;
    texture_descriptor_info.imageView = texture_view;
    texture_descriptor_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.dstSet = texture_descriptor_set;
    descriptor_write.dstBinding = 0u;
    descriptor_write.dstArrayElement = 0u;
    descriptor_write.descriptorCount = 1u;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_write.pBufferInfo = NULL;
    descriptor_write.pImageInfo = &texture_descriptor_info;
    vkUpdateDescriptorSets(device, 1u, &descriptor_write, 0u, NULL);
    texture_pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    texture_pipeline_layout_info.setLayoutCount = 1u;
    texture_pipeline_layout_info.pSetLayouts = &texture_descriptor_layout;
    if (vkCreatePipelineLayout(device, &texture_pipeline_layout_info, NULL,
                               &texture_pipeline_layout) != VK_SUCCESS) {
        write_text("vk_test: sampled image pipeline layout failed\n");
        goto done;
    }
    texture_transition.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    texture_transition.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    texture_transition.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    texture_transition.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    texture_transition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    texture_transition.image = copied_image;
    texture_transition.subresourceRange = image_range;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: sampled texture transition begin failed\n");
        goto done;
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0u,
                         0u, NULL, 0u, NULL, 1u, &texture_transition);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
        write_text("vk_test: sampled texture transition submission failed\n");
        goto done;
    }
    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = sizeof(triangle_vertex_spv);
    shader_info.pCode = triangle_vertex_spv;
    if (vkCreateShaderModule(device, &shader_info, NULL, &texture_vertex_shader) != VK_SUCCESS) {
        write_text("vk_test: sampled texture vertex shader creation failed\n");
        goto done;
    }
    shader_info.codeSize = sizeof(sampled_image_fragment_spv);
    shader_info.pCode = sampled_image_fragment_spv;
    if (vkCreateShaderModule(device, &shader_info, NULL, &texture_fragment_shader) != VK_SUCCESS) {
        write_text("vk_test: sampled image fragment shader creation failed\n");
        goto done;
    }
    graphics_stages[0].module = texture_vertex_shader;
    graphics_stages[1].module = texture_fragment_shader;
    graphics_pipeline_info.layout = texture_pipeline_layout;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &graphics_pipeline_info,
                                  NULL, &texture_pipeline) != VK_SUCCESS) {
        write_text("vk_test: sampled image SPIR-V pipeline rejected\n");
        goto done;
    }
    vkDestroyShaderModule(device, texture_vertex_shader, NULL);
    texture_vertex_shader = VK_NULL_HANDLE;
    vkDestroyShaderModule(device, texture_fragment_shader, NULL);
    texture_fragment_shader = VK_NULL_HANDLE;
    graphics_stages[0].module = vertex_shader;
    graphics_stages[1].module = fragment_shader;
    graphics_pipeline_info.layout = graphics_layout;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: sampled image draw begin failed\n");
        goto done;
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                         0u, NULL, 0u, NULL, 1u, &image_barrier);
    vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, texture_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, texture_pipeline_layout,
                            0u, 1u, &texture_descriptor_set, 0u, NULL);
    vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        ((uint32_t *)image_mapped)[0] != 0xffffffffu ||
        ((uint32_t *)image_mapped)[1] != 0xffffffffu ||
        ((uint32_t *)image_mapped)[2] != 0xff000000u ||
        ((uint32_t *)image_mapped)[3] != 0xff000000u) {
        write_text("vk_test: deferred SPIR-V combined image sampling failed\n");
        goto done;
    }
    write_text("vk_test: deferred SPIR-V nearest image sampling ok\n");
    {
        VkImageCreateInfo rgba_sample_info = image_info;
        VkImage rgba_sample_image = VK_NULL_HANDLE;
        VkDeviceMemory rgba_sample_memory = VK_NULL_HANDLE;
        VkImageViewCreateInfo rgba_sample_view_info = texture_view_info;
        VkImageView rgba_sample_view = VK_NULL_HANDLE;
        VkImageMemoryBarrier rgba_sample_barrier = {0};
        VkMemoryRequirements rgba_sample_requirements = {0};
        void *rgba_sample_bytes = NULL;
        VkResult rgba_end_result = VK_NOT_READY;
        VkResult rgba_submit_result = VK_NOT_READY;
        uint32_t rgba_texels[4] = {
            0xff0000ffu, 0xff00ff00u, 0xffff0000u, 0xffffffffu
        };
        uint32_t previous_sample_pixels[4];
        uint32_t sample_ok = 1u;
        uint32_t sample_stage = 1u;
        rgba_sample_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        rgba_sample_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &rgba_sample_info, NULL,
                          &rgba_sample_image) != VK_SUCCESS) sample_ok = 0u;
        else sample_stage = 2u;
        if (sample_ok != 0u) {
            vkGetImageMemoryRequirements(device, rgba_sample_image,
                                         &rgba_sample_requirements);
            memory_info.allocationSize = rgba_sample_requirements.size;
            memory_info.memoryTypeIndex = 0u;
            if (rgba_sample_requirements.size != sizeof(rgba_texels) ||
                rgba_sample_requirements.memoryTypeBits == 0u ||
                vkAllocateMemory(device, &memory_info, NULL,
                                 &rgba_sample_memory) != VK_SUCCESS ||
                vkBindImageMemory(device, rgba_sample_image,
                                  rgba_sample_memory, 0u) != VK_SUCCESS ||
                vkMapMemory(device, rgba_sample_memory, 0u, sizeof(rgba_texels), 0u,
                            &rgba_sample_bytes) != VK_SUCCESS)
                sample_ok = 0u;
            else sample_stage = 3u;
        }
        if (sample_ok != 0u) {
            memcpy(rgba_sample_bytes, rgba_texels, sizeof(rgba_texels));
            rgba_sample_view_info.image = rgba_sample_image;
            rgba_sample_view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
            if (vkCreateImageView(device, &rgba_sample_view_info, NULL,
                                  &rgba_sample_view) != VK_SUCCESS) sample_ok = 0u;
            else sample_stage = 4u;
        }
        if (sample_ok != 0u) {
            memcpy(previous_sample_pixels, image_mapped, sizeof(previous_sample_pixels));
            rgba_sample_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            rgba_sample_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            rgba_sample_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            rgba_sample_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            rgba_sample_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            rgba_sample_barrier.image = rgba_sample_image;
            rgba_sample_barrier.subresourceRange = image_range;
            texture_descriptor_info.imageView = rgba_sample_view;
            texture_descriptor_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkUpdateDescriptorSets(device, 1u, &descriptor_write, 0u, NULL);
            if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
                vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) sample_ok = 0u;
            else sample_stage = 5u;
        }
        if (sample_ok != 0u) {
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0u, 0u, NULL, 0u, NULL,
                1u, &rgba_sample_barrier);
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u, 0u, NULL,
                0u, NULL, 1u, &image_barrier);
            vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, texture_pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                texture_pipeline_layout, 0u, 1u, &texture_descriptor_set, 0u, NULL);
            vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
            vkCmdEndRenderPass(cmd);
            if (memcmp(previous_sample_pixels, image_mapped,
                       sizeof(previous_sample_pixels)) != 0) {
                sample_ok = 0u;
                sample_stage = 6u;
            }
            if (sample_ok != 0u) {
                rgba_end_result = vkEndCommandBuffer(cmd);
                if (rgba_end_result != VK_SUCCESS) {
                    sample_ok = 0u;
                    sample_stage = 7u;
                }
            }
            if (sample_ok != 0u) {
                rgba_submit_result = vkQueueSubmit(queue, 1u, &submit,
                                                   VK_NULL_HANDLE);
                if (rgba_submit_result != VK_SUCCESS) {
                    sample_ok = 0u;
                    sample_stage = 8u;
                }
            }
            if (sample_ok != 0u &&
                (((uint32_t *)image_mapped)[0] != 0xffffffffu ||
                ((uint32_t *)image_mapped)[1] != 0xffffffffu ||
                ((uint32_t *)image_mapped)[2] != 0xff000000u ||
                 ((uint32_t *)image_mapped)[3] != 0xff000000u)) {
                sample_ok = 0u;
                sample_stage = 9u;
            } else if (sample_ok != 0u) {
                sample_stage = 10u;
            }
        }
        (void)vkResetCommandBuffer(cmd, 0u);
        texture_descriptor_info.imageView = texture_view;
        texture_descriptor_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkUpdateDescriptorSets(device, 1u, &descriptor_write, 0u, NULL);
        if (rgba_sample_bytes != NULL) vkUnmapMemory(device, rgba_sample_memory);
        vkDestroyImageView(device, rgba_sample_view, NULL);
        vkDestroyImage(device, rgba_sample_image, NULL);
        vkFreeMemory(device, rgba_sample_memory, NULL);
        if (sample_ok == 0u) {
            write_text("vk_test: deferred RGBA sampled-image channel mapping failed\n");
            write_hex32("vk_test: RGBA sampling stage=", sample_stage);
            write_hex32("vk_test: RGBA vkEndCommandBuffer=", (uint32_t)rgba_end_result);
            write_hex32("vk_test: RGBA vkQueueSubmit=", (uint32_t)rgba_submit_result);
            write_hex32("vk_test: RGBA sampled pixel0=", ((uint32_t *)image_mapped)[0]);
            write_hex32("vk_test: RGBA sampled pixel1=", ((uint32_t *)image_mapped)[1]);
            write_hex32("vk_test: RGBA sampled pixel2=", ((uint32_t *)image_mapped)[2]);
            write_hex32("vk_test: RGBA sampled pixel3=", ((uint32_t *)image_mapped)[3]);
            goto done;
        }
        write_text("vk_test: deferred RGBA sampled-image channel mapping ok\n");
    }
    texture_sampler_info.magFilter = VK_FILTER_LINEAR;
    texture_sampler_info.minFilter = VK_FILTER_LINEAR;
    if (vkCreateSampler(device, &texture_sampler_info, NULL,
                        &linear_texture_sampler) != VK_SUCCESS) {
        write_text("vk_test: linear texture sampler creation failed\n");
        goto done;
    }
    texture_descriptor_info.sampler = linear_texture_sampler;
    vkUpdateDescriptorSets(device, 1u, &descriptor_write, 0u, NULL);
    vkDestroySampler(device, texture_sampler, NULL);
    texture_sampler = VK_NULL_HANDLE;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: linear sampled image draw begin failed\n");
        goto done;
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                         0u, NULL, 0u, NULL, 1u, &image_barrier);
    vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, texture_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, texture_pipeline_layout,
                            0u, 1u, &texture_descriptor_set, 0u, NULL);
    vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        write_text("vk_test: linear sampled image command validation failed\n");
        goto done;
    }
    if (vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
        write_text("vk_test: linear sampled image submit failed\n");
        goto done;
    }
    if (((uint32_t *)image_mapped)[0] != 0xff808080u ||
        ((uint32_t *)image_mapped)[1] != 0xff808080u ||
        ((uint32_t *)image_mapped)[2] != 0xff000000u ||
        ((uint32_t *)image_mapped)[3] != 0xff000000u) {
        write_text("vk_test: deferred SPIR-V linear image sampling failed\n");
        write_hex32("vk_test: linear pixel0=0x", ((uint32_t *)image_mapped)[0]);
        write_hex32("vk_test: linear pixel1=0x", ((uint32_t *)image_mapped)[1]);
        goto done;
    }
    write_text("vk_test: deferred SPIR-V linear image sampling ok\n");
    if (!vk_subresources_smoke(device, queue, render_pass, texture_pipeline,
            texture_pipeline_layout, texture_descriptor_set, &texture_descriptor_info)) goto done;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: sampled image layout rejection begin failed\n");
        goto done;
    }
    texture_descriptor_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkUpdateDescriptorSets(device, 1u, &descriptor_write, 0u, NULL);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u,
                         0u, NULL, 0u, NULL, 1u, &image_barrier);
    vkCmdBeginRenderPass(cmd, &render_begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, texture_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, texture_pipeline_layout,
                            0u, 1u, &texture_descriptor_set, 0u, NULL);
    vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
    vkCmdEndRenderPass(cmd);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_ERROR_INITIALIZATION_FAILED ||
        ((uint32_t *)image_mapped)[0] != 0xff808080u ||
        ((uint32_t *)image_mapped)[1] != 0xff808080u ||
        ((uint32_t *)image_mapped)[2] != 0xff000000u ||
        ((uint32_t *)image_mapped)[3] != 0xff000000u ||
        vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
        write_text("vk_test: sampled image layout mismatch mutated render target\n");
        goto done;
    }
    texture_descriptor_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkUpdateDescriptorSets(device, 1u, &descriptor_write, 0u, NULL);
    vkDestroyPipeline(device, texture_pipeline, NULL);
    texture_pipeline = VK_NULL_HANDLE;
    vkDestroyPipelineLayout(device, texture_pipeline_layout, NULL);
    texture_pipeline_layout = VK_NULL_HANDLE;
    vkDestroyDescriptorPool(device, texture_descriptor_pool, NULL);
    texture_descriptor_pool = VK_NULL_HANDLE;
    texture_descriptor_set = VK_NULL_HANDLE;
    vkDestroyDescriptorSetLayout(device, texture_descriptor_layout, NULL);
    texture_descriptor_layout = VK_NULL_HANDLE;
    vkDestroyImageView(device, texture_view, NULL);
    texture_view = VK_NULL_HANDLE;
    vkDestroySampler(device, texture_sampler, NULL);
    texture_sampler = VK_NULL_HANDLE;
    vkDestroySampler(device, linear_texture_sampler, NULL);
    linear_texture_sampler = VK_NULL_HANDLE;
    write_text("vk_test: descriptor layout/lifetime preflight ok\n");
    if (!vk_descriptor_arrays_smoke(instance, device, queue, &graphics_pipeline_info,
            &render_begin, image, (uint32_t *)image_mapped)) goto done;
    if (!vk_stencil_smoke(device, queue, &graphics_pipeline_info,
            image_view, (uint32_t *)image_mapped)) goto done;
    if (!vk_native_mapped_capability_smoke()) {write_text("vk_test: mapped capability lifecycle FAILED\n");goto done;}
    if (!vk_api_contract_smoke(instance,device,queue)) goto done;
    if (!vk_msaa_smoke(device, queue, &graphics_pipeline_info)) goto done;
    if (!vk_large_coverage_smoke(device, queue, &graphics_pipeline_info)||!vk_compute_graphics_smoke(device,queue,&graphics_pipeline_info)) goto done;
    vkDestroyFramebuffer(device, framebuffer, NULL);
    framebuffer = VK_NULL_HANDLE;
    vkDestroyImageView(device, image_view, NULL);
    image_view = VK_NULL_HANDLE;
    vkDestroyRenderPass(device, render_pass, NULL);
    render_pass = VK_NULL_HANDLE;
    vkDestroyPipeline(device, graphics_pipeline, NULL);
    graphics_pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline(device, integer_ops_pipeline, NULL);
    integer_ops_pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline(device, push_pipeline, NULL);
    push_pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline(device, vector_math_pipeline, NULL);
    vector_math_pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline(device, varying_pipeline, NULL);
    varying_pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline(device, vertex_attribute_pipeline, NULL);
    vertex_attribute_pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline(device, multi_attribute_pipeline, NULL);
    multi_attribute_pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline(device, normalized_attribute_pipeline, NULL);
    normalized_attribute_pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline(device, vec3_attribute_pipeline, NULL);
    vec3_attribute_pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline(device, strip_pipeline, NULL);
    strip_pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline(device, fan_pipeline, NULL);
    fan_pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline(device, dynamic_state_pipeline, NULL);
    dynamic_state_pipeline = VK_NULL_HANDLE;
    vkDestroyPipelineLayout(device, graphics_layout, NULL);
    graphics_layout = VK_NULL_HANDLE;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
        write_text("vk_test: graphics object lifetime failed\n");
        goto done;
    }
    if (vkFreeDescriptorSets(device, descriptor_pool, 2u, shader_sets) != VK_SUCCESS) {
        write_text("vk_test: draw-held descriptor lifetime failed\n");
        goto done;
    }
    vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    descriptor_pool = VK_NULL_HANDLE;
    descriptor_set = VK_NULL_HANDLE;
    uninitialized_descriptor_set = VK_NULL_HANDLE;
    vkDestroyDescriptorSetLayout(device, descriptor_layout, NULL);
    descriptor_layout = VK_NULL_HANDLE;
    write_text("vk_test: deferred SPIR-V triangle/software rasterizer ok\n");
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: unsupported draw begin failed\n");
        goto done;
    }
    vkCmdDraw(cmd, 3u, 1u, 0u, 0u);
    if (vkEndCommandBuffer(cmd) != VK_ERROR_FEATURE_NOT_PRESENT ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) == VK_SUCCESS) {
        write_text("vk_test: unsupported draw reported success\n");
        goto done;
    }

    /* A completed command buffer still owns references to its recorded
       resources until reset/free. Destroying them after completion must not
       invalidate the recorded storage before that release point. */
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: resource lifetime command begin failed\n");
        goto done;
    }
    vkCmdCopyBuffer(cmd, buffer, dst_buffer, 1u, &copy_region);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        vkQueueSubmit(queue, 1u, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        ((uint8_t *)dst_mapped)[0] != ((uint8_t *)mapped)[0]) {
        write_text("vk_test: resource lifetime submit failed\n");
        goto done;
    }
    vkUnmapMemory(device, memory);
    vkUnmapMemory(device, dst_memory);
    vkDestroyBuffer(device, buffer, NULL);
    buffer = VK_NULL_HANDLE;
    vkDestroyBuffer(device, dst_buffer, NULL);
    dst_buffer = VK_NULL_HANDLE;
    vkFreeMemory(device, memory, NULL);
    memory = VK_NULL_HANDLE;
    vkFreeMemory(device, dst_memory, NULL);
    dst_memory = VK_NULL_HANDLE;
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS) {
        write_text("vk_test: command-held resource release failed\n");
        goto done;
    }
    write_text("vk_test: bound memory and deferred resource lifetime ok\n");

    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = sizeof(malformed_spirv);
    shader_info.pCode = malformed_spirv;
    if (vkCreateShaderModule(device, &shader_info, NULL, &shader) == VK_SUCCESS ||
        shader != VK_NULL_HANDLE) {
        write_text("vk_test: malformed SPIR-V accepted\n");
        goto done;
    }
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1u, &pipeline_info,
                                  NULL, &pipeline) != VK_ERROR_FEATURE_NOT_PRESENT ||
        pipeline != VK_NULL_HANDLE) {
        write_text("vk_test: unsupported shaderless pipeline state accepted\n");
        goto done;
    }
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    {
        VkSubmitInfo sync_submit = {0};
        VkSubmitInfo sync_batch[2] = {{0}, {0}};
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSemaphore null_semaphore = VK_NULL_HANDLE;
        sync_submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        sync_batch[0].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        sync_batch[1].sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        if (vkCreateSemaphore(device, &semaphore_info, NULL, &semaphore) != VK_SUCCESS ||
            semaphore == VK_NULL_HANDLE) {
            write_text("vk_test: binary semaphore creation failed\n");
            goto done;
        }
        sync_submit.signalSemaphoreCount = 1u;
        sync_submit.pSignalSemaphores = &semaphore;
        if (vkQueueSubmit(queue, 1u, &sync_submit, VK_NULL_HANDLE) != VK_SUCCESS ||
            vkQueueSubmit(queue, 1u, &sync_submit, VK_NULL_HANDLE) !=
                VK_ERROR_INITIALIZATION_FAILED) {
            write_text("vk_test: binary semaphore double signal accepted\n");
            goto done;
        }
        sync_submit.signalSemaphoreCount = 0u;
        sync_submit.pSignalSemaphores = NULL;
        sync_submit.waitSemaphoreCount = 1u;
        sync_submit.pWaitSemaphores = &semaphore;
        sync_submit.pWaitDstStageMask = &wait_stage;
        if (vkQueueSubmit(queue, 1u, &sync_submit, VK_NULL_HANDLE) != VK_SUCCESS) {
            write_text("vk_test: binary semaphore wait/consume semantics failed\n");
            goto done;
        }
        wait_stage = 0u;
        if (vkQueueSubmit(queue, 1u, &sync_submit, VK_NULL_HANDLE) !=
                VK_ERROR_INITIALIZATION_FAILED) {
            write_text("vk_test: zero semaphore wait stage accepted\n");
            goto done;
        }
        wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        sync_batch[0].signalSemaphoreCount = 1u;
        sync_batch[0].pSignalSemaphores = &semaphore;
        sync_batch[1].waitSemaphoreCount = 1u;
        sync_batch[1].pWaitSemaphores = &semaphore;
        sync_batch[1].pWaitDstStageMask = &wait_stage;
        if (vkQueueSubmit(queue, 2u, sync_batch, VK_NULL_HANDLE) != VK_SUCCESS) {
            write_text("vk_test: ordered semaphore submit batch failed\n");
            goto done;
        }
        sync_submit.waitSemaphoreCount = 0u;
        sync_submit.pWaitSemaphores = NULL;
        sync_submit.pWaitDstStageMask = NULL;
        sync_submit.signalSemaphoreCount = 1u;
        sync_submit.pSignalSemaphores = &null_semaphore;
        if (vkQueueSubmit(queue, 1u, &sync_submit, VK_NULL_HANDLE) !=
                VK_ERROR_INITIALIZATION_FAILED) {
            write_text("vk_test: null semaphore handle accepted\n");
            goto done;
        }
        sync_submit.signalSemaphoreCount = 0u;
        sync_submit.pSignalSemaphores = NULL;
    }
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 0u;
    render_pass_info.pAttachments = NULL;
    if (vkCreateRenderPass(device, &render_pass_info, NULL, &render_pass) !=
            VK_ERROR_FEATURE_NOT_PRESENT || render_pass != VK_NULL_HANDLE) {
        write_text("vk_test: empty render pass accepted\n");
        goto done;
    }
    write_text("vk_test: kernel-backed binary semaphore tokens and fence status/lifetime ok\n");

    timeline_queue = vex_gpu_queue_create(0u);
    write_text("vk_test: kernel timeline queue created\n");
    if ((int64_t)timeline_queue < 0 || timeline_queue == 0u ||
        (int64_t)vex_gpu_queue_create(1u) >= 0 ||
        vex_gpu_submit_begin(timeline_queue, &timeline_serial) != 0 ||
        timeline_serial != 1u ||
        vex_gpu_fence_query(timeline_queue, timeline_serial) != 1 ||
        vex_gpu_fence_query(timeline_queue, timeline_serial + 1u) != -1 ||
        (write_text("vk_test: kernel timeline timeout wait\n"),
         vex_gpu_fence_wait(timeline_queue, timeline_serial, 1u)) != -3 ||
        vex_gpu_submit_begin(timeline_queue, &timeline_serial_second) != 0 ||
        timeline_serial_second != 2u ||
        vex_gpu_fence_query(timeline_queue, timeline_serial_second) != 1 ||
        vex_gpu_submit_complete(timeline_queue, timeline_serial_second, 0) == 0 ||
        vex_gpu_submit_complete(timeline_queue, timeline_serial, 0) != 0 ||
        vex_gpu_fence_query(timeline_queue, timeline_serial) != 0 ||
        vex_gpu_fence_query(timeline_queue, timeline_serial_second) != 1 ||
        vex_gpu_fence_wait(timeline_queue, timeline_serial, 0u) != 0 ||
        vex_gpu_submit_complete(timeline_queue, timeline_serial_second, -77) != 0 ||
        vex_gpu_fence_query(timeline_queue, timeline_serial_second) != -2 ||
        vex_gpu_fence_wait(timeline_queue, timeline_serial_second, 0u) != -2 ||
        vex_gpu_submit_begin(timeline_queue, &timeline_serial) == 0) {
        write_text("vk_test: kernel queue timeline/fence contract failed\n");
        goto done;
    }
    if (vex_gpu_queue_destroy(timeline_queue) != 0 ||
        vex_gpu_fence_query(timeline_queue, 1u) != -1) {
        timeline_queue = 0u;
        write_text("vk_test: kernel queue capability cleanup failed\n");
        goto done;
    }
    timeline_queue = 0u;
    write_text("vk_test: kernel multi-submit timeline order, error, timeout and stale handle ok\n");

    /* Vulkan memory exports a process-local Surface capability. Import takes
       a retained local reference to the same shared backing. */
    memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memory_info.pNext = NULL;
    memory_info.allocationSize = 64u;
    memory_info.memoryTypeIndex = 0u;
    if (vkAllocateMemory(device, &memory_info, NULL, &exported_memory) != VK_SUCCESS ||
        vkGetVexMemoryCapabilityVEX(device, exported_memory,
            &surface_info.surfaceCapability) != VK_SUCCESS) {
        write_text("vk_test: Vex memory capability export failed\n");
        goto done;
    }
    wsi_surface_query.surface_handle = surface_info.surfaceCapability;
    if (vex_surface_query(&wsi_surface_query) != 0 ||
        wsi_surface_query.backing_identity == 0u ||
        (wsi_surface_query.capability_rights & VEX_SURFACE_RIGHT_EXPORT) == 0u) {
        write_text("vk_test: Vulkan memory backing identity/rights invalid\n");
        goto done;
    }
    memory_import_info.sType = VK_STRUCTURE_TYPE_VEX_MEMORY_IMPORT_INFO_EXPERIMENTAL;
    memory_import_info.pNext = NULL;
    memory_import_info.surfaceCapability = surface_info.surfaceCapability;
    imported_memory_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imported_memory_info.pNext = &memory_import_info;
    imported_memory_info.allocationSize = 64u;
    imported_memory_info.memoryTypeIndex = 0u;
    if (vkAllocateMemory(device, &imported_memory_info, NULL,
            &imported_memory) != VK_SUCCESS ||
        vkMapMemory(device, exported_memory, 0u, 64u, 0u,
            &exported_memory_mapped) != VK_SUCCESS ||
        vkMapMemory(device, imported_memory, 0u, 64u, 0u,
            &imported_memory_mapped) != VK_SUCCESS) {
        write_text("vk_test: capability-backed Vulkan memory import failed\n");
        goto done;
    }
    ((uint32_t *)exported_memory_mapped)[3] = 0x5a31c7e2u;
    if (((uint32_t *)imported_memory_mapped)[3] != 0x5a31c7e2u) {
        write_text("vk_test: imported Vulkan memory did not share backing\n");
        goto done;
    }
    vkUnmapMemory(device, imported_memory);
    vkUnmapMemory(device, exported_memory);
    imported_memory_mapped = NULL;
    exported_memory_mapped = NULL;
    vkFreeMemory(device, imported_memory, NULL);
    imported_memory = VK_NULL_HANDLE;
    vkFreeMemory(device, exported_memory, NULL);
    exported_memory = VK_NULL_HANDLE;
    write_text("vk_test: shared Vex backing export/import and identity ok\n");

    surface_info.sType = VK_STRUCTURE_TYPE_VEX_SURFACE_CREATE_INFO_EXPERIMENTAL;
    surface_info.surfaceCapability = 1u;
    if (vkCreateVexSurfaceVEX(instance, &surface_info, NULL, &surface) !=
            VK_ERROR_SURFACE_LOST_KHR || surface != VK_NULL_HANDLE) {
        write_text("vk_test: invalid Surface capability was accepted\n");
        goto done;
    }

    wsi_buffer.width = 16u;
    wsi_buffer.height = 8u;
    wsi_buffer.stride = 16u;
    wsi_buffer.format = VEX_GPU_FORMAT_XRGB8888;
    wsi_buffer.flags = VEX_GPU_MEMORY_CPU_VISIBLE;
    wsi_buffer.size = 16u * 8u * sizeof(uint32_t);
    if (vex_gpu_buffer_create(&wsi_buffer) != 0 || wsi_buffer.handle == 0u) {
        write_text("vk_test: WSI shared backing allocation failed\n");
        goto done;
    }
    wsi_vex_surface.buffer_handle = wsi_buffer.handle;
    wsi_vex_surface.alpha_mode = VEX_GPU_SURFACE_ALPHA_OPAQUE;
    wsi_vex_surface.usage = VEX_GPU_SURFACE_USAGE_SAMPLED;
    if (vex_gpu_surface_create(&wsi_vex_surface) != 0 ||
        wsi_vex_surface.surface_handle == 0u) {
        write_text("vk_test: WSI Surface capability creation failed\n");
        goto done;
    }
    if (vex_gpu_buffer_destroy(wsi_buffer.handle) != 0) {
        write_text("vk_test: Surface did not retain backing after creator close\n");
        goto done;
    }
    wsi_buffer.handle = 0u;
    wsi_surface_query.surface_handle = wsi_vex_surface.surface_handle;
    if (vex_surface_query(&wsi_surface_query) != 0 ||
        wsi_surface_query.width != 16u || wsi_surface_query.height != 8u ||
        wsi_surface_query.stride != 16u) {
        write_text("vk_test: WSI Surface metadata mismatch\n");
        goto done;
    }
    surface_info.surfaceCapability = wsi_vex_surface.surface_handle;
    if (vkCreateVexSurfaceVEX(instance, &surface_info, NULL, &surface) != VK_SUCCESS ||
        surface == VK_NULL_HANDLE ||
        vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, 0u, surface,
            &supports_present) != VK_SUCCESS || supports_present != VK_TRUE ||
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface,
            &surface_capabilities) != VK_SUCCESS ||
        surface_capabilities.minImageCount != 2u ||
        surface_capabilities.maxImageCount != 4u ||
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count,
            &surface_format) != VK_SUCCESS || count != 1u ||
        surface_format.format != VK_FORMAT_B8G8R8A8_UNORM) {
        write_text("vk_test: Vex Surface WSI queries failed\n");
        goto done;
    }
    swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.surface = surface;
    swapchain_create_info.minImageCount = 2u;
    swapchain_create_info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    swapchain_create_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchain_create_info.imageExtent.width = 16u;
    swapchain_create_info.imageExtent.height = 8u;
    swapchain_create_info.imageArrayLayers = 1u;
    swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_create_info.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(device, &swapchain_create_info, NULL, &swapchain) != VK_SUCCESS ||
        swapchain == VK_NULL_HANDLE ||
        vkGetSwapchainImagesKHR(device, swapchain, &wsi_image_count, NULL) != VK_SUCCESS ||
        wsi_image_count != 2u ||
        vkGetSwapchainImagesKHR(device, swapchain, &wsi_image_count, wsi_images) != VK_SUCCESS ||
        wsi_image_count != 2u || wsi_images[0] == wsi_images[1]) {
        write_text("vk_test: Vex swapchain creation/images failed\n");
        goto done;
    }
    wsi_fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateSemaphore(device, &semaphore_info, NULL,
                          &wsi_render_semaphore) != VK_SUCCESS ||
        wsi_render_semaphore == VK_NULL_HANDLE ||
        vkCreateFence(device, &wsi_fence_info, NULL, &wsi_fence) != VK_SUCCESS ||
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, semaphore, wsi_fence,
            &wsi_first_image_index) != VK_SUCCESS || wsi_first_image_index != 0u ||
        vkGetFenceStatus(device, wsi_fence) != VK_SUCCESS ||
        vkAcquireNextImageKHR(device, swapchain, 0u, VK_NULL_HANDLE, VK_NULL_HANDLE,
            &wsi_second_image_index) != VK_SUCCESS || wsi_second_image_index != 1u ||
        vkAcquireNextImageKHR(device, swapchain, 10000000u, VK_NULL_HANDLE,
            VK_NULL_HANDLE, &count) != VK_TIMEOUT) {
        write_text("vk_test: swapchain acquire/fence semantics failed\n");
        goto done;
    }
    write_text("vk_test: acquired two swapchain images independently\n");
    cmd_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: WSI command buffer begin failed\n");
        goto done;
    }
    image_range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_range.baseMipLevel = 0u;
    image_range.levelCount = 1u;
    image_range.baseArrayLayer = 0u;
    image_range.layerCount = 1u;
    wsi_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    wsi_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    wsi_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    /* Both acquired images remain distinct while the first presentation is
       queued. Render image 0 (the externally visible Vex Surface) first. */
    wsi_image_index = wsi_first_image_index;
    wsi_image = wsi_images[wsi_image_index];
    wsi_barrier.image = wsi_image;
    wsi_barrier.subresourceRange = image_range;
    wsi_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    wsi_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL, 1u, &wsi_barrier);
    clear_color.float32[0] = 0.75f;
    clear_color.float32[1] = 0.25f;
    clear_color.float32[2] = 0.125f;
    clear_color.float32[3] = 1.0f;
    vkCmdClearColorImage(cmd, wsi_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clear_color, 1u, &image_range);
    wsi_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    wsi_barrier.dstAccessMask = 0u;
    wsi_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    wsi_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0u, 0u, NULL, 0u, NULL, 1u, &wsi_barrier);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS ||
        vkResetFences(device, 1u, &wsi_fence) != VK_SUCCESS) {
        write_text("vk_test: WSI deferred command recording failed\n");
        goto done;
    }
    {
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkCommandBuffer submit_command = cmd;
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1u;
        submit.pWaitSemaphores = &semaphore;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1u;
        submit.pCommandBuffers = &submit_command;
        submit.signalSemaphoreCount = 1u;
        submit.pSignalSemaphores = &wsi_render_semaphore;
        if (vk_test_queue_submit_raw(queue, 1u, &submit, wsi_fence) != VK_SUCCESS ||
            vkWaitForFences(device, 1u, &wsi_fence, VK_TRUE,
                            UINT64_MAX) != VK_SUCCESS) {
            write_text("vk_test: deferred render-to-Surface submission failed\n");
            goto done;
        }
    }
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.swapchainCount = 1u;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &wsi_image_index;
    present_info.waitSemaphoreCount = 1u;
    present_info.pWaitSemaphores = &wsi_render_semaphore;
    if (vkQueuePresentKHR(queue, &present_info) != VK_SUCCESS ||
        wsi_image_index != wsi_first_image_index) {
        write_text("vk_test: first swapchain image acquire/present state failed\n");
        goto done;
    }
    write_text("vk_test: first image queued for asynchronous present\n");
    if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS) {
        write_text("vk_test: second swapchain command buffer reset failed\n");
        goto done;
    }
    present_info.waitSemaphoreCount = 0u;
    present_info.pWaitSemaphores = NULL;
    wsi_image_index = wsi_second_image_index;
    wsi_image = wsi_images[wsi_image_index];
    wsi_barrier.image = wsi_image;
    wsi_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    wsi_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    wsi_barrier.srcAccessMask = 0u;
    wsi_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, NULL, 0u, NULL, 1u, &wsi_barrier);
    vkCmdClearColorImage(cmd, wsi_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        &clear_color, 1u, &image_range);
    wsi_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    wsi_barrier.dstAccessMask = 0u;
    wsi_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    wsi_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0u, 0u, NULL, 0u, NULL, 1u, &wsi_barrier);
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        write_text("vk_test: second swapchain command buffer end failed\n");
        goto done;
    }
    {
        VkCommandBuffer submit_command = cmd;
        VkSubmitInfo second_submit = {0};
        second_submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        second_submit.commandBufferCount = 1u;
        second_submit.pCommandBuffers = &submit_command;
        if (vk_test_queue_submit_raw(queue, 1u, &second_submit,
                                     VK_NULL_HANDLE) != VK_SUCCESS) {
            write_text("vk_test: second swapchain image render submit failed\n");
            goto done;
        }
    }
    present_info.pImageIndices = &wsi_image_index;
    if (vkQueuePresentKHR(queue, &present_info) != VK_SUCCESS) {
        write_text("vk_test: second async present enqueue failed\n");
        goto done;
    }
    write_text("vk_test: second image queued for asynchronous present\n");
    if (vkQueueWaitIdle(queue) != VK_SUCCESS) {
        write_text("vk_test: compositor queue completion wait failed\n");
        goto done;
    }
    write_text("vk_test: both compositor presents completed\n");
    if (vkDeviceWaitIdle(device) != VK_SUCCESS) {
        write_text("vk_test: compositor device completion wait failed\n");
        goto done;
    }
    if (vex_surface_query(&wsi_surface_query) != 0 ||
        wsi_surface_query.submitted_serial == 0u ||
        wsi_surface_query.completed_serial < wsi_surface_query.submitted_serial) {
        write_text("vk_test: surface completion serial not visible\n");
        goto done;
    }
    wsi_read_map.surface_handle = wsi_vex_surface.surface_handle;
    wsi_read_map.length = wsi_surface_query.backing_size;
    wsi_read_map.access = VEX_SURFACE_MAP_READ;
    if (vex_surface_map(&wsi_read_map) != 0 || wsi_read_map.mapped_addr == 0u ||
        ((uint32_t *)(uintptr_t)wsi_read_map.mapped_addr)[0] != 0xffbf4020u) {
        write_text("vk_test: presented pixels did not reach the shared Surface\n");
        goto done;
    }
    wsi_read_unmap.surface_handle = wsi_vex_surface.surface_handle;
    wsi_read_unmap.mapped_addr = wsi_read_map.mapped_addr;
    wsi_read_unmap.length = wsi_read_map.length;
    wsi_read_unmap.access = VEX_SURFACE_MAP_READ;
    if (vex_surface_unmap(&wsi_read_unmap) != 0) {
        write_text("vk_test: WSI readback mapping cleanup failed\n");
        goto done;
    }
    wsi_read_map.mapped_addr = 0u;
    write_text("vk_test: shared Surface readback verified\n");
    if (vkAcquireNextImageKHR(device, swapchain, 0u, VK_NULL_HANDLE,
            VK_NULL_HANDLE, &wsi_image_index) != VK_SUCCESS ||
        wsi_image_index != 0u || vkQueuePresentKHR(queue, &present_info) != VK_SUCCESS ||
        vkQueueWaitIdle(queue) != VK_SUCCESS) {
        write_text("vk_test: WSI image reacquire/completion lifecycle failed\n");
        goto done;
    }
    {
        VkSubmitInfo wait_gate = {0};
        VkSubmitInfo wait_chain = {0};
        VkSubmitInfo wait_done = {0};
        VkCommandBuffer simultaneous_cmds[2] = {VK_NULL_HANDLE,
                                                  VK_NULL_HANDLE};
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        uint64_t event_value = 0u;
        uint32_t acquired_index = UINT32_MAX;
        int returned_before_signal;
        int chained_submits_queued = 0;
        int simultaneous_cb_ready = 1;
        int fence_pending_before_release = 0;
        VkResult wait_chain_result = VK_ERROR_INITIALIZATION_FAILED;
        VkResult wait_done_result = VK_ERROR_INITIALIZATION_FAILED;
        VkResult fence_pre_result = VK_ERROR_INITIALIZATION_FAILED;
        int start_wait;
        int return_wait;
        int join_result;
        int thread_status = -1;

        async_started_event = vex_event_counter_create(0u, 0u);
        async_returned_event = vex_event_counter_create(0u, 0u);
        if (async_started_event == 0u || async_returned_event == 0u ||
            (int64_t)async_started_event < 0 ||
            (int64_t)async_returned_event < 0 ||
            vkCreateSemaphore(device, &semaphore_info, NULL,
                              &async_wait_semaphore) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphore_info, NULL,
                              &async_chain_semaphore) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphore_info, NULL,
                              &async_done_semaphore) != VK_SUCCESS ||
            vkCreateFence(device, &wsi_fence_info, NULL,
                          &async_wait_fence) != VK_SUCCESS) {
            write_text("vk_test: async queue-wait setup failed\n");
            goto done;
        }
        wait_gate.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        wait_gate.waitSemaphoreCount = 1u;
        wait_gate.pWaitSemaphores = &async_wait_semaphore;
        wait_gate.pWaitDstStageMask = &wait_stage;
        wait_gate.signalSemaphoreCount = 1u;
        wait_gate.pSignalSemaphores = &async_chain_semaphore;
        async_probe.queue = queue;
        async_probe.submit = wait_gate;
        async_probe.fence = VK_NULL_HANDLE;
        async_probe.started_event = async_started_event;
        async_probe.returned_event = async_returned_event;
        async_probe.result = VK_ERROR_INITIALIZATION_FAILED;
        if (vex_thread_create(vk_async_submit_probe_thread, &async_probe, 0u,
                              &async_probe_tid) != 0) {
            write_text("vk_test: async submit probe thread creation failed\n");
            goto done;
        }
        start_wait = vex_event_counter_read(async_started_event, &event_value,
                                             0u, 100u);
        return_wait = start_wait == 0
            ? vex_event_counter_read(async_returned_event, &event_value,
                                     0u, 1000u)
            : -1;
        returned_before_signal = return_wait == 0 &&
            __atomic_load_n(&async_probe.returned, __ATOMIC_ACQUIRE) != 0u &&
            async_probe.result == VK_SUCCESS;

        if (returned_before_signal) {
            /* Exercise the ordinary Vulkan simultaneous-use contract with a
               single immutable command buffer referenced twice by an
               in-flight submission. The queue is blocked above, so its
               pending count must remain two until the external acquire signal
               releases the dependency chain. */
            cmd_begin.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            if (vkResetCommandBuffer(cmd, 0u) != VK_SUCCESS ||
                vkBeginCommandBuffer(cmd, &cmd_begin) != VK_SUCCESS ||
                vkEndCommandBuffer(cmd) != VK_SUCCESS) {
                write_text("vk_test: simultaneous-use command buffer setup failed\n");
                simultaneous_cb_ready = 0;
            }
            cmd_begin.flags = 0u;
            simultaneous_cmds[0] = cmd;
            simultaneous_cmds[1] = cmd;
            wait_chain.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            wait_chain.waitSemaphoreCount = 1u;
            wait_chain.pWaitSemaphores = &async_chain_semaphore;
            wait_chain.pWaitDstStageMask = &wait_stage;
            wait_chain.signalSemaphoreCount = 1u;
            wait_chain.pSignalSemaphores = &async_done_semaphore;
            wait_done.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            wait_done.waitSemaphoreCount = 1u;
            wait_done.pWaitSemaphores = &async_done_semaphore;
            wait_done.pWaitDstStageMask = &wait_stage;
            wait_done.commandBufferCount = 2u;
            wait_done.pCommandBuffers = simultaneous_cmds;
            if (simultaneous_cb_ready) {
                wait_chain_result = vk_test_queue_submit_raw(queue, 1u,
                    &wait_chain, VK_NULL_HANDLE);
                if (wait_chain_result == VK_SUCCESS) {
                    wait_done_result = vk_test_queue_submit_raw(queue, 1u,
                        &wait_done, async_wait_fence);
                    if (wait_done_result == VK_SUCCESS)
                        chained_submits_queued = 1;
                }
            }
            fence_pre_result = vkGetFenceStatus(device, async_wait_fence);
            fence_pending_before_release = chained_submits_queued &&
                fence_pre_result == VK_NOT_READY;
        }

        /* Always provide the producer signal before joining. On an inline
           implementation this also releases the blocked submitter, allowing
           the test to fail cleanly rather than hanging the VM. */
        if (vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                async_wait_semaphore, VK_NULL_HANDLE, &acquired_index) !=
                VK_SUCCESS) {
            write_text("vk_test: async wait producer could not signal semaphore\n");
            goto done;
        }
        async_acquired = 1u;
        join_result = vex_thread_join(async_probe_tid, &thread_status);
        async_probe_tid = 0u;
        if (join_result != 0 || thread_status != 0 || !returned_before_signal ||
            !chained_submits_queued ||
            !fence_pending_before_release ||
            vkWaitForFences(device, 1u, &async_wait_fence, VK_TRUE,
                            UINT64_MAX) != VK_SUCCESS) {
            write_hex32("vk_test: async returned-before-signal=", (uint32_t)returned_before_signal);
            write_hex32("vk_test: async chain-submit-result=", (uint32_t)wait_chain_result);
            write_hex32("vk_test: async done-submit-result=", (uint32_t)wait_done_result);
            write_hex32("vk_test: async early-fence-status=", (uint32_t)fence_pre_result);
            write_hex32("vk_test: async simultaneous-cb-ready=", (uint32_t)simultaneous_cb_ready);
            write_hex32("vk_test: async probe-submit-result=", (uint32_t)async_probe.result);
            write_hex32("vk_test: async return-event-wait=", (uint32_t)return_wait);
            write_hex32("vk_test: async start-event-wait=", (uint32_t)start_wait);
            write_text("vk_test: vkQueueSubmit blocked or completed before its wait dependency\n");
            goto done;
        }
        present_info.waitSemaphoreCount = 0u;
        present_info.pWaitSemaphores = NULL;
        wsi_image_index = acquired_index;
        if (vkQueuePresentKHR(queue, &present_info) != VK_SUCCESS ||
            vkQueueWaitIdle(queue) != VK_SUCCESS) {
            write_text("vk_test: async queue wait/present completion failed\n");
            goto done;
        }
        async_acquired = 0u;
        vkDestroyFence(device, async_wait_fence, NULL);
        async_wait_fence = VK_NULL_HANDLE;
        vkDestroySemaphore(device, async_wait_semaphore, NULL);
        async_wait_semaphore = VK_NULL_HANDLE;
        vkDestroySemaphore(device, async_chain_semaphore, NULL);
        async_chain_semaphore = VK_NULL_HANDLE;
        vkDestroySemaphore(device, async_done_semaphore, NULL);
        async_done_semaphore = VK_NULL_HANDLE;
        (void)vex_event_counter_release(async_started_event);
        (void)vex_event_counter_release(async_returned_event);
        async_started_event = 0u;
        async_returned_event = 0u;
        write_text("vk_test: asynchronous submit chain, simultaneous-use lifetime, queue order and fence completion verified\n");
    }
    /* Recreate with a three-image chain. The old chain becomes OUT_OF_DATE,
       while its images remain alive until the caller destroys that handle. */
    swapchain_create_info.minImageCount = 3u;
    swapchain_create_info.oldSwapchain = swapchain;
    VkResult recreate_result=vkCreateSwapchainKHR(device,&swapchain_create_info,NULL,&replacement_swapchain);
    VkResult retire_result=recreate_result==VK_SUCCESS?
        vkAcquireNextImageKHR(device,swapchain,0u,VK_NULL_HANDLE,VK_NULL_HANDLE,&wsi_image_index):VK_NOT_READY;
    if (recreate_result != VK_SUCCESS || replacement_swapchain == VK_NULL_HANDLE ||
        retire_result != VK_ERROR_OUT_OF_DATE_KHR) {
        write_hex32("vk_test: swapchain recreate result=",(uint32_t)recreate_result);
        write_hex32("vk_test: swapchain retire result=",(uint32_t)retire_result);
        write_text("vk_test: swapchain recreation/retirement failed\n");
        goto done;
    }
    wsi_image_count = 4u;
    if (vkGetSwapchainImagesKHR(device, replacement_swapchain,
            &wsi_image_count, wsi_images) != VK_SUCCESS ||
        wsi_image_count != 3u || wsi_images[0] == wsi_images[1] ||
        wsi_images[1] == wsi_images[2] || wsi_images[0] == wsi_images[2]) {
        write_text("vk_test: recreated swapchain image set invalid\n");
        goto done;
    }
    write_text("vk_test: oldSwapchain retirement and three-image recreation ok\n");
    vkDestroySwapchainKHR(device, swapchain, NULL);
    swapchain = replacement_swapchain;
    replacement_swapchain = VK_NULL_HANDLE;
    if (!vk_subresources_inflight_smoke(device, queue, swapchain)) goto done;
    write_text("vk_test: deferred Vulkan render reached shared Surface and compositor\n");
    write_text("vk_test: Khronos headers, Vex backing, and WSI path validated\n");
    core_ok = 1;
    if (!vk_allocation_accounting_smoke(instance,device)) {
        core_ok = 0;
        write_text("vk_test: PMM / Vulkan heap accounting FAILED\n");
    }

done:
    if (async_probe_tid != 0u && async_wait_semaphore != VK_NULL_HANDLE &&
        swapchain != VK_NULL_HANDLE && async_acquired == 0u) {
        uint32_t release_index;
        if (vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                async_wait_semaphore, VK_NULL_HANDLE, &release_index) == VK_SUCCESS) {
            async_acquired = 1u;
            (void)vex_thread_join(async_probe_tid, NULL);
            async_probe_tid = 0u;
            present_info.waitSemaphoreCount = 0u;
            present_info.pWaitSemaphores = NULL;
            wsi_image_index = release_index;
            (void)vkQueuePresentKHR(queue, &present_info);
        }
    }
    if (async_probe_tid != 0u) {
        (void)vex_thread_join(async_probe_tid, NULL);
        async_probe_tid = 0u;
    }
    if (queue != VK_NULL_HANDLE) (void)vkQueueWaitIdle(queue);
    if (async_wait_fence != VK_NULL_HANDLE)
        vkDestroyFence(device, async_wait_fence, NULL);
    if (async_wait_semaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(device, async_wait_semaphore, NULL);
    if (async_started_event != 0u)
        (void)vex_event_counter_release(async_started_event);
    if (async_returned_event != 0u)
        (void)vex_event_counter_release(async_returned_event);
    if (feature_probe != VK_NULL_HANDLE) vkDestroyDevice(feature_probe, NULL);
    if (timeline_queue != 0u && (int64_t)timeline_queue >= 0)
        (void)vex_gpu_queue_destroy(timeline_queue);
    if (replacement_swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device, replacement_swapchain, NULL);
    if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, NULL);
    if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, NULL);
    if (wsi_read_map.mapped_addr != 0u) {
        wsi_read_unmap.surface_handle = wsi_vex_surface.surface_handle;
        wsi_read_unmap.mapped_addr = wsi_read_map.mapped_addr;
        wsi_read_unmap.length = wsi_read_map.length;
        wsi_read_unmap.access = VEX_SURFACE_MAP_READ;
        (void)vex_surface_unmap(&wsi_read_unmap);
    }
    if (wsi_vex_surface.surface_handle != 0u)
        (void)vex_gpu_surface_destroy(wsi_vex_surface.surface_handle);
    if (wsi_buffer.handle != 0u) (void)vex_gpu_buffer_destroy(wsi_buffer.handle);
    if (wsi_fence != VK_NULL_HANDLE) vkDestroyFence(device, wsi_fence, NULL);
    if (imported_memory != VK_NULL_HANDLE) vkFreeMemory(device, imported_memory, NULL);
    if (exported_memory != VK_NULL_HANDLE) vkFreeMemory(device, exported_memory, NULL);
    if (index_mapped != NULL && index_memory != VK_NULL_HANDLE)
        vkUnmapMemory(device, index_memory);
    if (semaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, semaphore, NULL);
    if (wsi_render_semaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(device, wsi_render_semaphore, NULL);
    if (alpha_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, alpha_pipeline, NULL);
    if (texture_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, texture_pipeline, NULL);
    if (two_uniform_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, two_uniform_pipeline, NULL);
    if (loop_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, loop_pipeline, NULL);
    if (graphics_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, graphics_pipeline, NULL);
    if (integer_ops_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, integer_ops_pipeline, NULL);
    if (push_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, push_pipeline, NULL);
    if (vector_math_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, vector_math_pipeline, NULL);
    if (varying_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, varying_pipeline, NULL);
    if (vertex_attribute_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, vertex_attribute_pipeline, NULL);
    if (multi_attribute_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, multi_attribute_pipeline, NULL);
    if (normalized_attribute_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, normalized_attribute_pipeline, NULL);
    if (vec3_attribute_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, vec3_attribute_pipeline, NULL);
    if (strip_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, strip_pipeline, NULL);
    if (fan_pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, fan_pipeline, NULL);
    if (dynamic_state_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(device, dynamic_state_pipeline, NULL);
    if (texture_pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, texture_pipeline_layout, NULL);
    if (two_uniform_pipeline_layout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(device, two_uniform_pipeline_layout, NULL);
    if (graphics_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, graphics_layout, NULL);
    if (vertex_shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertex_shader, NULL);
    if (fragment_shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, fragment_shader, NULL);
    if (integer_ops_fragment_shader != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, integer_ops_fragment_shader, NULL);
    if (push_fragment_shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, push_fragment_shader, NULL);
    if (vector_math_fragment_shader != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, vector_math_fragment_shader, NULL);
    if (varying_vertex_shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, varying_vertex_shader, NULL);
    if (varying_fragment_shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, varying_fragment_shader, NULL);
    if (vertex_attribute_shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, vertex_attribute_shader, NULL);
    if (multi_attribute_shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, multi_attribute_shader, NULL);
    if (vec3_attribute_shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, vec3_attribute_shader, NULL);
    if (texture_vertex_shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, texture_vertex_shader, NULL);
    if (texture_fragment_shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, texture_fragment_shader, NULL);
    if (two_uniform_shader != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, two_uniform_shader, NULL);
    if (two_uniform_vertex_shader != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, two_uniform_vertex_shader, NULL);
    if (loop_fragment_shader != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, loop_fragment_shader, NULL);
    if (loop_vertex_shader != VK_NULL_HANDLE)
        vkDestroyShaderModule(device, loop_vertex_shader, NULL);
    if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(device, framebuffer, NULL);
    if (image_view != VK_NULL_HANDLE) vkDestroyImageView(device, image_view, NULL);
    if (texture_view != VK_NULL_HANDLE) vkDestroyImageView(device, texture_view, NULL);
    if (texture_sampler != VK_NULL_HANDLE) vkDestroySampler(device, texture_sampler, NULL);
    if (render_pass != VK_NULL_HANDLE) vkDestroyRenderPass(device, render_pass, NULL);
    if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, NULL);
    if (cmd != VK_NULL_HANDLE) vkFreeCommandBuffers(device, pool, 1u, &cmd);
    if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(device, pool, NULL);
    if (descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptor_pool, NULL);
    if (texture_descriptor_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, texture_descriptor_pool, NULL);
    if (two_uniform_pool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, two_uniform_pool, NULL);
    if (linear_texture_sampler != VK_NULL_HANDLE)
        vkDestroySampler(device, linear_texture_sampler, NULL);
    if (descriptor_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, descriptor_layout, NULL);
    if (texture_descriptor_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, texture_descriptor_layout, NULL);
    if (two_uniform_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, two_uniform_layout, NULL);
    if (pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipeline_layout, NULL);
    if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, pipeline, NULL);
    if (shader != VK_NULL_HANDLE) vkDestroyShaderModule(device, shader, NULL);
    if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, NULL);
    if (dst_buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, dst_buffer, NULL);
    if (index_buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, index_buffer, NULL);
    if (image != VK_NULL_HANDLE) vkDestroyImage(device, image, NULL);
    if (copied_image != VK_NULL_HANDLE) vkDestroyImage(device, copied_image, NULL);
    if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, NULL);
    if (dst_memory != VK_NULL_HANDLE) vkFreeMemory(device, dst_memory, NULL);
    if (index_memory != VK_NULL_HANDLE) vkFreeMemory(device, index_memory, NULL);
    if (image_memory != VK_NULL_HANDLE) vkFreeMemory(device, image_memory, NULL);
    if (copied_image_memory != VK_NULL_HANDLE)
        vkFreeMemory(device, copied_image_memory, NULL);
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, NULL);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, NULL);
    if (!core_ok) return 1;
    write_text("vk_test: programmable renderer and WSI still incomplete\n");
    return 1;
}
