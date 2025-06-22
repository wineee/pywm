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

/* #define DEBUG_DAMAGE_HIGHLIGHT */
/* #define DEBUG_DAMAGE_RERENDER */

struct wm_content_vtable wm_output_vtable;

// Forward declarations
static void render(struct wm_output *output, struct timespec now, pixman_region32_t *damage);
static double configure(struct wm_output* output);

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

    int buffer_age;
    pixman_region32_t damage;
    pixman_region32_init(&damage);
    
    if (!wlr_output_attach_render(output->wlr_output, &buffer_age)) {
        pixman_region32_fini(&damage);
        return;
    }

    wlr_damage_ring_get_buffer_damage(&output->damage_ring, buffer_age, &damage);
    if (!output->wlr_output->needs_frame &&
            !pixman_region32_not_empty(&output->damage_ring.current)) {
        pixman_region32_fini(&damage);
        wlr_output_rollback(output->wlr_output);
        return;
    }

    render(output, now, &damage);

    pixman_region32_fini(&damage);
    wlr_damage_ring_rotate(&output->damage_ring);
}

static void handle_needs_frame(struct wl_listener *listener, void *data) {
    struct wm_output *output = wl_container_of(listener, output, needs_frame);
    wlr_output_schedule_frame(output->wlr_output);
}

static void render(struct wm_output *output, struct timespec now, pixman_region32_t *damage) {
    struct wm_renderer *renderer = output->wm_server->wm_renderer;

    int width, height;
    wlr_output_transformed_resolution(output->wlr_output, &width, &height);

    /* Ensure z-indes */
    wm_server_update_contents(output->wm_server);

    /* Begin render */
    wm_renderer_begin(renderer, output);

#ifdef DEBUG_DAMAGE_HIGHLIGHT
    wlr_renderer_clear(renderer->wlr_renderer, (float[]){1, 1, 0, 1});
#endif

    /* 
     * This does not catch all cases, where clearing is necessary - specifically, if only the texture contains transparency,
     * but compositor opacaity is set to 1, needs_clear will be false.
     *
     * In the end the assumption is there's always a background and this catches a fading out background */
    bool needs_clear = false;
    struct wm_content *r;
    wl_list_for_each_reverse(r, &output->wm_server->wm_contents, link) {
        if(wm_content_get_opacity(r) < 1. - 0.0001){
            needs_clear=true;
            break;
        }
    }

    struct wm_compose_chain* chain = wm_compose_chain_from_damage(output->wm_server, output, damage);

    struct wm_compose_chain* last = chain;
    while(last->lower) last = last->lower;

    if(needs_clear){
        wm_renderer_to_buffer(renderer, 0);
        wm_renderer_clear(renderer, damage, (float[]){ 0., 0., 0., 1.});

        if(last != chain){
            wm_renderer_to_buffer(renderer, 1);
            wm_renderer_clear(renderer, &last->damage, (float[]){ 0., 0., 0., 1.});
        }
    }

    if(last != chain){
        wm_renderer_to_buffer(renderer, 1);
    }

    /* Do render */
    for(struct wm_compose_chain* at=last; at; at=at->higher){
        wl_list_for_each_reverse(r, &output->wm_server->wm_contents, link) {
            if(at->lower && wm_content_get_z_index(r) < at->lower->z_index) continue;
            if(wm_content_get_z_index(r) > at->z_index) break;

            if(wm_content_get_opacity(r) < 0.0001) continue;
            wm_content_render(r, output, &at->damage, now);
        }
        if(at->composite){
            wm_composite_apply(at->composite, output, &at->composite_output, now);
        }
    }

    if(last != chain){
        wm_renderer_to_buffer(renderer, 1);
    }

    /* End render */
    wm_renderer_end(renderer, &chain->damage, output);
    wm_compose_chain_free(chain);

    /* Commit */
    pixman_region32_t frame_damage;
    pixman_region32_init(&frame_damage);

    enum wl_output_transform transform =
        wlr_output_transform_invert(output->wlr_output->transform);
    wlr_region_transform(&frame_damage, &output->damage_ring.current, transform,
                         width, height);

#ifdef DEBUG_DAMAGE_HIGHLIGHT
    pixman_region32_union_rect(&frame_damage, &frame_damage, 0, 0, width, height);
#endif

    wlr_output_set_damage(output->wlr_output, &frame_damage);
    pixman_region32_fini(&frame_damage);

    if (!wlr_output_commit(output->wlr_output)) {
        wlr_log(WLR_DEBUG, "Commit frame failed");
    }

    /* 
     * Synchronous update is best scheduled immediately after frame
     */
    DEBUG_PERFORMANCE(present_frame, output->key);
    wm_server_schedule_update(output->wm_server, output);
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
        if(w <= 0){
            wlr_log(WLR_INFO, "Output: Need to configure width for custom mode - defaulting to 1920");
            w = 1920;
        }
        if(h <= 0){
            wlr_log(WLR_INFO, "Output: Need to configure height for custom mode - defaulting to 1280");
            h = 1280;
        }
        dpi = output->wlr_output->phys_width > 0 ? (double)w * 25.4 / output->wlr_output->phys_width : 0;
        wlr_log(WLR_INFO, "Output: Setting custom mode - %dx%d(%d)", w, h, mHz);
        wlr_output_set_custom_mode(output->wlr_output, w, h, mHz);
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

void wm_output_init(struct wm_output *output, struct wm_server *server, struct wlr_output *wlr_output) {
    if(wm_output_overridden_name){
        strcpy(wlr_output->name, wm_output_overridden_name);
        wm_output_overridden_name = NULL;
    }
    wlr_log(WLR_INFO, "New output: %s: %s (%s) - use name: '%s' to configure", wlr_output->make, wlr_output->model, wlr_output->description, wlr_output->name);
    
    output->wm_server = server;
    output->wlr_output = wlr_output;
    wlr_output->data = output;

    output->layout_x = 0;
    output->layout_y = 0;

    if (!wm_renderer_init_output(server->wm_renderer, output)) {
        wlr_log(WLR_ERROR, "Failed to init output render");
        return;
    }

    wlr_damage_ring_init(&output->damage_ring);

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

    wlr_damage_ring_finish(&output->damage_ring);

    output->wlr_output->data = NULL;
    output->wlr_output = NULL;

    free(output);
}
