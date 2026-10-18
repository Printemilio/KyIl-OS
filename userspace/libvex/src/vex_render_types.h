/* Private renderer representation shared by Vulkan and GLES.
 * Vk enums are implementation details; no Vulkan API dispatch is used by GLES. */
#ifndef VEX_RENDER_TYPES_H
#define VEX_RENDER_TYPES_H
#include <vulkan/vulkan_vex.h>
#include <unos/vex_abi.h>
#define VK_VULKAN_MAX_VERTEX_INPUT_ATTRIBUTES 8u
#define VK_VULKAN_MAX_REFLECTED_DESCRIPTORS 16u
#define VK_SPV_MAX_IDS 2048u
#define VK_SPV_MAX_BLOCKS 256u
#define VK_SPV_MAX_DESCRIPTORS VK_VULKAN_MAX_REFLECTED_DESCRIPTORS
#define VK_SPV_MAX_INTERFACE_LOCATIONS VK_VULKAN_MAX_VERTEX_INPUT_ATTRIBUTES
#define VK_SPV_MAX_BLOCK_MEMBERS 16u
#define VK_SPV_MAX_EXECUTION_INSTRUCTIONS 65536u
typedef struct vk_memory_impl vk_memory_impl_t;
typedef struct vk_buffer_impl vk_buffer_impl_t;
typedef struct vk_buffer_view_impl vk_buffer_view_impl_t;
typedef struct vk_queue_impl vk_queue_impl_t;
typedef struct vk_swapchain_impl vk_swapchain_impl_t;
typedef struct vk_queue_job vk_queue_job_t;
typedef struct vk_semaphore_impl vk_semaphore_impl_t;
typedef struct vk_fence_impl vk_fence_impl_t;
typedef struct vk_descriptor_set_layout_impl vk_descriptor_set_layout_impl_t;
typedef struct vk_descriptor_set_impl vk_descriptor_set_impl_t;
typedef struct vk_spv_profile vk_spv_profile_t;
typedef struct vk_query_pool_impl vk_query_pool_impl_t;
typedef struct vk_instance_impl {
    uint8_t khr_surface_enabled;
    uint8_t vex_surface_enabled;
} vk_instance_impl_t;

typedef struct vk_surface_impl {
    uint64_t magic;
    vk_instance_impl_t *instance;
    uint64_t capability;
    uint64_t mapped_addr;
    uint64_t mapped_length;
    uint32_t *pixels;
    vex_surface_query_req_t info;
    uint32_t references;
    uint8_t destroy_requested;
    uint8_t reserved[3];
    vk_swapchain_impl_t *active_swapchain;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} vk_surface_impl_t;

typedef struct vk_device_impl {
    vk_instance_impl_t *instance;
    vk_queue_impl_t *queues;
    uint8_t swapchain_enabled;
    uint8_t uniform_array_dynamic;
    uint8_t sampled_array_dynamic;
    uint8_t storage_buffer_array_dynamic,storage_image_array_dynamic;
    uint8_t occlusion_precise;
    uint8_t logic_op;
} vk_device_impl_t;

struct vk_queue_impl {
    vk_device_impl_t *device;
    uint64_t kernel_queue;
    uint64_t last_serial;
    uint64_t work_event;
    uint32_t worker_tid;
    volatile uint32_t stop_worker;
    volatile int32_t async_error;
    vk_queue_job_t *job_head;
    vk_queue_job_t *job_tail;
    vk_queue_job_t *initial_job;
    uint32_t family;
    uint32_t index;
    struct vk_pending_present *pending_present_head;
    struct vk_pending_present *pending_present_tail;
    vk_queue_impl_t *next;
};

typedef struct vk_pending_present {
    uint64_t surface_capability;
    uint64_t serial;
    struct vk_pending_present *next;
} vk_pending_present_t;

typedef struct vk_queue_submit_batch {
    uint32_t wait_count;
    uint32_t command_count;
    uint32_t signal_count;
    vk_semaphore_impl_t **waits;
    struct vk_command_buffer_impl **commands;
    vk_semaphore_impl_t **signals;
} vk_queue_submit_batch_t;

