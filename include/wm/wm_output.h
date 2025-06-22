#ifndef WM_OUTPUT_H
#define WM_OUTPUT_H

#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_damage_ring.h>

struct wm_layout;
struct wm_renderer_buffers;

struct wm_output {
    struct wm_server* wm_server;
    struct wm_layout* wm_layout;
    struct wl_list link; // wm_layout::wm_outputs

    int layout_x; // Duplicated from wlr_output_layout
    int layout_y; // Duplicated from wlr_output_layout

    int key; // Unique key in a layout - update on layout change

    struct wlr_output* wlr_output;
    struct wlr_damage_ring damage_ring;

    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener damage;
    struct wl_listener frame;
    struct wl_listener needs_frame;

    bool expecting_frame;
    struct timespec last_frame;

#if WM_CUSTOM_RENDERER
    struct wm_renderer_buffers* renderer_buffers;
#endif
};

void wm_output_init(struct wm_output* output, struct wm_server* server, struct wm_layout *layout, struct wlr_output* out);
void wm_output_destroy(struct wm_output* output);

void wm_output_reconfigure(struct wm_output* output);


/*
 * Override name of next output to be initialised
 * Necessary, as wlroots provides no way of naming
 * headless outputs
 */
void wm_output_override_name(const char* name);


#endif
