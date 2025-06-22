#ifndef WM_SEAT_H
#define WM_SEAT_H

#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_compositor.h>

struct wm_server;
struct wm_cursor;
struct wm_layout;

struct wm_seat{
    struct wm_server* wm_server;

    struct wm_cursor* wm_cursor;

    struct wlr_seat* wlr_seat;
    struct wl_list wm_keyboards;
    struct wl_list wm_pointers;

    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;
    struct wl_listener request_set_cursor;
    struct wl_listener destroy;

    struct {
        double x0, xm;
        double y0, ym;
        bool active;
    } seatop_down;
};

void wm_seat_init(struct wm_seat* seat, struct wm_server* server, struct wm_layout* layout);
void wm_seat_destroy(struct wm_seat* seat);

void wm_seat_add_input_device(struct wm_seat* seat, struct wlr_input_device* input_device);

void wm_seat_clear_focus(struct wm_seat* seat);
void wm_seat_focus_surface(struct wm_seat* seat, struct wlr_surface* surface);

/* Pass input on to client */
void wm_seat_dispatch_key(struct wm_seat* seat, struct wlr_input_device* input_device, struct wlr_keyboard_key_event* event);
void wm_seat_dispatch_modifiers(struct wm_seat* seat, struct wlr_input_device* input_device);

/* true means event has been dispatched */
bool wm_seat_dispatch_motion(struct wm_seat* seat, double x, double y, uint32_t time_msec);
void wm_seat_dispatch_button(struct wm_seat* seat, struct wlr_pointer_button_event* event);
void wm_seat_dispatch_axis(struct wm_seat* seat, struct wlr_pointer_axis_event* event);
void wm_seat_kill_seatop(struct wm_seat* seat);

void wm_seat_reconfigure(struct wm_seat* seat);

#endif
