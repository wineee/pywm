#define _POSIX_C_SOURCE 200809L

#include "wm/wm_output.h"
#include "wm/wm_config.h"
#include "wm/wm_layout.h"
#include "wm/wm_renderer.h"
#include "wm/wm_server.h"
#include "wm/wm_util.h"
#include "wm/wm_view.h"
#include "wm/wm_widget.h"
#include "wm/wm_seat.h"
#include "wm/wm_cursor.h"
#include "wm/wm_composite.h"
#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_scene.h>

/* #define DEBUG_DAMAGE_HIGHLIGHT */
/* #define DEBUG_DAMAGE_RERENDER */

struct wm_content_vtable wm_output_vtable;

// Forward declarations
static double configure(struct wm_output* output);

/* Send frame done event to a surface */
static void send_frame_done(struct wlr_scene_buffer *scene_buffer, int sx, int sy, void *data) {
    struct timespec *now = data;
    wlr_scene_buffer_send_frame_done(scene_buffer, now);
}

/*
 * Callbacks
 */
static void handle_destroy(struct wl_listener *listener, void *data) {
    wlr_log(WLR_DEBUG, "Output: Destroy");
    struct wm_output *output = wl_container_of(listener, output, destroy);
    wm_output_destroy(output);
}

static void handle_commit(struct wl_listener *listener, void *data) {
    struct wm_output *output = wl_container_of(listener, output, commit);
    struct wlr_output_event_commit* event = data;

    if (!output->wlr_output->enabled) {
        return;
    }

    if(event->state->committed & (WLR_OUTPUT_STATE_MODE |
            WLR_OUTPUT_STATE_TRANSFORM |
            WLR_OUTPUT_STATE_SCALE)){
        wlr_damage_ring_set_bounds(&output->damage_ring,
            output->wlr_output->width, output->wlr_output->height);
        wlr_output_schedule_frame(output->wlr_output);
    }
}

static void handle_damage(struct wl_listener *listener, void *data) {
    struct wm_output *output = wl_container_of(listener, output, damage);
    struct wlr_output_event_damage* event = data;
    if (wlr_damage_ring_add(&output->damage_ring, event->damage)) {
        wlr_output_schedule_frame(output->wlr_output);
    }
}

