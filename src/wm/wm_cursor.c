#define _POSIX_C_SOURCE 200809L

#include <time.h>
#include <wayland-server.h>
#include <assert.h>
#include <wlr/util/log.h>
#include "wm/wm_cursor.h"
#include "wm/wm_seat.h"
#include "wm/wm_layout.h"
#include "wm/wm_output.h"
#include "wm/wm_pointer.h"
#include "wm/wm_config.h"
#include "wm/wm_server.h"
#include "wm/wm_content.h"
#include "wm/wm_drag.h"
#include "wm/wm.h"
#include "wm/wm_util.h"

/*
 * Callbacks
 */
static void handle_motion(struct wl_listener* listener, void* data){
    struct wm_cursor* cursor = wl_container_of(listener, cursor, motion);
    struct wlr_pointer_motion_event* event = data;

    clock_t t_msec = clock() * 1000 / CLOCKS_PER_SEC;
    cursor->msec_delta = event->time_msec - t_msec;

    wlr_cursor_move(cursor->wlr_cursor, &event->pointer->base, event->delta_x, event->delta_y);
    if(wm_callback_motion(event->delta_x, event->delta_y, cursor->wlr_cursor->x, cursor->wlr_cursor->y, event->time_msec)){
        wlr_cursor_move(cursor->wlr_cursor, &event->pointer->base, -event->delta_x, -event->delta_y);
        return;
    }

    wm_cursor_update(cursor);
}

static void handle_motion_absolute(struct wl_listener* listener, void* data){
    struct wm_cursor* cursor = wl_container_of(listener, cursor, motion_absolute);
    struct wlr_pointer_motion_absolute_event* event = data;

    double lx, ly;
    wlr_cursor_absolute_to_layout_coords(cursor->wlr_cursor, &event->pointer->base, event->x, event->y, &lx, &ly);

    double dx = lx - cursor->wlr_cursor->x;
    double dy = ly - cursor->wlr_cursor->y;

    if(wm_callback_motion(dx, dy, lx, ly, event->time_msec)){
        return;
    }

    wlr_cursor_move(cursor->wlr_cursor, &event->pointer->base, dx, dy);
    wm_cursor_update(cursor);
}

static void handle_button(struct wl_listener* listener, void* data){
    struct wm_cursor* cursor = wl_container_of(listener, cursor, button);
    struct wlr_pointer_button_event* event = data;

    if(wm_callback_button(event)){
        wm_seat_kill_seatop(cursor->wm_seat);
        return;
    }

    wm_seat_dispatch_button(cursor->wm_seat, event);
}

static void handle_axis(struct wl_listener* listener, void* data){
    struct wm_cursor* cursor = wl_container_of(listener, cursor, axis);
    struct wlr_pointer_axis_event* event = data;

    if(wm_callback_axis(event)){
        return;
    }

    wm_seat_dispatch_axis(cursor->wm_seat, event);
}

static void handle_frame(struct wl_listener* listener, void* data){
    struct wm_cursor* cursor = wl_container_of(listener, cursor, frame);

    wlr_seat_pointer_notify_frame(cursor->wm_seat->wlr_seat);
}

static void handle_surface_destroy(struct wl_listener* listener, void* data){
    struct wm_cursor* cursor = wl_container_of(listener, cursor, surface_destroy);
    wm_cursor_set_image_surface(cursor, NULL, 0, 0);
}

static void handle_pointer_pinch_begin(struct wl_listener *listener, void *data) {
    struct wm_cursor *cursor = wl_container_of(
            listener, cursor, pinch_begin);
    struct wlr_pointer_pinch_begin_event *event = data;

    if(wm_callback_gesture_pinch_begin(event)){
        return;
    }

    cursor->pinch_started = true;
    wlr_pointer_gestures_v1_send_pinch_begin(
            cursor->pointer_gestures, cursor->wm_seat->wlr_seat,
            event->time_msec, event->fingers);
}

static void handle_pointer_pinch_update(struct wl_listener *listener, void *data) {
    struct wm_cursor *cursor = wl_container_of(
            listener, cursor, pinch_update);
    struct wlr_pointer_pinch_update_event *event = data;

    if(wm_callback_gesture_pinch_update(event)){
        return;
    }

    wlr_pointer_gestures_v1_send_pinch_update(
            cursor->pointer_gestures, cursor->wm_seat->wlr_seat,
            event->time_msec, event->dx, event->dy,
            event->scale, event->rotation);
}

