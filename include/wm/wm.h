#pragma once

#include <time.h>
#include <stdbool.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>

struct wm_view;
struct wm_server;
struct wm_layout;
struct wm_widget;

#define WM_CURSOR_MIN -1000000

struct wm {
    struct wm_server* server;

    void (*callback_layout_change)(struct wm_layout*);
    bool (*callback_key)(struct wlr_keyboard_key_event*, const char* keysyms);
    bool (*callback_modifiers)(struct wlr_keyboard_modifiers*);
    bool (*callback_motion)(double, double, double, double, uint32_t);
    bool (*callback_button)(struct wlr_pointer_button_event*);
    bool (*callback_axis)(struct wlr_pointer_axis_event*);

    bool (*callback_gesture_swipe_begin)(struct wlr_pointer_swipe_begin_event* event);
    bool (*callback_gesture_swipe_update)(struct wlr_pointer_swipe_update_event* event);
    bool (*callback_gesture_swipe_end)(struct wlr_pointer_swipe_end_event* event);
    bool (*callback_gesture_pinch_begin)(struct wlr_pointer_pinch_begin_event* event);
    bool (*callback_gesture_pinch_update)(struct wlr_pointer_pinch_update_event* event);
    bool (*callback_gesture_pinch_end)(struct wlr_pointer_pinch_end_event* event);
    bool (*callback_gesture_hold_begin)(struct wlr_pointer_hold_begin_event* event);
    bool (*callback_gesture_hold_end)(struct wlr_pointer_hold_end_event* event);

    void (*callback_init_view)(struct wm_view*);
    void (*callback_destroy_view)(struct wm_view*);
    void (*callback_view_event)(struct wm_view*, const char* event);

    /* Once the server is ready, and we can create new threads */
    void (*callback_ready)(void);

    void (*callback_update_view)(struct wm_view*);

    /* Synchronous update once per frame */
    void (*callback_update)(void);
};

void wm_init();
void wm_destroy();
int wm_run();
void wm_terminate();

void wm_focus_view(struct wm_view* view);
void wm_update_cursor(int cursor_visible, int pos_x, int pos_y);

void wm_set_locked(double locked);

void wm_open_virtual_output(const char* name);
void wm_close_virtual_output(const char* name);

/*
 * Instead of writing setters for every single callback,
 * just put them in this object
 */
struct wm* get_wm();

void wm_callback_layout_change(struct wm_layout* layout);

/*
 * Return false if event should be dispatched to clients
 */
bool wm_callback_key(struct wlr_keyboard_key_event* event, const char* keysyms);
bool wm_callback_modifiers(struct wlr_keyboard_modifiers* modifiers);
bool wm_callback_motion(double delta_x, double delta_y, double abs_x, double abs_y, uint32_t time_msec);
bool wm_callback_button(struct wlr_pointer_button_event* event);
bool wm_callback_axis(struct wlr_pointer_axis_event* event);

bool wm_callback_gesture_swipe_begin(struct wlr_pointer_swipe_begin_event* event);
bool wm_callback_gesture_swipe_update(struct wlr_pointer_swipe_update_event* event);
bool wm_callback_gesture_swipe_end(struct wlr_pointer_swipe_end_event* event);
bool wm_callback_gesture_pinch_begin(struct wlr_pointer_pinch_begin_event* event);
bool wm_callback_gesture_pinch_update(struct wlr_pointer_pinch_update_event* event);
bool wm_callback_gesture_pinch_end(struct wlr_pointer_pinch_end_event* event);
bool wm_callback_gesture_hold_begin(struct wlr_pointer_hold_begin_event* event);
bool wm_callback_gesture_hold_end(struct wlr_pointer_hold_end_event* event);

/*
 * Should set display_x, display_y, display_height, display_width
 * Can also call set_size
 */
void wm_callback_init_view(struct wm_view* view);
void wm_callback_destroy_view(struct wm_view* view);
void wm_callback_view_event(struct wm_view* view, const char* event);

void wm_callback_update_view(struct wm_view* view);
void wm_callback_update();
void wm_callback_ready();
