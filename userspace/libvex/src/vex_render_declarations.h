/* Private renderer functions; defined once per runtime adapter. */
static int32_t clamp_i32(int32_t value, int32_t min_value, int32_t max_value);
static void fill_depth_rect(vk_image_plane_t *image, int32_t x, int32_t y,
                            int32_t w, int32_t h, float depth);
static void fill_image_rect(vk_image_plane_t *image, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
static void fill_stencil_rect(vk_image_plane_t *plane, int32_t x, int32_t y,
        int32_t width, int32_t height, uint32_t value);
static uint32_t pack_rgba(const VkClearColorValue *color);
static uint32_t vk_apply_color_write_mask(uint32_t source, uint32_t destination,
                                          VkColorComponentFlags write_mask);
static uint32_t vk_aspect_offset(VkFormat f,VkImageAspectFlags a);
static uint8_t vk_blend_channel_byte(float value);
static float vk_blend_component(VkBlendOp op, float source, float destination,
                                float source_factor, float destination_factor);
static float vk_blend_factor_value(VkBlendFactor factor, uint32_t component,
                                   const float source[4], const float destination[4],
                                   const float constants[4]);
static uint32_t vk_blend_pixel(const float source_color[4], uint32_t destination_pixel,
                               const vk_pipeline_impl_t *pipeline,const float constants[4],float dither);
static float vk_clip_distance(const vk_clip_vertex_t *vertex, uint32_t plane);
static int vk_clip_float_is_finite(float value);
static int vk_clip_vertex_lerp(const vk_clip_vertex_t *a,
                               const vk_clip_vertex_t *b,
                               float t, vk_clip_vertex_t *out);
static int vk_depth_compare(VkCompareOp operation, float incoming, float stored);
static void vk_draw_raster_bounds(const vk_recorded_command_t *record,
                                  const vk_image_plane_t *image,
                                  int32_t *x0, int32_t *y0,
                                  int32_t *x1, int32_t *y1);
static int vk_draw_vertex_index(const vk_recorded_command_t *record,
                                uint32_t ordinal, uint32_t *vertex_index_out);
static float vk_edge(float ax, float ay, float bx, float by, float px, float py);
static int vk_edge_covers_sample(float edge, float area,
                                 float ax, float ay, float bx, float by);
static VkResult vk_execute_draw(const vk_recorded_command_t *record);
static int vk_format_has_depth(uint32_t f);
static int vk_format_has_stencil(uint32_t f);
static int vk_format_is_color_unorm(uint32_t format);
static uint32_t vk_format_texel_bytes(VkFormat format);
static int vk_image_plane(vk_image_impl_t *image, uint32_t mip, uint32_t layer,
                           vk_image_plane_t *plane);
static vk_image_subresource_impl_t *vk_image_subresource(
        const vk_image_impl_t *image, uint32_t mip, uint32_t layer);
static void *vk_image_subresource_pixels(vk_image_impl_t *image,
                                             uint32_t mip, uint32_t layer);
static uint32_t vk_logic_pixel(VkLogicOp op,uint32_t s,uint32_t d);
static uint32_t vk_pixel_from_canonical(uint32_t format, uint32_t pixel);
static uint32_t vk_pixel_to_canonical(uint32_t format, uint32_t pixel);
static VkResult vk_rasterize_clip_triangle(const vk_recorded_command_t *record,
                                           vk_pipeline_impl_t *pipeline,
                                           vk_image_plane_t *image, uint32_t *pixels,
                                           const uint32_t pos[3][4],
                                           const uint32_t varying[3][VK_SPV_MAX_INTERFACE_LOCATIONS][4]);
static VkResult vk_rasterize_line(const vk_recorded_command_t *record,
                                 vk_pipeline_impl_t *pipeline,
                                 vk_image_plane_t *image, uint32_t *pixels,
                                 uint32_t vertex_index0, uint32_t vertex_index1);
static VkResult vk_rasterize_point(const vk_recorded_command_t *record,
                                   vk_pipeline_impl_t *pipeline,
                                   vk_image_plane_t *image, uint32_t *pixels,
                                   uint32_t vertex_index);
static VkResult vk_rasterize_triangle(const vk_recorded_command_t *record,
                                      vk_pipeline_impl_t *pipeline,
                                      vk_image_plane_t *image, uint32_t *pixels,
                                      const uint32_t vertex_indices[3]);
static int vk_read_vertex_attribute(const vk_recorded_command_t *record,
                                    uint32_t vertex_index, uint32_t location,
                                    uint32_t words[4]);
static int vk_run_draw_vertex(const vk_recorded_command_t *record,
                              vk_pipeline_impl_t *pipeline,
                              uint32_t vertex_index, vk_clip_vertex_t *out);
static uint32_t vk_sample_linear_pixel(const uint32_t *pixels, uint32_t format,
                                      uint32_t width, uint32_t height,
                                      float u, float v,
                                      uint32_t address_u, uint32_t address_v);
static void vk_sample_location(uint32_t n,uint32_t sample,float *x,float *y);
static uint32_t vk_sampler_address_index(int32_t index, uint32_t extent,
                                         uint32_t address_mode);
static VkResult vk_shade_fragment(const vk_recorded_command_t *record,
        vk_pipeline_impl_t *pipeline, vk_image_plane_t *image, uint32_t *pixels,
        float *depth_values, int32_t x, int32_t y, float depth,
        const uint32_t varyings[VK_SPV_MAX_INTERFACE_LOCATIONS][4], int front_facing,const float coord[4]);
static VkResult vk_shade_sample(const vk_recorded_command_t *record,
                                  vk_pipeline_impl_t *pipeline,
                                  vk_image_plane_t *image, uint32_t *pixels,
                                  float *depth_values, int32_t x, int32_t y,
                                  float depth,
                                  const float fragment_coord[4],
                                  const uint32_t varyings[VK_SPV_MAX_INTERFACE_LOCATIONS][4], int front_facing, uint32_t sample);
static int vk_spv_arithmetic_type(const vk_spv_profile_t *p,uint32_t a,uint32_t b);
static int vk_spv_atomic(const vk_spv_profile_t *p,vk_spv_value_t *v,
        vk_descriptor_set_impl_t *const sets[8],vk_spv_invocation_t *inv,const uint32_t *w,uint32_t op);
static int vk_spv_barrier_valid(const vk_spv_profile_t *p,const uint32_t *w,int control);
static int vk_spv_block_layout(const vk_spv_profile_t *p, uint32_t type,
        uint32_t depth, uint32_t *alignment, uint32_t *size, int std140);
static int vk_spv_call_graph(const vk_spv_profile_t *p,const uint32_t *code,uint32_t function,uint32_t depth,uint8_t *state);
static int vk_spv_composite_child(const vk_spv_profile_t *p,uint32_t type,uint32_t index,uint32_t *child,uint32_t *offset);
static int vk_spv_define(vk_spv_profile_t *profile, uint32_t id);
static int vk_spv_descriptor_type_for_variable(vk_spv_profile_t *profile,
                                               uint32_t variable,
                                               VkDescriptorType *type_out);
static vk_descriptor_value_impl_t *vk_spv_descriptor_value(
        vk_descriptor_set_impl_t *const sets[8],uint32_t set,uint32_t binding,uint32_t element);
static int vk_spv_failure(vk_spv_invocation_t *inv,uint32_t line);
static uint32_t vk_spv_float_word(float value);
static int vk_spv_get_combined_image(
    vk_descriptor_set_impl_t *const bound_sets[8], uint32_t set_number,
    uint32_t binding_number, uint32_t element_index, vk_image_view_impl_t **view_out,
    vk_sampler_impl_t **sampler_out, VkImageLayout *descriptor_layout_out);
static int vk_spv_instruction_supported_in_function(uint32_t opcode, uint32_t word_count);
static uint32_t vk_spv_interface_f32_components(const vk_spv_profile_t *profile,
                                                uint32_t variable,
                                                uint32_t storage);
static int vk_spv_interface_vec4_f32(const vk_spv_profile_t *profile,
                                     uint32_t variable, uint32_t storage);
static int vk_spv_memory_chain(const vk_spv_profile_t *p,vk_spv_value_t *v,uint32_t base,
        uint32_t result_type,uint32_t result,const uint32_t *indices,uint32_t count);
static int vk_spv_memory_semantics(uint32_t scope,uint32_t semantics);
static int vk_spv_parse_profile(const vk_shader_module_impl_t *shader,
                                VkShaderStageFlagBits stage,
                                vk_spv_profile_t *profile);
static int vk_spv_parse_string(const uint32_t *words, uint32_t word_count,
                               uint32_t start, const char *expected,
                               uint32_t *consumed_words);
static int vk_spv_pointer_address(const vk_spv_profile_t *p,vk_spv_value_t *values,
        uint32_t pointer,vk_descriptor_set_impl_t *const sets[8],vk_spv_invocation_t *inv,
        uint32_t bytes,int write,uint8_t **out);
static uint32_t vk_spv_popcount8(uint8_t value);
static int vk_spv_profile_layout_supported(const vk_spv_profile_t *profile,
                                           vk_pipeline_layout_impl_t *layout,
                                           VkShaderStageFlagBits stage);
static int vk_spv_profile_push_initialized(const vk_spv_profile_t *profile,
                                           VkShaderStageFlagBits stage,
                                           const uint32_t initialized[3]);
static int vk_spv_push_words(const vk_spv_profile_t *p,uint32_t type,uint32_t offset,uint32_t depth,uint32_t *mask);
static int vk_spv_resources_supported(const vk_spv_profile_t *p,
        vk_descriptor_set_impl_t *const sets[8]);
static int vk_spv_run(const vk_shader_module_impl_t *shader,
                      const vk_spv_profile_t *profile_data,
                      VkShaderStageFlagBits stage, uint32_t vertex_index,uint32_t instance_index,int front_facing,
                      const uint32_t vertex_attribute_words[VK_SPV_MAX_INTERFACE_LOCATIONS][4],
                      vk_descriptor_set_impl_t *const bound_sets[8],
                      const uint8_t push_constants[128],
                      const uint32_t fragment_input_words[VK_SPV_MAX_INTERFACE_LOCATIONS][4],
                      const float fragment_coord[4],
                      uint32_t output_words[4],
                      uint32_t varying_words[VK_SPV_MAX_INTERFACE_LOCATIONS][4],
                      vk_spv_invocation_t *invocation);
static int vk_spv_sample_combined_image(
    vk_descriptor_set_impl_t *const bound_sets[8], uint32_t set_number,
    uint32_t binding_number, uint32_t element_index, const uint32_t coordinate_words[4],
    int explicit_lod, float lod, uint32_t result_words[4]);
static int vk_spv_sample_plane(vk_image_view_impl_t *view,
    vk_sampler_impl_t *sampler, VkImageLayout descriptor_layout, uint32_t mip,
    const uint32_t coordinate_words[4], uint32_t result_words[4]);
static int vk_spv_scalar_and_components(const vk_spv_profile_t *profile,
                                        uint32_t type_id,
                                        uint32_t *scalar_type,
                                        uint32_t *components);
static uint32_t vk_spv_shared_bytes(const vk_spv_profile_t *p,uint32_t t,uint32_t depth);
static int vk_spv_stage_index(VkShaderStageFlagBits stage);
static int vk_spv_static_memory_valid(const vk_shader_module_impl_t *shader,const vk_spv_profile_t *p);
static int vk_spv_storage_image(const vk_spv_profile_t *p,vk_spv_value_t *values,
        vk_descriptor_set_impl_t *const sets[8],const uint32_t *w,uint32_t op);
static int vk_spv_texel(const vk_spv_profile_t *p,vk_spv_value_t *v,
        vk_descriptor_set_impl_t *const sets[8],const uint32_t *w,uint32_t op);
static uint32_t vk_spv_value_words(const vk_spv_profile_t *p,uint32_t type,uint32_t depth);
static float vk_spv_word_float(uint32_t word);
static void vk_stencil_update(uint8_t *byte, const VkStencilOpState *state, VkStencilOp op);
static int vk_texel_format(VkFormat format,uint32_t *kind,uint32_t *components);
static int vk_validate_draw_vertex_buffer(const vk_recorded_command_t *record);
static int vk_validate_vertex_index(const vk_recorded_command_t *record,
                                    uint32_t vertex_index);
static int vk_vertex_format_info(VkFormat format, uint32_t *components_out,
                                 uint32_t *size_out, uint8_t *encoding_out,
                                 uint8_t *bgra_out);
static float *vk_view_depth_values(vk_image_view_impl_t *view);
static int vk_view_plane(vk_image_view_impl_t *view, vk_image_plane_t *plane);