static void handle_pointer_pinch_end(struct wl_listener *listener, void *data) {
    struct wm_cursor *cursor = wl_container_of(
            listener, cursor, pinch_end);
    struct wlr_pointer_pinch_end_event *event = data;

    if(wm_callback_gesture_pinch_end(event) && !cursor->pinch_started){
        return;
    }

    cursor->pinch_started = false;
    wlr_pointer_gestures_v1_send_pinch_end(
            cursor->pointer_gestures, cursor->wm_seat->wlr_seat,
            event->time_msec, event->cancelled);
}

static void handle_pointer_swipe_begin(struct wl_listener *listener, void *data) {
    struct wm_cursor *cursor = wl_container_of(
            listener, cursor, swipe_begin);
    struct wlr_pointer_swipe_begin_event *event = data;

    if(wm_callback_gesture_swipe_begin(event)){
        return;
    }

    cursor->swipe_started = true;
    wlr_pointer_gestures_v1_send_swipe_begin(
            cursor->pointer_gestures, cursor->wm_seat->wlr_seat,
            event->time_msec, event->fingers);
}

static void handle_pointer_swipe_update(struct wl_listener *listener, void *data) {
    struct wm_cursor *cursor = wl_container_of(
            listener, cursor, swipe_update);
    struct wlr_pointer_swipe_update_event *event = data;

    if(wm_callback_gesture_swipe_update(event)){
        return;
    }

    wlr_pointer_gestures_v1_send_swipe_update(
            cursor->pointer_gestures, cursor->wm_seat->wlr_seat,
            event->time_msec, event->dx, event->dy);
}

static void handle_pointer_swipe_end(struct wl_listener *listener, void *data) {
    struct wm_cursor *cursor = wl_container_of(
            listener, cursor, swipe_end);
    struct wlr_pointer_swipe_end_event *event = data;

    if(wm_callback_gesture_swipe_end(event) && !cursor->swipe_started){
        return;
    }

    cursor->swipe_started = false;
    wlr_pointer_gestures_v1_send_swipe_end(
            cursor->pointer_gestures, cursor->wm_seat->wlr_seat,
            event->time_msec, event->cancelled);
}

/*
 * Class implementation
 */
void wm_cursor_init(struct wm_cursor* cursor, struct wm_seat* seat, struct wm_layout* layout){
    cursor->wm_seat = seat;

    cursor->wlr_cursor = wlr_cursor_create();
    assert(cursor->wlr_cursor);

    wlr_cursor_attach_output_layout(cursor->wlr_cursor, layout->wlr_output_layout);

    cursor->wlr_xcursor_manager = NULL;
    wm_cursor_reconfigure(cursor);

    cursor->motion.notify = handle_motion;
    wl_signal_add(&cursor->wlr_cursor->events.motion, &cursor->motion);

    cursor->motion_absolute.notify = handle_motion_absolute;
    wl_signal_add(&cursor->wlr_cursor->events.motion_absolute, &cursor->motion_absolute);

    cursor->button.notify = handle_button;
    wl_signal_add(&cursor->wlr_cursor->events.button, &cursor->button);

    cursor->axis.notify = handle_axis;
    wl_signal_add(&cursor->wlr_cursor->events.axis, &cursor->axis);

    cursor->frame.notify = handle_frame;
    wl_signal_add(&cursor->wlr_cursor->events.frame, &cursor->frame);

    wl_list_init(&cursor->surface_destroy.link);
    cursor->surface_destroy.notify = handle_surface_destroy;

    cursor->pointer_gestures = wlr_pointer_gestures_v1_create(cursor->wm_seat->wm_server->wl_display);
    cursor->pinch_begin.notify = handle_pointer_pinch_begin;
    wl_signal_add(&cursor->wlr_cursor->events.pinch_begin, &cursor->pinch_begin);
    cursor->pinch_update.notify = handle_pointer_pinch_update;
    wl_signal_add(&cursor->wlr_cursor->events.pinch_update, &cursor->pinch_update);
    cursor->pinch_end.notify = handle_pointer_pinch_end;
    wl_signal_add(&cursor->wlr_cursor->events.pinch_end, &cursor->pinch_end);
    cursor->swipe_begin.notify = handle_pointer_swipe_begin;
    wl_signal_add(&cursor->wlr_cursor->events.swipe_begin, &cursor->swipe_begin);
    cursor->swipe_update.notify = handle_pointer_swipe_update;
    wl_signal_add(&cursor->wlr_cursor->events.swipe_update, &cursor->swipe_update);
    cursor->swipe_end.notify = handle_pointer_swipe_end;
    wl_signal_add(&cursor->wlr_cursor->events.swipe_end, &cursor->swipe_end);

    cursor->swipe_started = false;
    cursor->pinch_started = false;

    cursor->cursor_visible = 0;
}

