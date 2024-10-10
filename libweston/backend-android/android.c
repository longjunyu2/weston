/*
 * Copyright 2024 Junyu Long
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "config.h"

#include <string.h>
#include <libweston/backend-android.h>
#include <libweston/windowed-output-api.h>
#include <weston-wrapper.h>
#include <linux-explicit-synchronization.h>
#include <pixel-formats.h>
#include <pixman-renderer.h>
#include <shared/weston-drm-fourcc.h>
#include <shared/timespec-util.h>
#include <gl-borders.h>

#define DEFAULT_OUTPUT_REPAINT_REFRESH 60000 /* In mHz. */

struct pixman_renderbuffer {
    struct weston_renderbuffer base;

    pixman_image_t *image;
    struct wl_list link;
};

struct android_backend {
    struct weston_backend base;
    struct weston_compositor *compositor;

    struct weston_seat android_seat;
    struct weston_touch_device* android_touch_device;

    const struct pixel_format_info **formats;
    unsigned int formats_count;

    int refresh;
    bool repaint_only_on_capture;

    void* jni;
};

struct android_head {
    struct weston_head base;
};

struct android_output {
    struct weston_output base;
	struct android_backend *backend;

	struct weston_mode mode;
    struct wl_event_source *finish_frame_timer;
    struct weston_renderbuffer *renderbuffer;

    struct frame *frame;
};

static const uint32_t android_formats[] = {
        DRM_FORMAT_XRGB8888, /* default for pixman-renderer */
        DRM_FORMAT_ARGB8888,
};

static void
android_destroy(struct weston_backend *backend);

static inline struct android_head *
to_android_head(struct weston_head *base)
{
    if (base->backend->destroy != android_destroy)
        return NULL;
    return container_of(base, struct android_head, base);
}

static void
android_output_destroy(struct weston_output *bas);

static inline struct android_output *
to_android_output(struct weston_output *base)
{
    if (base->destroy != android_output_destroy)
        return NULL;
    return container_of(base, struct android_output, base);
}

static inline struct android_backend *
to_android_backend(struct weston_backend *base)
{
    return container_of(base, struct android_backend, base);
}

static int
android_output_start_repaint_loop(struct weston_output *output)
{
    struct timespec ts;

    weston_compositor_read_presentation_clock(output->compositor, &ts);
    weston_output_finish_frame(output, &ts, WP_PRESENTATION_FEEDBACK_INVALID);

    return 0;
}

static int
finish_frame_handler(void *data)
{
    struct android_output *output = data;

    weston_output_finish_frame_from_timer(&output->base);

    return 1;
}

static int
android_output_repaint(struct weston_output *output_base)
{
    struct android_output *output = to_android_output(output_base);
    struct weston_compositor *ec;
    struct android_backend *b;
    struct pixman_renderbuffer *pixman_buffer;
    pixman_region32_t damage;
    int delay_msec;

    assert(output);

    b = output->backend;
    ec = output->base.compositor;

    pixman_region32_init(&damage);

    weston_output_flush_damage_for_primary_plane(output_base, &damage);

    ec->renderer->repaint_output(&output->base, &damage, output->renderbuffer);

    pixman_region32_fini(&damage);

    if (b->compositor->renderer->type == WESTON_RENDERER_PIXMAN) {
        pixman_buffer = container_of(output->renderbuffer, struct pixman_renderbuffer, base);
        wrapper_notify_android_repaint_output_pixman(b->jni, pixman_buffer->image);
    }

    delay_msec = millihz_to_nsec(output->mode.refresh) / 1000000;
    wl_event_source_timer_update(output->finish_frame_timer, delay_msec);

    return 0;
}


static void
android_output_disable_pixman(struct android_output *output)
{
    struct weston_renderer *renderer = output->base.compositor->renderer;

    weston_renderbuffer_unref(output->renderbuffer);
    output->renderbuffer = NULL;
    renderer->pixman->output_destroy(&output->base);
}

static int
android_output_disable(struct weston_output *base)
{
    struct android_output *output = to_android_output(base);
    struct android_backend *b;

    assert(output);

    if (!output->base.enabled)
        return 0;

    b = output->backend;

    wl_event_source_remove(output->finish_frame_timer);

    switch (b->compositor->renderer->type) {
        case WESTON_RENDERER_GL:
            weston_log("Error: could not disable gl renderer since it is not supported yet.\n");
            break;
        case WESTON_RENDERER_PIXMAN:
            android_output_disable_pixman(output);
            break;
        case WESTON_RENDERER_NOOP:
            break;
        case WESTON_RENDERER_AUTO:
            unreachable("cannot have auto renderer at runtime");
    }

    return 0;
}

