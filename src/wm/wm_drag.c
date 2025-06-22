#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include "wm/wm_drag.h"
#include "wm/wm_seat.h"
#include "wm/wm_util.h"
#include "wm/wm_layout.h"
#include "wm/wm_server.h"
#include "wm/wm_output.h"
#include "wm/wm_cursor.h"
#include "wm/wm_renderer.h"

struct wm_content_vtable wm_drag_vtable;

static void wm_drag_icon_destroy(struct wm_drag* drag){
    if(drag->wlr_drag_icon){
        wl_list_remove(&drag->icon_map.link);
        wl_list_remove(&drag->icon_unmap.link);
        wl_list_remove(&drag->icon_destroy.link);
        wl_list_remove(&drag->icon_surface_commit.link);
        drag->wlr_drag_icon = NULL;
    }

}

static void handle_destroy(struct wl_listener* listener, void* data){
    wlr_log(WLR_DEBUG, "Drag: destroying drag (and icon)");
    struct wm_drag* drag = wl_container_of(listener, drag, destroy);

    wm_layout_damage_from(drag->wm_seat->wm_server->wm_layout, &drag->super, NULL);
    wm_content_destroy(&drag->super);
    free(drag);
}

static void icon_handle_destroy(struct wl_listener* listener, void* data){
    struct wm_drag* drag = wl_container_of(listener, drag, icon_destroy);
    wlr_log(WLR_DEBUG, "Drag: destroying icon");

    wm_drag_icon_destroy(drag);
}

static void icon_handle_surface_commit(struct wl_listener* listener, void* data){
    struct wm_drag* drag = wl_container_of(listener, drag, icon_surface_commit);
    wm_drag_update_position(drag);
}

static void icon_handle_map(struct wl_listener* listener, void* data){
    struct wm_drag* drag = wl_container_of(listener, drag, icon_map);

    wlr_log(WLR_DEBUG, "Drag: surface map");
    wm_drag_update_position(drag);
}
static void icon_handle_unmap(struct wl_listener* listener, void* data){
    struct wm_drag* drag = wl_container_of(listener, drag, icon_unmap);

    wlr_log(WLR_DEBUG, "Drag: surface unmap");
    wm_layout_damage_from(drag->wm_seat->wm_server->wm_layout, &drag->super, NULL);
}

void wm_drag_init(struct wm_drag* drag, struct wm_seat* seat, struct wlr_drag* wlr_drag){
    wm_content_init(&drag->super, seat->wm_server);
    drag->super.vtable = &wm_drag_vtable;
    wm_content_set_opacity(&drag->super, 0.5);
    wm_content_set_z_index(&drag->super, WM_DRAG_Z_INDEX);
    wm_content_set_box(&drag->super, 0, 0, 0, 0);

    drag->wm_seat = seat;
    drag->wlr_drag = wlr_drag;

    drag->destroy.notify = handle_destroy;
    wl_signal_add(&wlr_drag->events.destroy, &drag->destroy);

    drag->wlr_drag_icon = NULL;
    if(wlr_drag->icon){
        drag->wlr_drag_icon = wlr_drag->icon;

        drag->icon_destroy.notify = icon_handle_destroy;
        wl_signal_add(&wlr_drag->icon->events.destroy, &drag->icon_destroy);

        // wlroots 0.17: use wlr_surface events instead of drag icon events
        drag->icon_map.notify = icon_handle_map;
        wl_signal_add(&wlr_drag->icon->surface->events.map, &drag->icon_map);

        drag->icon_unmap.notify = icon_handle_unmap;
        wl_signal_add(&wlr_drag->icon->surface->events.unmap, &drag->icon_unmap);

        drag->icon_surface_commit.notify = icon_handle_surface_commit;
        wl_signal_add(&wlr_drag->icon->surface->events.commit, &drag->icon_surface_commit);
    }

    // FIXME(rewine): Way ai add this
    wm_layout_damage_whole(seat->wm_server->wm_layout);
}

void wm_drag_update_position(struct wm_drag* drag){
    wm_layout_damage_from(drag->wm_seat->wm_server->wm_layout, &drag->super, NULL);
    /* Surface width / height are not set correctly - hacky way via output scale */
    if(!drag->wlr_drag_icon || !drag->wlr_drag_icon->surface) return;

    double width = drag->wlr_drag_icon->surface->current.buffer_width;
    double height = drag->wlr_drag_icon->surface->current.buffer_height;
    wm_content_set_box(&drag->super,
                       drag->wm_seat->wm_cursor->wlr_cursor->x - .5*width,
                       drag->wm_seat->wm_cursor->wlr_cursor->y - .5*height,
                       width, height);
    wm_layout_damage_from(drag->wm_seat->wm_server->wm_layout, &drag->super, NULL);
}

static void wm_drag_destroy(struct wm_content* super){
    struct wm_drag* drag = wm_cast(wm_drag, super);
    wl_list_remove(&drag->destroy.link);
    wm_drag_icon_destroy(drag);

    wm_content_base_destroy(super);
}

static void wm_drag_render(struct wm_content* super, struct wm_output* output, pixman_region32_t* output_damage, struct timespec now){
    struct wm_drag* drag = wm_cast(wm_drag, super);
    if(!drag->wlr_drag_icon) return;

    struct wlr_box unscaled = {
        .x = round((drag->super.display_x - output->layout_x) * output->wlr_output->scale),
        .y = round((drag->super.display_y - output->layout_y) * output->wlr_output->scale),
        .width = round(drag->super.display_width * output->wlr_output->scale),
        .height = round(drag->super.display_height * output->wlr_output->scale)};

    /* drag icons do not seem to properly handle scaling - therefore simply crop by output_scale */
    struct wlr_box box = {
        .x = unscaled.x + .5 * unscaled.width * (1. - 1. / output->wlr_output->scale),
        .y = unscaled.y + .5 * unscaled.height * (1. - 1. / output->wlr_output->scale),
        .width = unscaled.width / output->wlr_output->scale,
        .height = unscaled.height / output->wlr_output->scale
    };

    struct wlr_texture *texture = wlr_surface_get_texture(drag->wlr_drag_icon->surface);
    if (!texture) {
        return;
    }
    wm_renderer_render_texture_at(
            output->wm_server->wm_renderer, output_damage,
            drag->wlr_drag_icon->surface, texture, &box,
            wm_content_get_opacity(super), &box, 0,
            super->lock_enabled ? 0.0 : super->wm_server->lock_perc);
}

bool wm_content_is_drag(struct wm_content* content){
    return content->vtable == &wm_drag_vtable;
}

static void wm_drag_printf(FILE* file, struct wm_content* super){
    struct wm_drag* drag = wm_cast(wm_drag, super);
    fprintf(file, "wm_drag (%f, %f - %f, %f)\n", drag->super.display_x, drag->super.display_y, drag->super.display_width, drag->super.display_height);
}

struct wm_content_vtable wm_drag_vtable = {
    .destroy = &wm_drag_destroy,
    .render = &wm_drag_render,
    .damage_output = NULL,
    .printf = &wm_drag_printf,
};