typedef enum vk_queue_job_kind {
    VK_QUEUE_JOB_SUBMIT,
    VK_QUEUE_JOB_PRESENT
} vk_queue_job_kind_t;

struct vk_queue_job {
    vk_queue_job_t *next;
    vk_queue_job_kind_t kind;
    uint8_t payload_released;
    uint64_t serial;
    union {
        struct {
            uint32_t count;
            vk_queue_submit_batch_t *batches;
            vk_fence_impl_t *fence;
        } submit;
        struct {
            vk_swapchain_impl_t *swapchain;
            struct vk_image_impl *image;
            vk_pending_present_t *pending;
            uint32_t wait_count;
            vk_semaphore_impl_t **waits;
        } present;
    } args;
};

struct vk_swapchain_impl {
    vk_device_impl_t *device;
    vk_surface_impl_t *surface;
    uint32_t image_count;
    uint32_t next_acquire_index;
    VkImage *images;
    int32_t present_x;
    int32_t present_y;
    uint64_t available_event;
    uint8_t retired;
    uint8_t destroy_requested;
};

typedef struct vk_image_subresource_impl {
    VkDeviceSize offset;
    VkDeviceSize size;
    VkDeviceSize row_pitch;
    uint32_t width;
    uint32_t height;
    VkImageLayout layout;
    VkImageLayout scheduled_layout;
} vk_image_subresource_impl_t;

typedef struct vk_image_plane {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t samples;
    VkImageLayout layout;
    void *pixels;
} vk_image_plane_t;

typedef struct vk_image_impl {
    vk_device_impl_t *device;
    vk_swapchain_impl_t *swapchain;
    uint32_t index;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t mip_levels;
    uint32_t array_layers;
    uint32_t samples;
    VkImageTiling tiling;
    VkImageAspectFlags aspects;
    VkDeviceSize layer_pitch;
    VkDeviceSize allocation_size;
    uint64_t backing_identity;
    vk_image_subresource_impl_t *subresources;
    VkImageUsageFlags usage;
    vk_memory_impl_t *memory;
    VkDeviceSize memory_offset;
    uint32_t *owned_pixels;
    uint32_t owned_stride;
    uint64_t presentation_surface;
    uint64_t mapped_addr;
    uint64_t mapped_length;
    uint64_t available_event;
    uint64_t present_serial;
    uint32_t wsi_state;
    uint32_t references;
    uint8_t owns_pixels;
    uint8_t owns_surface;
    uint8_t destroy_requested;
} vk_image_impl_t;

typedef struct vk_image_view_impl {
    vk_image_impl_t *image;
    VkImageViewType type;
    VkImageSubresourceRange range;
    uint32_t references;
    uint8_t destroy_requested;
} vk_image_view_impl_t;

typedef struct vk_shader_module_impl {
    vk_device_impl_t *device;
    size_t code_size;
    uint32_t *code;
    uint32_t references;
    uint8_t destroy_requested;
} vk_shader_module_impl_t;

typedef struct vk_render_pass_impl {
    vk_device_impl_t *device;
    uint32_t color_format;
    uint32_t samples;
    uint32_t depth_format;
    VkAttachmentLoadOp color_load_op;
    VkAttachmentStoreOp color_store_op;
    VkAttachmentLoadOp depth_load_op;
    VkAttachmentStoreOp depth_store_op;
    VkAttachmentLoadOp stencil_load_op;
    VkAttachmentStoreOp stencil_store_op;
    VkImageLayout initial_layout;
    VkImageLayout subpass_layout;
    VkImageLayout final_layout;
    VkImageLayout depth_initial_layout;
    VkImageLayout depth_subpass_layout;
    VkImageLayout depth_final_layout;
    uint32_t color_index, depth_index, resolve_index, attachment_count;
    VkImageLayout resolve_initial_layout,resolve_subpass_layout,resolve_final_layout;
    uint8_t has_resolve;
    uint8_t has_depth;
    uint32_t references;
    uint8_t destroy_requested;
} vk_render_pass_impl_t;

