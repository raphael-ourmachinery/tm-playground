#include "loader.h"

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/application.h>
#include <foundation/camera.h>
#include <foundation/carray.inl>
#include <foundation/color_spaces.h>
#include <foundation/error.h>
#include <foundation/input.h>
#include <foundation/localizer.h>
#include <foundation/log.h>
#include <foundation/math.inl>
#include <foundation/memory_tracker.h>
#include <foundation/murmurhash64a.inl>
#include <foundation/os.h>
#include <foundation/path.h>
#include <foundation/plugin.h>
#include <foundation/profiler.h>
#include <foundation/rect.inl>
#include <foundation/sprintf.h>
#include <foundation/string.inl>
#include <foundation/unicode.h>
#include <foundation/the_truth.h>

#include <plugins/os_window/os_window.h>

struct tm_api_registry_api *tm_global_api_registry;

struct tm_allocator_api *tm_allocator_api;
struct tm_camera_api *tm_camera_api;
struct tm_error_api *tm_error_api;
struct tm_input_api *tm_input_api;
struct tm_localizer_api *tm_localizer_api;
struct tm_logger_api *tm_logger_api;
struct tm_memory_tracker_api *tm_memory_tracker_api;
struct tm_os_api *tm_os_api;
struct tm_path_api *tm_path_api;
struct tm_plugins_api *tm_plugins_api;
struct tm_profiler_api *tm_profiler_api;
struct tm_sprintf_api *tm_sprintf_api;
struct tm_unicode_api *tm_unicode_api;
struct tm_temp_allocator_api *tm_temp_allocator_api;
struct tm_the_truth_api *tm_the_truth_api;

struct tm_os_display_api *tm_os_display_api;
struct tm_os_window_api *tm_os_window_api;

extern void tm_path_tracing_app_load_plugin(struct tm_api_registry_api *reg, bool load);

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api *reg, bool load)
{
    tm_global_api_registry = reg;

    // Get foundation apis
    tm_allocator_api = tm_get_api(reg, tm_allocator_api);
    tm_camera_api = tm_get_api(reg, tm_camera_api);
    tm_error_api = tm_get_api(reg, tm_error_api);
    tm_input_api = tm_get_api(reg, tm_input_api);
    tm_localizer_api = tm_get_api(reg, tm_localizer_api);
    tm_logger_api = tm_get_api(reg, tm_logger_api);
    tm_memory_tracker_api = tm_get_api(reg, tm_memory_tracker_api);
    tm_os_api = tm_get_api(reg, tm_os_api);
    tm_path_api = tm_get_api(reg, tm_path_api);
    tm_plugins_api = tm_get_api(reg, tm_plugins_api);
    tm_profiler_api = tm_get_api(reg, tm_profiler_api);
    tm_sprintf_api = tm_get_api(reg, tm_sprintf_api);
    tm_unicode_api = tm_get_api(reg, tm_unicode_api);
    tm_temp_allocator_api = tm_get_api(reg, tm_temp_allocator_api);
    tm_the_truth_api = tm_get_api(reg, tm_the_truth_api);

    // Get APIs
    tm_os_display_api = tm_get_api(reg, tm_os_display_api);
    tm_os_window_api = tm_get_api(reg, tm_os_window_api);

    tm_path_tracing_app_load_plugin(reg, load);
}