static void
android_output_destroy(struct weston_output *base)
{
	struct android_output *output = to_android_output(base);

    assert(output);

    android_output_disable(&output->base);
    weston_output_release(&output->base);
    wrapper_notify_android_output_destroy(output->backend->jni);

    assert(!output->frame);
	free(output);
}

static int
android_output_enable_pixman(struct android_output *output)
{
    const struct pixman_renderer_interface *pixman;
    const struct pixman_renderer_output_options options = {
            .use_shadow = true,
            .fb_size = {
                    .width = output->base.current_mode->width,
                    .height = output->base.current_mode->height
            },
            .format = pixel_format_get_info(android_formats[0])
    };

    pixman = output->base.compositor->renderer->pixman;

    if (pixman->output_create(&output->base, &options) < 0)
        return -1;

    output->renderbuffer =
            pixman->create_image(&output->base, options.format,
                                 output->base.current_mode->width,
                                 output->base.current_mode->height);
    if (!output->renderbuffer)
        goto err_renderer;

    return 0;

    err_renderer:
    pixman->output_destroy(&output->base);

    return -1;
}

static int
android_output_enable(struct weston_output *base)
{
    struct android_output *output = to_android_output(base);
    struct android_backend *b;
    struct wl_event_loop *loop;
    int ret = 0;

    assert(output);

    b = output->backend;

    loop = wl_display_get_event_loop(b->compositor->wl_display);
    output->finish_frame_timer =
            wl_event_loop_add_timer(loop, finish_frame_handler, output);

    if (output->finish_frame_timer == NULL) {
        weston_log("failed to add finish frame timer\n");
        return -1;
    }

    switch (b->compositor->renderer->type) {
        case WESTON_RENDERER_GL:
            weston_log("Error: Android backend not supports gl renderer yet.\n");
            break;
        case WESTON_RENDERER_PIXMAN:
            ret = android_output_enable_pixman(output);
            break;
        case WESTON_RENDERER_NOOP:
            break;
        case WESTON_RENDERER_AUTO:
            unreachable("cannot have auto renderer at runtime");
    }

    if (ret < 0) {
        wl_event_source_remove(output->finish_frame_timer);
        return -1;
    }

    return 0;
}

static int
android_output_set_size(struct weston_output *base,
        int width, int height)
{
    struct android_output *output = to_android_output(base);
    struct weston_head *head;
    int output_width, output_height;

    if (!output)
        return -1;

    /* We can only called once */
    assert(!output->base.current_mode);

    /* Make sure we have scale set. */
    assert(output->base.current_scale);

    wl_list_for_each(head, &output->base.head_list, output_link) {
        weston_head_set_monitor_strings(head, "weston", "android",
                                        NULL);

        /* XXX: Calculate proper size. */
        weston_head_set_physical_size(head, width, height);
    }

    output_width = width * output->base.current_scale;
    output_height = height * output->base.current_scale;

    output->mode.flags =
            WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED;
    output->mode.width = output_width;
    output->mode.height = output_height;
    output->mode.refresh = output->backend->refresh;
    wl_list_insert(&output->base.mode_list, &output->mode.link);

    output->base.current_mode = &output->mode;

    output->base.start_repaint_loop = android_output_start_repaint_loop;
    output->base.repaint = android_output_repaint;
    output->base.assign_planes = NULL;
    output->base.set_backlight = NULL;
    output->base.set_dpms = NULL;
    output->base.switch_mode = NULL;

    wrapper_notify_android_output_set_size(output->backend->jni, width, height);

    return 0;
}

static struct weston_output *
android_output_create(struct weston_backend *backed, const char* name)
{
    struct android_backend *b = container_of(backed, struct android_backend, base);
    struct weston_compositor *compositor = b->compositor;
	struct android_output *output;

    /* name can't be NULL*/
    assert(name);

	output = zalloc(sizeof *output);
	if (!output)
		return NULL;

    weston_output_init(&output->base, compositor, name);

    output->base.destroy = android_output_destroy;
    output->base.disable = android_output_disable;
    output->base.enable = android_output_enable;
    output->base.attach_head = NULL;
    output->base.detach_head = NULL;
    output->base.repaint_only_on_capture = b->repaint_only_on_capture;
    output->backend = b;

    weston_compositor_add_pending_output(&output->base, compositor);

    wrapper_notify_android_output_create(b->jni);

	return &output->base;
}

static int
android_head_create(struct weston_backend *base,
        const char *name)
{
    struct android_backend *backend = to_android_backend(base);
    struct android_head *head;

    /* name can't be NULL */
    assert(name);

    head = zalloc(sizeof *head);
    if (head == NULL)
        return -1;

    weston_head_init(&head->base, name);

    head->base.backend = &backend->base;

    weston_head_set_connection_status(&head->base, true);
    weston_head_set_supported_eotf_mask(&head->base,
                                        WESTON_EOTF_MODE_ALL_MASK);
    weston_head_set_supported_colorimetry_mask(&head->base,
                                               WESTON_COLORIMETRY_MODE_ALL_MASK);

    /* Ideally all attributes of the head would be set here, so that the
     * user has all the information when deciding to create outputs.
     * We do not have those until set_size() time through.
     */

    weston_compositor_add_head(backend->compositor, &head->base);

    return 0;
}