typedef struct vk_framebuffer_impl {
    vk_device_impl_t *device;
    vk_render_pass_impl_t *render_pass;
    vk_image_view_impl_t *attachment0;
    vk_image_view_impl_t *depth_attachment;
    vk_image_view_impl_t *resolve_attachment;
    uint32_t width;
    uint32_t height;
    uint32_t references;
    uint8_t destroy_requested;
} vk_framebuffer_impl_t;

typedef struct vk_pipeline_layout_impl {
    vk_device_impl_t *device;
    VkDescriptorSetLayout *set_layouts;
    uint32_t set_layout_count;
    uint32_t push_constant_range_count;
    VkPushConstantRange push_ranges[4];
    uint32_t references;
    uint8_t destroy_requested;
} vk_pipeline_layout_impl_t;

typedef struct vk_draw_state {
    VkStencilOpState stencil[2];
    float blend_constants[4];
    float depth_bias_constant,depth_bias_slope;
} vk_draw_state_t;

typedef struct vk_pipeline_impl {
    vk_device_impl_t *device;
    vk_pipeline_layout_impl_t *layout;
    vk_render_pass_impl_t *render_pass;
    vk_shader_module_impl_t *vertex_shader;
    vk_shader_module_impl_t *fragment_shader;
    vk_spv_profile_t *vertex_profile;
    vk_spv_profile_t *fragment_profile;
    vk_shader_module_impl_t *compute_shader;
    vk_spv_profile_t *compute_profile;
    VkPipelineBindPoint bind_point;
    void *shader_workspace[2];
    uint32_t vertex_entry;
    uint32_t fragment_entry;
    VkViewport viewport;
    VkRect2D scissor;
    VkPrimitiveTopology topology;
    VkCullModeFlags cull_mode;
    VkFrontFace front_face;
    VkBool32 depth_test_enable;
    VkBool32 depth_write_enable;
    VkCompareOp depth_compare_op;
    uint32_t samples;
    uint32_t sample_mask;
    VkBool32 alpha_to_coverage;
    VkBool32 stencil_test_enable;
    VkStencilOpState stencil_front, stencil_back;
    VkVertexInputBindingDescription vertex_binding;
    VkVertexInputAttributeDescription vertex_attributes[8];
    VkPipelineColorBlendAttachmentState color_blend;
    float blend_constants[4];
    float depth_bias_constant,depth_bias_slope;
    uint32_t dynamic_mask;
    VkBool32 depth_bias_enable,logic_op_enable,dither_enable;
    uint8_t gl_quad;
    const uint32_t (*quad_inputs)[VK_SPV_MAX_INTERFACE_LOCATIONS][4];
    const float (*quad_coords)[4];
    /* Interpreter workspaces private to one concurrent band of rows (see
       vk_raster_rows); null means the profile's own. */
    void *workspace_override;       /* vk_spv_value_t array */
    void *quad_workspace_override;  /* four of them */
    /* GL derivatives are evaluated for a 2x2 fragment quad. Cache all four
       results so adjacent covered pixels do not interpret the same quad four
       times. The cache is invalidated at each primitive boundary. */
    uint8_t quad_cache_valid;
    uint8_t quad_cache_discard[4];
    int32_t quad_cache_x, quad_cache_y;
    uint32_t quad_cache_color[4][4];
    VkLogicOp logic_op;
    uint8_t vertex_attribute_mask;
    uint8_t dynamic_viewport;
    uint8_t dynamic_scissor;
    uint32_t references;
    uint8_t destroy_requested;
} vk_pipeline_impl_t;

typedef struct vk_command_pool_impl {
    vk_device_impl_t *device;
    struct vk_command_buffer_impl *first_buffer;
    VkCommandPoolCreateFlags flags;
} vk_command_pool_impl_t;

typedef struct vk_event_impl {
    vk_device_impl_t *device;
    uint64_t wake_counter;
    uint32_t signaled,waiters,references;
    uint8_t destroy_requested;
} vk_event_impl_t;

