#define _POSIX_C_SOURCE 200809L

#include "wm/wm.h"

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include "wm/wm_cursor.h"
#include "wm/wm_layout.h"
#include "wm/wm_seat.h"
#include "wm/wm_server.h"
#include "wm/wm_view.h"
#include "wm/wm_widget.h"
#include "wm/wm_util.h"
#include "wm/wm_config.h"

struct wm wm = {0};

void wm_init(struct wm_config *config) {
    if (wm.server)
        return;

    wlr_log_init(config->debug ? WLR_DEBUG : WLR_INFO, NULL);
    wm.server = calloc(1, sizeof(struct wm_server));
    wm_server_init(wm.server, config);
}

void wm_destroy() {
    if (!wm.server)
        return;

    wm_server_destroy(wm.server);
    free(wm.server);
    wm.server = 0;
}

void *run() {
    if (!wm.server || !wm.server->wl_display)
        return NULL;

    /* Setup socket and set env */
    const char *socket = wl_display_add_socket_auto(wm.server->wl_display);
    if (!socket) {
        wlr_log_errno(WLR_ERROR, "Unable to open wayland socket");
        wlr_backend_destroy(wm.server->wlr_backend);
        return NULL;
    }

    wlr_log(WLR_INFO, "Running compositor on wayland display '%s'", socket);
    setenv("_WAYLAND_DISPLAY", socket, true);

    wlr_log(WLR_INFO, "Attempting to start backend");
    if (!wlr_backend_start(wm.server->wlr_backend)) {
        wlr_log(WLR_ERROR, "Failed to start backend (!!!)");
        wlr_backend_destroy(wm.server->wlr_backend);
        wl_display_destroy(wm.server->wl_display);
        return NULL;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
#ifdef WM_HAS_XWAYLAND
    if (wm.server->wlr_xwayland) {
        setenv("DISPLAY", wm.server->wlr_xwayland->display_name, true);
    }else{
        wm_callback_ready();
    }
#endif

    /* Main */
    wlr_log(WLR_INFO, "Main...");
    wl_display_run(wm.server->wl_display);

    unsetenv("_WAYLAND_DISPLAY");
    unsetenv("WAYLAND_DISPLAY");
    unsetenv("DISPLAY");

    return NULL;
}

int wm_run() {
    if (!wm.server)
        return 2;

    run();

    return 0;
}

void wm_terminate() {
    if (!wm.server)
        return;

    wl_display_terminate(wm.server->wl_display);
}

void wm_focus_view(struct wm_view *view) {
    if (!wm.server)
        return;

    wm_view_focus(view, wm.server->wm_seat);
}

void wm_update_cursor(int cursor_visible, int pos_x, int pos_y) {
    if (!wm.server)
        return;

    wm_cursor_set_visible(wm.server->wm_seat->wm_cursor, cursor_visible);

    if(pos_x > WM_CURSOR_MIN && pos_y > WM_CURSOR_MIN){
        wm_cursor_set_position(wm.server->wm_seat->wm_cursor, pos_x, pos_y);
    }

    wm_cursor_update(wm.server->wm_seat->wm_cursor);
}

void wm_set_locked(double locked) {
    if (!wm.server)
        return;

    wm_server_set_locked(wm.server, locked);
}

void wm_open_virtual_output(const char* name){
   wm_server_open_virtual_output(wm.server, name);
}

void wm_close_virtual_output(const char* name){
   wm_server_close_virtual_output(wm.server, name);
}

struct wm *get_wm() {
    return &wm;
}

/*
 * Callbacks
 */
void wm_callback_layout_change(struct wm_layout *layout) {
    TIMER_START(callback_layout_change);
    if (wm.callback_layout_change) {
        (*wm.callback_layout_change)(layout);
    }
    TIMER_STOP(callback_layout_change);
    TIMER_PRINT(callback_layout_change);
}

bool wm_callback_key(struct wlr_keyboard_key_event *event,
                     const char *keysyms) {
    TIMER_START(callback_key);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if (wm.callback_key) {
        res = (*wm.callback_key)(event, keysyms);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_key);
    TIMER_PRINT(callback_key);
    return res;
}

bool wm_callback_modifiers(struct wlr_keyboard_modifiers *modifiers) {
    TIMER_START(callback_modifiers);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if (wm.callback_modifiers) {
        res = (*wm.callback_modifiers)(modifiers);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_modifiers);
    TIMER_PRINT(callback_modifiers);
    return res;
}

bool wm_callback_motion(double delta_x, double delta_y, double abs_x, double abs_y, uint32_t time_msec) {
    TIMER_START(callback_motion);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if (wm.callback_motion) {
        res = (*wm.callback_motion)(delta_x, delta_y, abs_x, abs_y, time_msec);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_motion);
    TIMER_PRINT(callback_motion);

    return res;
}

bool wm_callback_button(struct wlr_pointer_button_event *event) {
    TIMER_START(callback_button);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if (wm.callback_button) {
        res = (*wm.callback_button)(event);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_button);
    TIMER_PRINT(callback_button);

    return res;
}

bool wm_callback_axis(struct wlr_pointer_axis_event *event) {
    TIMER_START(callback_axis);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if (wm.callback_axis) {
        res = (*wm.callback_axis)(event);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_axis);
    TIMER_PRINT(callback_axis);

    return res;
}

bool wm_callback_gesture_swipe_begin(struct wlr_pointer_swipe_begin_event* event){
    TIMER_START(callback_gesture_swipe_begin);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if(wm.callback_gesture_swipe_begin){
        res = (*wm.callback_gesture_swipe_begin)(event);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_gesture_swipe_begin);
    TIMER_PRINT(callback_gesture_swipe_begin);

    return res;
}
bool wm_callback_gesture_swipe_update(struct wlr_pointer_swipe_update_event* event){
    TIMER_START(callback_gesture_swipe_update);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if(wm.callback_gesture_swipe_update){
        res = (*wm.callback_gesture_swipe_update)(event);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_gesture_swipe_update);
    TIMER_PRINT(callback_gesture_swipe_update);

    return res;
}
bool wm_callback_gesture_swipe_end(struct wlr_pointer_swipe_end_event* event){
    TIMER_START(callback_gesture_swipe_end);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if(wm.callback_gesture_swipe_end){
        res = (*wm.callback_gesture_swipe_end)(event);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_gesture_swipe_end);
    TIMER_PRINT(callback_gesture_swipe_end);

    return res;
}
bool wm_callback_gesture_pinch_begin(struct wlr_pointer_pinch_begin_event* event){
    TIMER_START(callback_gesture_pinch_begin);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if(wm.callback_gesture_pinch_begin){
        res = (*wm.callback_gesture_pinch_begin)(event);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_gesture_pinch_begin);
    TIMER_PRINT(callback_gesture_pinch_begin);

    return res;
}
bool wm_callback_gesture_pinch_update(struct wlr_pointer_pinch_update_event* event){
    TIMER_START(callback_gesture_pinch_update);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if(wm.callback_gesture_pinch_update){
        res = (*wm.callback_gesture_pinch_update)(event);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_gesture_pinch_update);
    TIMER_PRINT(callback_gesture_pinch_update);

    return res;
}
bool wm_callback_gesture_pinch_end(struct wlr_pointer_pinch_end_event* event){
    TIMER_START(callback_gesture_pinch_end);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if(wm.callback_gesture_pinch_end){
        res = (*wm.callback_gesture_pinch_end)(event);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_gesture_pinch_end);
    TIMER_PRINT(callback_gesture_pinch_end);

    return res;
}
bool wm_callback_gesture_hold_begin(struct wlr_pointer_hold_begin_event* event){
    TIMER_START(callback_gesture_hold_begin);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if(wm.callback_gesture_hold_begin){
        res = (*wm.callback_gesture_hold_begin)(event);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_gesture_hold_begin);
    TIMER_PRINT(callback_gesture_hold_begin);
    return res;
}
bool wm_callback_gesture_hold_end(struct wlr_pointer_hold_end_event* event){
    TIMER_START(callback_gesture_hold_end);
    DEBUG_PERFORMANCE(callback_start, 0);
    bool res = false;
    if(wm.callback_gesture_hold_end){
        res = (*wm.callback_gesture_hold_end)(event);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_gesture_hold_end);
    TIMER_PRINT(callback_gesture_hold_end);
    return res;
}

void wm_callback_init_view(struct wm_view *view) {
    TIMER_START(callback_init_view);
    DEBUG_PERFORMANCE(callback_start, 0);
    if (wm.callback_init_view) {
        (*wm.callback_init_view)(view);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_init_view);
    TIMER_PRINT(callback_init_view);
}

void wm_callback_destroy_view(struct wm_view *view) {
    TIMER_START(callback_destroy_view);
    DEBUG_PERFORMANCE(callback_start, 0);
    if (wm.callback_destroy_view) {
        (*wm.callback_destroy_view)(view);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_destroy_view);
    TIMER_PRINT(callback_destroy_view);
}

void wm_callback_view_event(struct wm_view *view, const char *event) {
    TIMER_START(callback_view_event);
    DEBUG_PERFORMANCE(callback_start, 0);
    if (wm.callback_view_event) {
        (*wm.callback_view_event)(view, event);
    }
    DEBUG_PERFORMANCE(callback_finish, 0);
    TIMER_STOP(callback_view_event);
    TIMER_PRINT(callback_view_event);
}

void wm_callback_update_view(struct wm_view *view){
    TIMER_START(callback_update_view);
    DEBUG_PERFORMANCE(py_start, 0);
    if (wm.callback_update_view) {
        (*wm.callback_update_view)(view);
    }
    DEBUG_PERFORMANCE(py_finish, 0);
    TIMER_STOP(callback_update_view);
    TIMER_PRINT(callback_update_view);
}

void wm_callback_update() {
    TIMER_START(callback_update);
    if (wm.callback_update) {
        (*wm.callback_update)();
    }
    TIMER_STOP(callback_update);
    TIMER_PRINT(callback_update);
}

void wm_callback_ready() {
    TIMER_START(callback_ready);
    if (wm.callback_ready) {
        (*wm.callback_ready)();
    }
    TIMER_STOP(callback_ready);
    TIMER_PRINT(callback_ready);
}