static void
android_head_destroy(struct weston_head *base)
{
    struct android_head *head = to_android_head(base);

    assert(head);

    weston_head_release(&head->base);
    free(head);
}

static void func_touch(struct weston_backend* base, int touchId, int touchType, float x, float y) {
    struct android_backend* b = to_android_backend(base);
    static struct timespec ts;
    static struct weston_coord_global pos;
    static struct weston_coord_global* pos_p;

    pos.c.x = x;
    pos.c.y = y;
    weston_compositor_get_time(&ts);

    if (touchType == WL_TOUCH_UP) {
        pos_p = NULL;
    } else
        pos_p = &pos;

    notify_touch(b->android_touch_device, &ts, touchId, pos_p, touchType);
    notify_touch_frame(b->android_touch_device);
}

static void func_key(struct weston_backend* base, int key, int keyState) {
    struct android_backend* b = to_android_backend(base);
    static struct timespec ts;

    weston_compositor_get_time(&ts);

    notify_key(&b->android_seat, &ts, key, keyState, STATE_UPDATE_AUTOMATIC);
}

static void func_pointer(struct weston_backend* base, int pointerType, float x, float y) {
    struct android_backend* b = to_android_backend(base);
    static struct weston_coord_global pos;
    static struct timespec ts;
    static struct weston_pointer_motion_event motionEvent = {0};

    pos.c.x = x;
    pos.c.y = y;
    weston_compositor_get_time(&ts);

    if (pointerType == ABSOLUTE_POS) {
        motionEvent.mask = WESTON_POINTER_MOTION_ABS;
        motionEvent.abs = pos;
    } else if (pointerType == RELATIVE_POS) {
        motionEvent.mask = WESTON_POINTER_MOTION_REL;
        motionEvent.rel = pos.c;
    }

    notify_motion(&b->android_seat, &ts, &motionEvent);
    notify_pointer_frame(&b->android_seat);
}

static void func_button(struct weston_backend* base, int button, int buttonState) {
    struct android_backend* b = to_android_backend(base);
    static struct timespec ts;

    weston_compositor_get_time(&ts);

    notify_button(&b->android_seat, &ts, button, buttonState);
}

static void func_axis(struct weston_backend* base, int axisType, float value, bool hasDiscrete, int discrete) {
    struct android_backend* b = to_android_backend(base);
    static struct weston_pointer_axis_event axisEvent= {0};
    static struct timespec ts;

    weston_compositor_get_time(&ts);

    axisEvent.axis = axisType;
    axisEvent.value = value;
    axisEvent.has_discrete = hasDiscrete;
    axisEvent.discrete = discrete;

    notify_axis(&b->android_seat, &ts, &axisEvent);
    notify_pointer_frame(&b->android_seat);
}

static int
android_input_create(struct android_backend* b) {
    struct xkb_keymap *keymap = NULL;
    struct xkb_rule_names rules;

    weston_seat_init(&b->android_seat, b->compositor, "android");

    if (!update_xkb_rules(b->jni, &rules.rules, &rules.model, &rules.layout)) {
        weston_log("Failed to update xkb rules.\n");
        goto error;
    }
    rules.options = NULL;
    rules.variant = NULL;

    if (!(keymap = xkb_keymap_new_from_names(
            b->compositor->xkb_context,
            &rules,
            XKB_KEYMAP_COMPILE_NO_FLAGS))) {
        weston_log("Failed to get keymap.\n");
        goto error;
    }

    if (weston_seat_init_keyboard(&b->android_seat, keymap)) {
        weston_log("Failed to init keyboard for android seat\n");
        goto error;
    }

    xkb_keymap_unref(keymap);

    wrapper_func_key(b->jni, func_key);

    // init pointer
    if (weston_seat_init_pointer(&b->android_seat)) {
        weston_log("Failed to init pointer for android seat\n");
        goto error;
    }

    wrapper_func_button(b->jni, func_button);
    wrapper_func_pointer(b->jni, func_pointer);
    wrapper_func_axis(b->jni, func_axis);

    // init touch
    if (weston_seat_init_touch(&b->android_seat)) {
        weston_log("Failed to init touch for android seat\n");
        goto error;
    }

    if (!(b->android_touch_device = weston_touch_create_touch_device(
            b->android_seat.touch_state,
            "android-touch",NULL, NULL))) {
        weston_log("Failed to create touch device\n");
        goto error;
    }

    wrapper_func_touch(b->jni, func_touch);

    return 0;

error:
    weston_seat_release(&b->android_seat);
    return -1;
}