typedef enum vk_record_kind {
    VK_RECORD_CLEAR_COLOR,
    VK_RECORD_CLEAR_ATTACHMENTS,
    VK_RECORD_CLEAR_DEPTH_STENCIL,
    VK_RECORD_COPY_BUFFER,
    VK_RECORD_WRITE_BUFFER,
    VK_RECORD_MEMORY_BARRIER,
    VK_RECORD_EVENT_SET,
    VK_RECORD_EVENT_RESET,
    VK_RECORD_EVENT_WAIT,
    VK_RECORD_QUERY_RESET,
    VK_RECORD_QUERY_BEGIN,
    VK_RECORD_QUERY_END,
    VK_RECORD_QUERY_COPY,
    VK_RECORD_IMAGE_BARRIER,
    VK_RECORD_COPY_BUFFER_TO_IMAGE,
    VK_RECORD_COPY_IMAGE_TO_BUFFER,
    VK_RECORD_COPY_IMAGE,
    VK_RECORD_RESOLVE_IMAGE,
    VK_RECORD_BLIT_IMAGE,
    VK_RECORD_BIND_DESCRIPTOR_SET,
    VK_RECORD_BEGIN_RENDER_PASS,
    VK_RECORD_END_RENDER_PASS,
    VK_RECORD_DRAW,
    VK_RECORD_DISPATCH,
    VK_RECORD_EXECUTE_SECONDARY
} vk_record_kind_t;

typedef struct vk_recorded_command {
    struct vk_recorded_command *next;
    vk_record_kind_t kind;
    union {
        struct {struct vk_command_buffer_impl *command;uint64_t generation;} secondary;
        struct {
            vk_pipeline_impl_t *pipeline;
            vk_descriptor_set_impl_t *sets[8];
            vk_buffer_impl_t *indirect;
            VkDeviceSize offset;
            uint32_t groups[3];
            uint8_t push[128];
        } dispatch;
        struct {vk_buffer_impl_t *buffer;VkDeviceSize offset,size;uint32_t value;uint8_t *data;} write_buffer;
        struct {vk_buffer_impl_t *buffer;} memory_barrier;
        struct {vk_event_impl_t *event;} event;
        struct {vk_query_pool_impl_t *pool;uint32_t first,count;vk_buffer_impl_t *buffer;VkDeviceSize offset,stride;VkQueryResultFlags flags;} query;
        struct {
            vk_image_impl_t *image;
            uint32_t color;
            VkImageLayout layout;
            VkImageSubresourceRange range;
        } clear;
        struct {
            vk_image_impl_t *image;
            float depth;
            uint32_t stencil;
            VkImageLayout layout;
            VkImageSubresourceRange range;
        } clear_depth_stencil;
        struct {
            vk_buffer_impl_t *src;
            vk_buffer_impl_t *dst;
            VkBufferCopy region;
        } copy_buffer;
        struct {
            vk_image_impl_t *image;
            VkImageLayout old_layout;
            VkImageLayout new_layout;
            VkImageSubresourceRange range;
        } image_barrier;
        struct {
            vk_buffer_impl_t *src;
            vk_image_impl_t *dst;
            VkImageLayout layout;
            VkBufferImageCopy region;
        } copy_buffer_to_image;
        struct {
            vk_image_impl_t *src;
            vk_buffer_impl_t *dst;
            VkImageLayout layout;
            VkBufferImageCopy region;
        } copy_image_to_buffer;
        struct {
            vk_image_impl_t *src;
            vk_image_impl_t *dst;
            VkImageLayout src_layout;
            VkImageLayout dst_layout;
            VkImageCopy region;
        } copy_image;
        struct {
            vk_image_impl_t *src;
            vk_image_impl_t *dst;
            VkImageLayout src_layout;
            VkImageLayout dst_layout;
            VkImageBlit region;
            VkFilter filter;
        } blit_image;
        struct {
            struct vk_descriptor_set_impl *set;
            uint32_t first_set;
        } bind_descriptor_set;
        struct {
            vk_framebuffer_impl_t *framebuffer;
            VkRect2D area;
            uint32_t clear_color;
            float clear_depth;
            uint32_t clear_stencil;
        } begin_render_pass;
        struct {
            vk_image_impl_t *image;
            vk_image_impl_t *depth_image;
            uint32_t subresource;
            uint32_t depth_subresource;
            VkImageLayout expected_layout;
            VkImageLayout final_layout;
            VkImageLayout depth_expected_layout;
            VkImageLayout depth_final_layout;
            vk_framebuffer_impl_t *resolve_framebuffer;
            VkRect2D resolve_area;
        } end_render_pass;
        struct {
            vk_query_pool_impl_t *query_pool;uint32_t query_index;
            vk_pipeline_impl_t *pipeline;
            vk_framebuffer_impl_t *framebuffer;
            VkRect2D render_area;
            uint32_t vertex_count;
            uint32_t instance_count;
            uint32_t first_vertex;
            uint32_t first_instance;
            vk_buffer_impl_t *index_buffer;
            VkDeviceSize index_buffer_offset;
            VkIndexType index_type;
            VkViewport viewport;
            VkRect2D scissor;
            vk_draw_state_t state;
            uint32_t first_index;
            int32_t vertex_offset;
            uint8_t indexed;
            vk_descriptor_set_impl_t *descriptor_sets[8];
            vk_buffer_impl_t *vertex_buffer;
            VkDeviceSize vertex_buffer_offset;
            uint8_t push_constants[3][128];
            uint32_t push_constant_initialized[3];
        } draw;
        struct {
            vk_framebuffer_impl_t *framebuffer;
            VkRect2D rect;
            VkImageAspectFlags aspects;
            VkClearValue value;
        } clear_attachments;
    } args;
} vk_recorded_command_t;