static void handle_frame(struct wl_listener *listener, void *data) {
    struct wm_output *output = wl_container_of(listener, output, frame);

    if(!output->wlr_output->enabled){
        return;
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    /* Ensure z-index */
    wm_server_update_contents(output->wm_server);

    /* Render the scene if needed and commit the output */
    wlr_scene_output_commit(output->scene_output, NULL);

    /* Send frame done events */
    wlr_scene_output_for_each_buffer(output->scene_output, send_frame_done, &now);

    /* 
     * Synchronous update is best scheduled immediately after frame
     */
    DEBUG_PERFORMANCE(present_frame, output->key);
    wm_server_schedule_update(output->wm_server, output);
}

static void handle_needs_frame(struct wl_listener *listener, void *data) {
    struct wm_output *output = wl_container_of(listener, output, needs_frame);
    wlr_output_schedule_frame(output->wlr_output);
}

/*
 * Class implementation
 */

static const char* wm_output_overridden_name = NULL;
void wm_output_override_name(const char* name){
    wm_output_overridden_name = name;
}

static double configure(struct wm_output* output){
    struct wm_config_output* config = wm_config_find_output(output->wm_server->wm_config, output->wlr_output->name);
    double dpi = 0.;

    /* Set mode */
    if (!wl_list_empty(&output->wlr_output->modes)) {
        struct wlr_output_mode *pref =
            wlr_output_preferred_mode(output->wlr_output);
        struct wlr_output_mode *best = NULL;

        struct wlr_output_mode *mode;
        wl_list_for_each(mode, &output->wlr_output->modes, link) {
            wlr_log(WLR_INFO, "Output: Output supports %dx%d(%d) %s",
                    mode->width, mode->height, mode->refresh,
                    mode->preferred ? "(Preferred)" : "");

            if (config) {
                // Sway logic
                if (mode->width == config->width &&
                    mode->height == config->height) {
                    if (mode->refresh == config->mHz) {
                        best = mode;
                        break;
                    }
                    if (best == NULL || mode->refresh > best->refresh) {
                        best = mode;
                    }
                }
            }
        }

        if (!best)
            best = pref;

        dpi = output->wlr_output->phys_width > 0 ? (double)best->width * 25.4 / output->wlr_output->phys_width : 0;
        wlr_log(WLR_INFO, "Output: Setting mode: %dx%d(%d)", best->width, best->height, best->refresh);
        wlr_output_set_mode(output->wlr_output, best);
    }else{
        int w = config ? config->width : 0;
        int h = config ? config->height : 0;
        int mHz = config ? config->mHz : 0;
        if(w <= 0 || h <= 0 || mHz <= 0) {
            wlr_log(WLR_INFO, "Output: Invalid mode");
        } else {
            dpi = output->wlr_output->phys_width > 0 ? (double)w * 25.4 / output->wlr_output->phys_width : 0;
            wlr_log(WLR_INFO, "Output: Setting custom mode - %dx%d(%d)", w, h, mHz);
            wlr_output_set_custom_mode(output->wlr_output, w, h, mHz);
        }
    }

    enum wl_output_transform transform = config ? config->transform : WL_OUTPUT_TRANSFORM_NORMAL;
    wlr_output_set_transform(output->wlr_output, transform);

    wlr_output_enable(output->wlr_output, true);
    if (!wlr_output_commit(output->wlr_output)) {
        wlr_log(WLR_INFO, "Output: Could not commit");
    }

    /* Set HiDPI scale */
    double scale = config ? config->scale : -1.0;
    if(scale < 0.1){
        if(dpi > 182){
            wlr_log(WLR_INFO, "Output: Assuming HiDPI scale");
            scale = 2.;
        }else{
            scale = 1.;
        }
    }

    wlr_log(WLR_INFO, "Output: Setting scale to %f", scale);
    wlr_output_set_scale(output->wlr_output, scale);

    return scale;
}

void wm_output_init(struct wm_output *output, struct wm_server *server, struct wm_layout *layout, struct wlr_output *wlr_output) {
    if(wm_output_overridden_name){
        strcpy(wlr_output->name, wm_output_overridden_name);
        wm_output_overridden_name = NULL;
    }
    wlr_log(WLR_INFO, "New output: %s: %s (%s) - use name: '%s' to configure", wlr_output->make, wlr_output->model, wlr_output->description, wlr_output->name);
    
    output->wm_server = server;
    output->wm_layout = layout;
    output->wlr_output = wlr_output;
    //wlr_output->data = output;

    output->layout_x = 0;
    output->layout_y = 0;

    if (!wm_renderer_init_output(server->wm_renderer, output)) {
        wlr_log(WLR_ERROR, "Failed to init output render");
        return;
    }

    wlr_damage_ring_init(&output->damage_ring);

    /* Create scene output */
    output->scene_output = wlr_scene_output_create(server->wlr_scene, wlr_output);

    double scale = configure(output);

    output->destroy.notify = &handle_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    output->commit.notify = &handle_commit;
    wl_signal_add(&wlr_output->events.commit, &output->commit);

    output->damage.notify = &handle_damage;
    wl_signal_add(&wlr_output->events.damage, &output->damage);

    output->frame.notify = &handle_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->needs_frame.notify = &handle_needs_frame;
    wl_signal_add(&wlr_output->events.needs_frame, &output->needs_frame);

    /* Let the cursor know we possibly have a new scale */
    wm_cursor_ensure_loaded_for_scale(server->wm_seat->wm_cursor, scale);

#ifdef WM_CUSTOM_RENDERER
    output->renderer_buffers = NULL;
#endif

    output->expecting_frame = false;
    clock_gettime(CLOCK_MONOTONIC, &output->last_frame);

    wlr_output_schedule_frame(wlr_output);
}

void wm_output_reconfigure(struct wm_output* output){
    double scale = configure(output);
    wm_cursor_ensure_loaded_for_scale(output->wm_server->wm_seat->wm_cursor, scale);
}

void wm_output_destroy(struct wm_output *output) {
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->commit.link);
    wl_list_remove(&output->damage.link);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->needs_frame.link);

    /* Destroy scene output */
    if (output->scene_output) {
        wlr_scene_output_destroy(output->scene_output);
    }

    wlr_damage_ring_finish(&output->damage_ring);
#if WM_CUSTOM_RENDERER
    wm_renderer_buffers_destroy(output->renderer_buffers);
#endif

    free(output);
}