void wm_cursor_ensure_loaded_for_scale(struct wm_cursor* cursor, double scale){
    wlr_xcursor_manager_load(cursor->wlr_xcursor_manager, scale);
}

void wm_cursor_destroy(struct wm_cursor* cursor) {
    wl_list_remove(&cursor->motion.link);
    wl_list_remove(&cursor->motion_absolute.link);
    wl_list_remove(&cursor->button.link);
    wl_list_remove(&cursor->axis.link);
    wl_list_remove(&cursor->frame.link);
    wl_list_remove(&cursor->surface_destroy.link);

    wl_list_remove(&cursor->pinch_begin.link);
    wl_list_remove(&cursor->pinch_update.link);
    wl_list_remove(&cursor->pinch_end.link);
    wl_list_remove(&cursor->swipe_begin.link);
    wl_list_remove(&cursor->swipe_update.link);
    wl_list_remove(&cursor->swipe_end.link);
}

void wm_cursor_add_pointer(struct wm_cursor* cursor, struct wm_pointer* pointer){
    wlr_cursor_attach_input_device(cursor->wlr_cursor, pointer->wlr_input_device);
}

void wm_cursor_update(struct wm_cursor* cursor){
    struct wm_content* r;
    wl_list_for_each(r, &cursor->wm_seat->wm_server->wm_contents, link) {
        if(wm_content_is_drag(r)){
            struct wm_drag* drag = wm_cast(wm_drag, r);
            wm_drag_update_position(drag);
        }
    }

    clock_t t_msec = clock() * 1000 / CLOCKS_PER_SEC;
    if(!wm_seat_dispatch_motion(cursor->wm_seat, cursor->wlr_cursor->x, cursor->wlr_cursor->y, t_msec + cursor->msec_delta)){
        wm_cursor_set_image(cursor, "left_ptr");
    }else{
        /* We are over a surface */
    }
}

void wm_cursor_set_visible(struct wm_cursor* cursor, int visible){
    cursor->cursor_visible = visible;

    if(cursor->client_image.surface){
        wm_cursor_set_image_surface(cursor,
                cursor->client_image.surface,
                cursor->client_image.hotspot_x,
                cursor->client_image.hotspot_y);
    }else{
        wm_cursor_set_image(cursor, "left_ptr");
    }
}

void wm_cursor_set_position(struct wm_cursor* cursor, int pos_x, int pos_y){
    wlr_cursor_move(cursor->wlr_cursor, NULL, pos_x - cursor->wlr_cursor->x, pos_y - cursor->wlr_cursor->y);
    wm_cursor_update(cursor);
}

void wm_cursor_set_image(struct wm_cursor* cursor, const char* image){
    wl_list_remove(&cursor->surface_destroy.link);
    wl_list_init(&cursor->surface_destroy.link);
    cursor->client_image.surface = NULL;

    if(!cursor->cursor_visible){
        wlr_cursor_unset_image(cursor->wlr_cursor);
    }else{
        wlr_cursor_set_xcursor(cursor->wlr_cursor, cursor->wlr_xcursor_manager, image);
    }
}

void wm_cursor_set_image_surface(struct wm_cursor* cursor, struct wlr_surface* surface, int32_t hotspot_x, int32_t hotspot_y){
    if(!surface){
        wm_cursor_set_image(cursor, "left_ptr");
        return;
    }

    wl_list_remove(&cursor->surface_destroy.link);
    wl_signal_add(&surface->events.destroy, &cursor->surface_destroy);

    cursor->client_image.surface = surface;
    cursor->client_image.hotspot_x = hotspot_x;
    cursor->client_image.hotspot_y = hotspot_y;

    if(!cursor->cursor_visible){
        wlr_cursor_unset_image(cursor->wlr_cursor);
    }else{
        wlr_cursor_set_surface(cursor->wlr_cursor, surface, hotspot_x, hotspot_y);
    }
}

void wm_cursor_reconfigure(struct wm_cursor* cursor){
    if(cursor->wlr_xcursor_manager){
        wlr_xcursor_manager_destroy(cursor->wlr_xcursor_manager);
    }

    wlr_log(WLR_DEBUG, "Loading cursor theme %s", cursor->wm_seat->wm_server->wm_config->xcursor_theme);
    cursor->wlr_xcursor_manager = wlr_xcursor_manager_create(
            cursor->wm_seat->wm_server->wm_config->xcursor_theme,
            cursor->wm_seat->wm_server->wm_config->xcursor_size);
    wlr_xcursor_manager_load(cursor->wlr_xcursor_manager, 1.);
}