typedef struct vk_command_buffer_impl {
    vk_device_impl_t *device;
    vk_command_pool_impl_t *pool;
    struct vk_command_buffer_impl *next_in_pool;
    vk_recorded_command_t *first_record;
    vk_recorded_command_t *last_record;
    VkResult record_error;
    uint8_t recording;
    uint8_t executable;
    uint8_t destroy_requested;
    uint8_t reserved_lifetime;
    VkCommandBufferUsageFlags usage_flags;
    uint32_t references;
    uint32_t pending_submissions;
    VkImage bound_image;
    vk_framebuffer_impl_t *framebuffer;
    vk_pipeline_impl_t *pipeline;
    uint32_t level;
    uint64_t generation;
    vk_pipeline_impl_t *compute_pipeline;
    vk_descriptor_set_impl_t *compute_descriptor_sets[8];
    vk_descriptor_set_impl_t *bound_descriptor_sets[8];
    vk_buffer_impl_t *vertex_buffer;
    VkDeviceSize vertex_buffer_offset;
    vk_buffer_impl_t *index_buffer;
    VkDeviceSize index_buffer_offset;
    VkIndexType index_type;
    VkViewport viewport;
    VkRect2D scissor;
    uint8_t viewport_set;
    uint8_t scissor_set;
    uint32_t dynamic_set;
    vk_draw_state_t draw_state;
    vk_framebuffer_impl_t *active_framebuffer;
    VkRect2D active_render_area;
    uint8_t active_render_pass;
    uint8_t push_constants[3][128];
    uint32_t push_constant_initialized[3];
    vk_query_pool_impl_t *active_query;uint32_t active_query_index;
} vk_command_buffer_impl_t;

struct vk_memory_impl {
    vk_device_impl_t *device;
    uint64_t backing_identity;
    uint8_t *bytes;
    VkDeviceSize size;
    VkDeviceSize backing_size;
    uint64_t surface_capability;
    VkDeviceSize host_offset, host_size;
    uint8_t host_mapped;
    uint64_t mapped_addr;
    uint32_t references;
    uint8_t destroy_requested;
};

