#include "loader.h"

#include <foundation/allocator.h>
#include <foundation/api_registry.h>
#include <foundation/application.h>
#include <foundation/buffer_format.h>
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
#include <foundation/string.inl>
#include <foundation/the_truth.h>

#include <plugins/os_window/os_window.h>

#include <float.h>

#if defined(TM_OS_POSIX)
#include <unistd.h>
#endif

#if defined(TM_OS_WINDOWS)
#include <ShellScalingAPI.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif


typedef struct frame_parameters_t
{
    uint64_t frame_count;
    tm_clock_o clock;
    double time;
    double smooth_delta;
} frame_parameters_t;

enum { MAX_DEVICES = 8 };

typedef struct window_t
{
    tm_window_o *window;
    uint32_t swap_chain_resolution[2];
} window_t;

struct tm_application_o
{
    tm_allocator_i allocator;

    tm_the_truth_o *tt;

    char *data_dir;

    frame_parameters_t frame_parameters;

    window_t window;
    tm_color_space_desc_t color_space;

    uint64_t next_input_event;
    bool exit;
    TM_PAD(7);

    uint64_t reload_count;

    uint64_t os_dialog_show_count;
};

#define TM_RUNNING_APPLICATION_STATIC_VARIABLE TM_STATIC_HASH("tm_running_application_static_variable", 0x1d288e6042152ac8ULL)
tm_application_o **running_application_ptr;


static window_t *create_window(tm_application_o *app, tm_rect_t r, bool center_on_screen, bool maximize)
{
    window_t *win = &app->window;

    enum tm_os_window_style window_style = TM_OS_WINDOW_STYLE_CUSTOM_BORDER;
    if (center_on_screen)
        window_style |= TM_OS_WINDOW_STYLE_CENTERED;

    win->window = tm_os_window_api->create_window(TM_LOCALIZE("Playground"), r, window_style, 0);
    tm_os_window_api->set_border_metrics(win->window, (tm_os_window_border_metrics_t){ 3.0f, 30.0f });

    if (maximize)
        tm_os_window_api->set_window_state(win->window, TM_OS_WINDOW_STATE_MAXIMIZE);

    tm_window_platform_data_o platform_win = tm_os_window_api->platform_data(win->window);
    (void)platform_win;
    const tm_rect_t rect = tm_os_window_api->rect(win->window);
    win->swap_chain_resolution[0] = (uint32_t)rect.w;
    win->swap_chain_resolution[1] = (uint32_t)rect.h;

    return win;
}


static tm_the_truth_o *setup_the_truth(tm_allocator_i *allocator)
{
    return tm_the_truth_api->create(allocator, TM_THE_TRUTH_CREATE_TYPES_ALL);
}

static void setup_initial_window(tm_application_o *app)
{
    TM_PROFILER_BEGIN_FUNC_SCOPE();

    // Hard coded initial window size and position.
    // TODO: Figure out best practice for first-time boot up of editor.
    tm_rect_t rect = { 100, 100, 1920, 1000 };

    // Grab the dpi scale factor, width and height for the display that holds the window center.
    const tm_vec2_t c = tm_rect_center(rect);
    float dpi_scale_factor = 1.f;
    tm_rect_t display_rect = { .w = FLT_MAX, .h = FLT_MAX };
    const uint32_t n_connected_displays = tm_os_display_api->num_displays();
    for (uint32_t i = 0; i != n_connected_displays; ++i) {
        tm_display_o *display = tm_os_display_api->display(i);
        const tm_rect_t drect = tm_os_display_api->os_display_rect(display);
        if (c.x >= drect.x && c.x < (drect.x + drect.w) && c.y >= drect.y && c.y < (drect.y + drect.h)) {
            dpi_scale_factor = tm_os_display_api->os_display_dpi_scale_factor(display);
            display_rect = drect;
            break;
        }
    }

    // Adjust window rect to display scale factor.
    rect = tm_os_window_api->adjust_rect(rect, dpi_scale_factor, TM_OS_WINDOW_ADJUST_RECT_TO_PIXELS);

    const bool maximize = rect.w >= display_rect.w || rect.h >= display_rect.h;

    // Clamp width and height to display size.
    rect.w = tm_min(rect.w, display_rect.w);
    rect.h = tm_min(rect.h, display_rect.h);

    create_window(app, rect, true, maximize);

    TM_PROFILER_END_FUNC_SCOPE();
}

