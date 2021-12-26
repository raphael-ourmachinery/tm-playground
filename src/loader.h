#pragma once

#include <foundation/api_types.h>

extern struct tm_api_registry_api *tm_global_api_registry;

extern struct tm_allocator_api *tm_allocator_api;
extern struct tm_buffer_format_api *tm_buffer_format_api;
extern struct tm_camera_api *tm_camera_api;
extern struct tm_error_api *tm_error_api;
extern struct tm_input_api *tm_input_api;
extern struct tm_localizer_api *tm_localizer_api;
extern struct tm_logger_api *tm_logger_api;
extern struct tm_memory_tracker_api *tm_memory_tracker_api;
extern struct tm_os_api *tm_os_api;
extern struct tm_path_api *tm_path_api;
extern struct tm_plugins_api *tm_plugins_api;
extern struct tm_profiler_api *tm_profiler_api;
extern struct tm_sprintf_api *tm_sprintf_api;
extern struct tm_unicode_api *tm_unicode_api;
extern struct tm_temp_allocator_api *tm_temp_allocator_api;
extern struct tm_the_truth_api *tm_the_truth_api;

extern struct tm_default_render_pipe_api *tm_default_render_pipe_api;
extern struct tm_draw2d_api *tm_draw2d_api;
extern struct tm_dxc_shader_compiler_api *tm_dxc_shader_compiler_api;
extern struct tm_docking_api *tm_docking_api;
extern struct tm_font_api *tm_font_api;
extern struct tm_os_display_api *tm_os_display_api;
extern struct tm_os_window_api *tm_os_window_api;
extern struct tm_properties_view_api *tm_properties_view_api;
extern struct tm_renderer_api *tm_renderer_api;
extern struct tm_render_graph_api *tm_render_graph_api;
extern struct tm_render_graph_module_api *tm_render_graph_module_api;
extern struct tm_renderer_init_api *tm_renderer_init_api;
extern struct tm_shader_api *tm_shader_api;
extern struct tm_shader_declaration_api *tm_shader_declaration_api;
extern struct tm_shader_repository_api *tm_shader_repository_api;
extern struct tm_shader_system_api *tm_shader_system_api;
extern struct tm_ui_api *tm_ui_api;
extern struct tm_vulkan_api *tm_vulkan_api;
extern struct tm_ui_renderer_api *tm_ui_renderer_api;
extern struct tm_font_library_api *tm_font_library_api;

extern struct tm_renderer_command_buffer_api *tm_cmd_buf_api;
extern struct tm_renderer_resource_command_buffer_api *tm_res_buf_api;

static const float window_margin = 1.0f;
static const float window_padding = 4.0f;
static const float caption_height = 30.0f;