typedef struct vk_buffer_impl {
    vk_device_impl_t *device;
    VkDeviceSize size;
    VkFlags usage;
    vk_memory_impl_t *memory;
    VkDeviceSize memory_offset;
    uint32_t references;
    uint8_t destroy_requested;
} vk_buffer_impl_t;

typedef struct vk_submit_semaphore_state {
    vk_semaphore_impl_t *semaphore;
    uint32_t wait_submit;
    uint32_t signal_submit;
    uint32_t pending_external_waits;
    VkBool32 signaled;
    VkBool32 initial_signaled;
    VkBool32 unresolved_wait;
} vk_submit_semaphore_state_t;

typedef struct vk_descriptor_layout_binding_impl {
    VkDescriptorSetLayoutBinding binding;
    uint32_t value_offset;
} vk_descriptor_layout_binding_impl_t;

struct vk_descriptor_set_layout_impl {
    vk_device_impl_t *device;
    vk_descriptor_layout_binding_impl_t *bindings;
    uint32_t binding_count;
    uint32_t value_count;
    uint32_t references;
    uint8_t destroy_requested;
};

typedef struct vk_descriptor_value_impl {
    VkDescriptorType type;
    uint8_t initialized;
    union {
        struct {
            vk_buffer_impl_t *buffer;
            VkDeviceSize offset;
            VkDeviceSize range;
        } buffer;
        struct {
            vk_image_view_impl_t *view;
            struct vk_sampler_impl *sampler;
            VkImageLayout layout;
        } image;
        vk_buffer_view_impl_t *texel;
    } resource;
} vk_descriptor_value_impl_t;

typedef struct vk_descriptor_pool_impl {
    vk_device_impl_t *device;
    uint32_t max_sets;
    uint32_t allocated;
    VkDescriptorPoolCreateFlags flags;
    uint32_t capacity[32];
    uint32_t used[32];
    struct vk_descriptor_set_impl *sets;
    uint32_t references;
    uint8_t destroy_requested;
} vk_descriptor_pool_impl_t;

typedef struct vk_descriptor_set_impl {
    struct vk_descriptor_set_impl *pool_next;
    vk_descriptor_pool_impl_t *pool;
    vk_descriptor_set_layout_impl_t *layout;
    vk_descriptor_value_impl_t *values;
    uint32_t value_count;
    uint32_t references;
    uint32_t pending_submissions;
    uint8_t allocated;
    uint8_t destroy_requested;
} vk_descriptor_set_impl_t;

typedef struct vk_sampler_impl {
    vk_device_impl_t *device;
    uint32_t mag_filter;
    uint32_t min_filter;
    uint32_t mipmap_mode;
    uint32_t address_mode_u;
    uint32_t address_mode_v;
    float    max_anisotropy;
    VkBool32 compare_enable;
    uint32_t compare_op;
    float    min_lod;
    float    max_lod;
    uint32_t references;
    uint8_t destroy_requested;
} vk_sampler_impl_t;

typedef struct vk_query_slot {uint64_t samples;uint32_t state;} vk_query_slot_t;

struct vk_query_pool_impl {
    vk_device_impl_t *device;
    uint32_t query_count,references,waiters;
    uint64_t wake_counter;
    vk_query_slot_t *slots;
    uint8_t destroy_requested;
};

enum {
    VK_VERTEX_FORMAT_FLOAT32 = 1u,
    VK_VERTEX_FORMAT_UNORM8 = 2u,
    VK_VERTEX_FORMAT_SNORM8 = 3u
};