static void check_swapchain_resize(tm_application_o *app)
{
    window_t *win = &app->window;

    tm_rect_t rect = tm_os_window_api->rect(win->window);
    if ((uint32_t)rect.w != win->swap_chain_resolution[0] || (uint32_t)rect.h != win->swap_chain_resolution[1]) {
    }
}

static bool tick_application(tm_application_o *app)
{
    TM_PROFILER_BEGIN_FUNC_SCOPE();

    const tm_window_status_t win_status = tm_os_window_api->status(app->window.window);
    tm_unused(win_status);

    tm_temp_allocator_api->tick_frame();
    tm_the_truth_api->garbage_collect(app->tt);
    const uint64_t rc = tm_plugins_api->reload_count();
    if (rc != app->reload_count) {
        tm_the_truth_api->hot_reload(app->tt);
        app->reload_count = rc;
    }

    // Update frame time
    tm_clock_o now = tm_os_api->time->now();
    double delta = tm_os_api->time->delta(now, app->frame_parameters.clock);
    // exponential moving average for smooth delta, window of 16 frames.
    const float smooth_delta_weight = app->frame_parameters.frame_count > 16 && delta < 0.25 ? (1.f / 16.f) : 1.f;
    app->frame_parameters.smooth_delta = smooth_delta_weight * delta + (1 - smooth_delta_weight) * app->frame_parameters.smooth_delta;
    app->frame_parameters.clock = now;
    app->frame_parameters.time += delta;

    // Run message pump for all window
    const uint64_t os_dialog_show_count = tm_os_api->dialogs->show_count();
    const bool os_dialog_shown = os_dialog_show_count != app->os_dialog_show_count;
    app->os_dialog_show_count = os_dialog_show_count;
    if (os_dialog_shown) {
        // Clear event queue.
        while (true) {
            tm_input_event_t events[32];
            const uint64_t n = tm_input_api->events(app->next_input_event, events, 32);
            app->next_input_event += n;
            if (n < 32)
                break;
        }
    }
    /*tm_os_window_api->wait();*/
    tm_os_window_api->update_window(app->window.window);
    /*if (tm_os_window_api->update_window(app->window.window).focus_changed || os_dialog_shown)*/
        /*tm_ui_api->release_held_state(app->window.ui);*/

    if (tm_os_window_api->has_user_requested_close(app->window.window, true))
        return false;

    // Process input
    while (true) {
        tm_input_event_t events[32];
        uint64_t n = tm_input_api->events(app->next_input_event, events, 32);
        /*if (win_status.has_focus)*/
            /*feed_events_editor_gui(app, events, (uint32_t)n);*/
        /*else {*/
            /*for (const tm_input_event_t *e = events; e != events + n; ++e) {*/
                /*if (!e->type)*/
                    /*continue;*/
                /*const uint32_t ct = e->source->controller_type;*/

                /*// Always feed mouse position to track it.*/
                /*if (ct == TM_INPUT_CONTROLLER_TYPE_MOUSE && e->item_id == TM_INPUT_MOUSE_ITEM_POSITION)*/
                    /*feed_events_editor_gui(app, e, 1);*/

                /*// Feed mouse events to window under cursor for drag/drop management.*/
                /*else if (win_status.is_under_cursor && ct == TM_INPUT_CONTROLLER_TYPE_MOUSE)*/
                    /*feed_events_editor_gui(app, e, 1);*/
            /*}*/
        /*}*/
        app->next_input_event += n;
        if (n < 32)
            break;
    }


    // Update UI and handle window resizes
    check_swapchain_resize(app);

    return TM_PROFILER_END_FUNC_SCOPE_WITH(!app->exit);
}