static void
android_input_destroy(struct android_backend* b) {
    weston_touch_device_destroy(b->android_touch_device);
    weston_seat_release(&b->android_seat);
}

static void
android_destroy(struct weston_backend *backend)
{
    struct android_backend *b = container_of(backend, struct android_backend, base);
    struct weston_compositor *ec = b->compositor;
    struct weston_head *base, *next;

    wl_list_remove(&b->base.link);

    wl_list_for_each_safe(base, next, &ec->head_list, compositor_link) {
        if (to_android_head(base))
            android_head_destroy(base);
    }

    wrapper_notify_android_destroy(b->jni);

    free(b->formats);
    free(b);

    android_input_destroy(b);

    /* XXX: cleaning up after cairo/fontconfig here might seem suitable,
     * but fontconfig will create additional threads which we can't wait
     * for -- in order to realiably de-allocate all resources, as to get a
     * report without any mem leaks. */
}

static const struct weston_windowed_output_api api = {
        android_output_set_size,
        android_head_create,
};

static struct android_backend *
android_backend_create(struct weston_compositor *compositor,
        struct weston_android_backend_config *config)
{
    struct android_backend *b;
    int ret;

    b = zalloc(sizeof *b);
    if (b == NULL)
        return NULL;

    b->compositor = compositor;
    wl_list_insert(&compositor->backend_list, &b->base.link);

    b->base.supported_presentation_clocks =
            WESTON_PRESENTATION_CLOCKS_SOFTWARE;

    b->base.destroy = android_destroy;
    b->base.create_output = android_output_create;

    b->formats_count = ARRAY_LENGTH(android_formats);
    b->formats = pixel_format_get_array(android_formats, b->formats_count);

    b->jni = config->jni;

    /* Wayland event source's timeout has a granularity of the order of
     * milliseconds so the highest supported rate is 1 kHz. 0 is a special
     * value that enables repaints only on capture. */
    if (config->refresh > 0) {
        b->refresh = MIN(config->refresh, 1000000);
    } else if (config->refresh == 0) {
        b->refresh = 1000000;
        b->repaint_only_on_capture = true;
    } else {
        b->refresh = DEFAULT_OUTPUT_REPAINT_REFRESH;
    }

    if (!compositor->renderer) {
        switch (config->renderer) {
            case WESTON_RENDERER_GL:
                weston_log("Error: Android backend does not supports gl renderer yet.\n");
                ret = -1;
                break;
            case WESTON_RENDERER_PIXMAN:
                ret = weston_compositor_init_renderer(compositor,
                                                      WESTON_RENDERER_PIXMAN,
                                                      NULL);
                break;
            case WESTON_RENDERER_AUTO:
            case WESTON_RENDERER_NOOP:
                ret = noop_renderer_init(compositor);
                break;

            default:
                weston_log("Error: unsupported renderer\n");
                ret = -1;
                break;
        }

        if (ret < 0)
            goto err_input;

        /* Support zwp_linux_explicit_synchronization_unstable_v1 to enable
         * testing. */
        if (linux_explicit_synchronization_setup(compositor) < 0)
            goto err_input;
    }

    ret = weston_plugin_api_register(compositor,
                                     WESTON_WINDOWED_OUTPUT_API_NAME_ANDROID,
                                     &api, sizeof(api));

    if (ret < 0) {
        weston_log("Failed to register output API.\n");
        goto err_input;
    }

    return b;

err_input:
    wl_list_remove(&b->base.link);
    free(b);
    return NULL;
}

static void
config_init_to_defaults(struct weston_android_backend_config *config)
{
    config->refresh = DEFAULT_OUTPUT_REPAINT_REFRESH;
}

WL_EXPORT int
weston_backend_init(struct weston_compositor *compositor,
                    struct weston_backend_config *config_base)
{
    struct android_backend *b = NULL;
    struct weston_android_backend_config config = {{ 0, }};

    if (config_base == NULL ||
        config_base->struct_version != WESTON_ANDROID_BACKEND_CONFIG_VERSION ||
        config_base->struct_size > sizeof(struct weston_android_backend_config)) {
        weston_log("android backend config structure is invalid\n");
        goto error;
    }

    config_init_to_defaults(&config);
    memcpy(&config, config_base, config_base->struct_size);

    b = android_backend_create(compositor, &config);
    if (b == NULL)
        goto error;

    if (android_input_create(b)) {
        weston_log("Failed to create Android input\n");
        goto error;
    }

    return 0;

error:
    if (b) {
        wl_list_remove(&b->base.link);
        free(b);
    }
    return -1;
}