struct vk_spv_profile {
    uint8_t gl_environment;
    uint8_t gl_needs_quad;
    void *quad_workspace;
    uint32_t gl_top_left_height;
    uint8_t gl_uniform[VK_SPV_MAX_IDS];
    const uint32_t (*gl_uniform_words)[16];
    uint8_t kind[VK_SPV_MAX_IDS];
    uint8_t storage[VK_SPV_MAX_IDS];
    uint8_t variable[VK_SPV_MAX_IDS];
    uint8_t defined[VK_SPV_MAX_IDS];
    uint8_t block[VK_SPV_MAX_IDS];
    uint8_t buffer_block[VK_SPV_MAX_IDS];
    uint8_t image_arrayed[VK_SPV_MAX_IDS],image_sampled[VK_SPV_MAX_IDS];
    uint8_t image_dim[VK_SPV_MAX_IDS],image_format[VK_SPV_MAX_IDS];
    uint8_t access[VK_SPV_MAX_IDS];
    uint8_t member_access[VK_SPV_MAX_IDS][VK_SPV_MAX_BLOCK_MEMBERS];
    uint32_t workgroup_offset[VK_SPV_MAX_IDS];
    uint32_t workgroup_size;
    uint32_t local_size[3];
    uint32_t compute_builtin[6];
    uint8_t member0_offset_zero[VK_SPV_MAX_IDS];
    uint8_t member_offset_set[VK_SPV_MAX_IDS][VK_SPV_MAX_BLOCK_MEMBERS];
    uint8_t member_col_major[VK_SPV_MAX_IDS][VK_SPV_MAX_BLOCK_MEMBERS];
    uint8_t member_matrix_stride_set[VK_SPV_MAX_IDS][VK_SPV_MAX_BLOCK_MEMBERS];
    uint32_t element[VK_SPV_MAX_IDS];
    uint32_t type_of[VK_SPV_MAX_IDS];
    uint32_t count[VK_SPV_MAX_IDS];
    uint32_t width[VK_SPV_MAX_IDS];
    uint32_t signedness[VK_SPV_MAX_IDS];
    uint32_t member_type[VK_SPV_MAX_IDS][VK_SPV_MAX_BLOCK_MEMBERS];
    uint32_t member_offset[VK_SPV_MAX_IDS][VK_SPV_MAX_BLOCK_MEMBERS];
    uint32_t member_matrix_stride[VK_SPV_MAX_IDS][VK_SPV_MAX_BLOCK_MEMBERS];
    uint32_t descriptor_required_size[VK_SPV_MAX_IDS];
    uint32_t descriptor_elements[VK_SPV_MAX_IDS];
    uint32_t constant_value[VK_SPV_MAX_IDS];
    uint32_t array_stride[VK_SPV_MAX_IDS];
    uint8_t constant_integer[VK_SPV_MAX_IDS];
    uint32_t builtin[VK_SPV_MAX_IDS];
    uint32_t location[VK_SPV_MAX_IDS];
    uint32_t descriptor_set[VK_SPV_MAX_IDS];
    uint32_t descriptor_binding[VK_SPV_MAX_IDS];
    uint8_t descriptor_type_by_id[VK_SPV_MAX_IDS];
    uint32_t label_word[VK_SPV_MAX_IDS];
    uint32_t function_start[VK_SPV_MAX_IDS],function_end[VK_SPV_MAX_IDS],function_type[VK_SPV_MAX_IDS];
    uint32_t function_parameter[VK_SPV_MAX_IDS][VK_SPV_MAX_BLOCK_MEMBERS];
    uint8_t function_parameters[VK_SPV_MAX_IDS];
    uint32_t owner[VK_SPV_MAX_IDS];
    uint32_t loop_merge_label[VK_SPV_MAX_IDS];
    uint32_t loop_continue_label[VK_SPV_MAX_IDS];
    uint8_t loop_header[VK_SPV_MAX_IDS];
    uint32_t glsl_std450_import_id;
    uint32_t function_id;
    uint32_t function_word;
    uint32_t function_end_word;
    uint32_t input_id;
    uint32_t instance_id;
    uint32_t front_facing_id;
    uint32_t frag_coord_id;
    uint32_t position_id;
    uint32_t output_id;
    uint32_t attribute_id[VK_SPV_MAX_INTERFACE_LOCATIONS];
    uint32_t vertex_varying_id[VK_SPV_MAX_INTERFACE_LOCATIONS];
    uint32_t fragment_varying_id[VK_SPV_MAX_INTERFACE_LOCATIONS];
    uint8_t attribute_components[VK_SPV_MAX_INTERFACE_LOCATIONS];
    uint8_t vertex_varying_components[VK_SPV_MAX_INTERFACE_LOCATIONS];
    uint8_t fragment_varying_components[VK_SPV_MAX_INTERFACE_LOCATIONS];
    uint8_t attribute_mask;
    uint8_t vertex_varying_mask;
    uint8_t fragment_varying_mask;
    uint32_t push_constant_variable_id;
    uint32_t push_word_mask;
    void *workspace;
    /* GL programs only (allocated by vex_render_program_create): the module's
     * constants and variables as the entry function starts, valid for one draw
     * (vex_render_draw clears `prologue_ready`, since uniforms may have changed).
     * Every invocation copies this instead of rebuilding it from the module.
     * `entry_offset` is the word of the first instruction of the entry function. */
    void *prologue;
    uint8_t prologue_ready;
    uint8_t prologue_filling;   /* one thread fills the snapshot per draw */
    uint32_t entry_offset;
    /* Fragment programs recognised as one of the few Skia shaders that make up
     * nearly every compositor pixel (vex_render_program_classify): evaluated by
     * native code in vex_render_quad.h instead of the interpreter. `fast_location`
     * are the varying locations the program reads, `fast_binding` the descriptor
     * binding of its sampler and `fast_bias` the LOD bias of its texture2D(). */
    uint8_t fast_kind;
    uint8_t fast_location[2];
    uint32_t fast_binding;
    float fast_bias;
    uint32_t bound;
    uint8_t descriptor_count;
    uint8_t push_constant_count;
    uint8_t stage;
    uint8_t memory_model;
    uint8_t shader_capability;
    uint8_t uniform_array_dynamic;
    uint8_t sampled_array_dynamic;
    uint8_t storage_buffer_array_dynamic,storage_image_array_dynamic;
    uint8_t implicit_samples;
    uint8_t fragment_origin_upper_left;
};