static const char *default_data_dir(tm_temp_allocator_i *ta, const char *exe)
{
    const char *exe_name = tm_path_api->base_cstr(exe);
    return tm_temp_allocator_api->printf(ta, "%.*sdata/", (int)(exe_name - exe), exe);
}

static tm_application_o *create_application(int argc, char **argv)
{
    tm_os_api->socket->init();

#if defined(TM_OS_WINDOWS)
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
#endif

    tm_profiler_api->init(tm_allocator_api->system, 1024 * 1024);
    *tm_profiler_api->enabled = true;

    TM_PROFILER_BEGIN_FUNC_SCOPE();

    bool hot_reload_plugins = true;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--no-hot-reload"))
            hot_reload_plugins = false;
    }

    // Attempt to load plugins
    const char *exe_path = tm_os_api->system->exe_path(argv[0]);
    {
        TM_PROFILER_BEGIN_LOCAL_SCOPE(load_plugins);
        TM_INIT_TEMP_ALLOCATOR(ta);

        const tm_str_t exe_dir = tm_path_api->directory(tm_str(exe_path));
        const tm_str_t plugin_dir = tm_path_api->join(exe_dir, tm_str("plugins"), ta);
        const char **plugins = tm_plugins_api->enumerate(tm_cstring(plugin_dir, ta), ta);
        for (const char **p = plugins; p != tm_carray_end(plugins); ++p)
            tm_plugins_api->load(*p, hot_reload_plugins);

        TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
        TM_PROFILER_END_LOCAL_SCOPE(load_plugins);
    }
    tm_global_api_registry->disable_apis_missing_dependencies();

    const bool USE_END_OF_PAGE_ALLOCATOR = false;
    tm_allocator_i *standard_allocator = USE_END_OF_PAGE_ALLOCATOR ? tm_allocator_api->end_of_page : tm_allocator_api->system;
    tm_allocator_i a = tm_allocator_api->create_child(standard_allocator, "application");

    tm_application_o *app = tm_alloc(&a, sizeof(*app));
    *app = (tm_application_o){
        .allocator = a,
        .color_space = TM_COLOR_SPACE_DEFAULT_SDR
    };
    *running_application_ptr = app;

    TM_INIT_TEMP_ALLOCATOR(ta);

    app->tt = setup_the_truth(&app->allocator);


    const char *data_dir = default_data_dir(ta, exe_path);
    const uint32_t data_dir_len = (uint32_t)strlen(data_dir) + 1;
    app->data_dir = tm_alloc(&app->allocator, data_dir_len);
    memcpy(app->data_dir, data_dir, data_dir_len);

    setup_initial_window(app);

    app->frame_parameters.clock = tm_os_api->time->now();
    /*app->simple_draw = init_simple_draw(&app->allocator, app->tt);*/

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);

    TM_PROFILER_END_FUNC_SCOPE();

    return app;
}

static void destroy_application(tm_application_o *app)
{

    /*shutdown_simple_draw(app->simple_draw);*/

    tm_free(&app->allocator, app->data_dir, strlen(app->data_dir) + 1);

    tm_os_window_api->destroy_window(app->window.window);

    tm_the_truth_api->destroy(app->tt);

    tm_profiler_api->shutdown();

    tm_allocator_i a = app->allocator;
    tm_free(&a, app, sizeof(*app));
    tm_allocator_api->destroy_child(&a);

    *running_application_ptr = 0;
}

static tm_application_o *application(void)
{
    return *running_application_ptr;
}

static const char *application__data_dir(tm_application_o *app)
{
    return app->data_dir;
}

struct tm_application_api *tm_application_api = &(struct tm_application_api){
    .create = create_application,
    .tick = tick_application,
    .destroy = destroy_application,
    .application = application,
    .data_dir = application__data_dir,
};


void tm_path_tracing_app_load_plugin(struct tm_api_registry_api *reg, bool load)
{
    running_application_ptr = reg->static_variable(TM_RUNNING_APPLICATION_STATIC_VARIABLE, sizeof(tm_application_o *), __FILE__, __LINE__);

    tm_set_or_remove_api(reg, load, tm_application_api, tm_application_api);
}