typedef struct vk_spv_value {
    uint32_t type;
    uint32_t word[16];
    uint32_t descriptor_set;
    uint32_t descriptor_binding;
    uint32_t descriptor_byte_offset;
    uint32_t descriptor_index;
    uint16_t local_written;
    uint16_t local_offset;
    uint32_t local_base;
    uint8_t memory_pointer;
    uint8_t access;
    uint32_t memory_base;
    uint8_t local_pointer;
    uint8_t initialized;
    uint8_t descriptor_pointer;
    uint8_t push_constant_pointer;
} vk_spv_value_t;

enum {
    VK_SPV_KIND_VOID = 1,
    VK_SPV_KIND_BOOL,
    VK_SPV_KIND_INT,
    VK_SPV_KIND_FLOAT,
    VK_SPV_KIND_VECTOR,
    VK_SPV_KIND_MATRIX,
    VK_SPV_KIND_POINTER,
    VK_SPV_KIND_FUNCTION,
    VK_SPV_KIND_STRUCT,
    VK_SPV_KIND_IMAGE,
    VK_SPV_KIND_SAMPLED_IMAGE,
    VK_SPV_KIND_ARRAY,
    VK_SPV_KIND_RUNTIME_ARRAY
};

typedef struct vk_spv_invocation {
    vk_spv_value_t *values;
    uint8_t *shared;
    uint32_t groups[3],group[3],local[3],global[3];
    uint32_t pc,label,predecessor,function,call_depth,instructions,barrier;
    uint32_t fault_pc,fault_opcode;
    uint8_t started,done;
    uint8_t graphics_quad;
    struct {uint32_t offset,label,predecessor,function,result,type;} calls[32];
} vk_spv_invocation_t;

typedef struct vk_buffer_view_impl {
    vk_device_impl_t *device;
    vk_buffer_impl_t *buffer;
    VkDeviceSize offset,range;
    VkFormat format;
    uint32_t references;
    uint8_t destroy_requested;
} vk_buffer_view_impl_t;
typedef struct vk_clip_vertex {
    uint32_t position[4];
    uint32_t varying[VK_SPV_MAX_INTERFACE_LOCATIONS][4];
} vk_clip_vertex_t;
#endif
