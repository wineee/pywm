#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/util/log.h>

#include "wm/wm_content.h"
#include "wm/wm_output.h"
#include "wm/wm_server.h"
#include "wm/wm_layout.h"
#include "wm/wm_view.h"
#include "wm/wm_view_xdg.h"
#include "wm/wm_util.h"

struct wm_content_vtable wm_content_base_vtable;

void wm_content_init(struct wm_content* content, struct wm_server* server) {
    content->vtable = &wm_content_base_vtable;

    content->wm_server = server;
    content->display_x = 0.;
    content->display_y = 0.;
    content->display_width = 0.;
    content->display_height = 0.;
    content->corner_radius = 0.;

    content->fixed_output = NULL;
    content->workspace_x = 0.;
    content->workspace_y = 0.;
    content->workspace_width = -1.;
    content->workspace_height = -1.;


    content->z_index = 0;
    wl_list_insert(&content->wm_server->wm_contents, &content->link);

    content->lock_enabled = false;
}

void wm_content_base_destroy(struct wm_content* content) {
    wl_list_remove(&content->link);
}

void wm_content_set_output(struct wm_content* content, int key, struct wlr_output* outp){
    struct wm_output* res = NULL;
    if(key>=0 || outp){
        struct wm_output* output;
        wl_list_for_each(output, &content->wm_server->wm_layout->wm_outputs, link){
            if(key == output->key || (outp && outp == output->wlr_output)){
                res = output;
                break;
            }
        }
    }

    if(!res && (outp || key>=0)){
        wlr_log(WLR_ERROR, "Invalid output (%d) given to wm_content_set_output", key);
    }

    if(res == content->fixed_output){
        return;
    }

    wm_layout_damage_from(content->wm_server->wm_layout, content, NULL);
    content->fixed_output = res;
    wm_layout_damage_from(content->wm_server->wm_layout, content, NULL);

    wm_layout_update_content_outputs(content->wm_server->wm_layout, content);
}

struct wm_output* wm_content_get_output(struct wm_content* content){
    if(!content->fixed_output) return NULL;

    struct wm_output* output;
    wl_list_for_each(output, &content->wm_server->wm_layout->wm_outputs, link){
        if(output == content->fixed_output) return output;
    }

    wlr_log(WLR_ERROR, "Removing invalid fixed output");
    content->fixed_output = NULL;
    return NULL;
}

void wm_content_set_workspace(struct wm_content* content, double x, double y, double width, double height){
    if(fabs(content->workspace_x - x) +
            fabs(content->workspace_y - y) +
            fabs(content->workspace_width - width) +
            fabs(content->workspace_height - height) < 0.01) return;

    wm_layout_damage_from(content->wm_server->wm_layout, content, NULL);
    content->workspace_x = x;
    content->workspace_y = y;
    content->workspace_width = width;
    content->workspace_height = height;
    wm_layout_damage_from(content->wm_server->wm_layout, content, NULL);

    wm_layout_update_content_outputs(content->wm_server->wm_layout, content);
}

void wm_content_get_workspace(struct wm_content* content, double* workspace_x, double* workspace_y, double* workspace_width, double* workspace_height){
    *workspace_x = content->workspace_x;
    *workspace_y = content->workspace_y;
    *workspace_width = content->workspace_width;
    *workspace_height = content->workspace_height;
}

bool wm_content_has_workspace(struct wm_content* content){
    return !(content->workspace_width < 0 || content->workspace_height < 0);
}

bool wm_content_is_on_output(struct wm_content* content, struct wm_output* output){
    double display_x, display_y, display_width, display_height;
    wm_content_get_box(content, &display_x, &display_y, &display_width, &display_height);
    struct wlr_box box = {
        .x = display_x,
        .y = display_y,
        .width = display_width,
        .height = display_height
    };

    return content->fixed_output == output 
        || (wlr_output_layout_intersects(output->wm_layout->wlr_output_layout, output->wlr_output, &box)
            && content->fixed_output == NULL);
}

void wm_content_set_box(struct wm_content* content, double x, double y, double width, double height) {
    if(fabs(content->display_x - x) +
            fabs(content->display_y - y) + 
            fabs(content->display_width - width) +
            fabs(content->display_height - height) < 0.01) return;

    wm_layout_damage_from(content->wm_server->wm_layout, content, NULL);
    content->display_x = x;
    content->display_y = y;
    content->display_width = width;
    content->display_height = height;
    wm_layout_damage_from(content->wm_server->wm_layout, content, NULL);

    /* Update scene node position if this is a view */
    if (wm_content_is_view(content)) {
        struct wm_view* view = wm_cast(wm_view, content);
        if (wm_view_is_xdg(view)) {
            struct wm_view_xdg* xdg_view = wm_cast(wm_view_xdg, view);
            if (xdg_view->scene_node) {
                wlr_scene_node_set_position(xdg_view->scene_node, x, y);
            }
        }
    }

    wm_layout_update_content_outputs(content->wm_server->wm_layout, content);
}

void wm_content_get_box(struct wm_content* content, double* display_x, double* display_y, double* display_width, double* display_height){
    *display_x = content->display_x;
    *display_y = content->display_y;
    *display_width = content->display_width;
    *display_height = content->display_height;
}

void wm_content_set_z_index(struct wm_content* content, double z_index){
    if(fabs(z_index - content->z_index) < 0.0001) return;

    content->z_index = z_index;
    wm_layout_damage_from(content->wm_server->wm_layout, content, NULL);

    /* Update scene node position if this is a view */
    if (wm_content_is_view(content)) {
        struct wm_view* view = wm_cast(wm_view, content);
        if (wm_view_is_xdg(view)) {
            struct wm_view_xdg* xdg_view = wm_cast(wm_view_xdg, view);
            if (xdg_view->scene_node) {
                /* Raise to top to ensure proper z-ordering */
                wlr_scene_node_raise_to_top(xdg_view->scene_node);
            }
        }
    }
}

double wm_content_get_z_index(struct wm_content* content){
    return content->z_index;
}

void wm_content_set_opacity(struct wm_content* content, double opacity){
    if(fabs(content->opacity - opacity) < 0.0001) return;

    content->opacity = opacity;
    wm_layout_damage_from(content->wm_server->wm_layout, content, NULL);

    /* Update scene node opacity if this is a view */
    if (wm_content_is_view(content)) {
        struct wm_view* view = wm_cast(wm_view, content);
        if (wm_view_is_xdg(view)) {
            struct wm_view_xdg* xdg_view = wm_cast(wm_view_xdg, view);
            if (xdg_view->scene_node) {
                struct wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(xdg_view->scene_node);
                if (scene_buffer) {
                    wlr_scene_buffer_set_opacity(scene_buffer, opacity);
                }
            }
        }
    }
}

double wm_content_get_opacity(struct wm_content* content){
   return content->opacity;
}

void wm_content_set_mask(struct wm_content* content, double mask_x, double mask_y, double mask_w, double mask_h){
    if(fabs(content->mask_x - mask_x) +
            fabs(content->mask_y - mask_y) + 
            fabs(content->mask_w - mask_w) +
            fabs(content->mask_h - mask_h) < 0.0001) return;

    content->mask_x = mask_x;
    content->mask_y = mask_y;
    content->mask_w = mask_w;
    content->mask_h = mask_h;

    wm_layout_damage_from(content->wm_server->wm_layout, content, NULL);
}
void wm_content_get_mask(struct wm_content* content, double* mask_x, double* mask_y, double* mask_w, double* mask_h){
    *mask_x = content->mask_x;
    *mask_y = content->mask_y;
    *mask_w = content->mask_w;
    *mask_h = content->mask_h;

    if(*mask_w < 0) *mask_w = -*mask_x + content->display_width + 1;
    if(*mask_h < 0) *mask_h = -*mask_h + content->display_height + 1;
}

void wm_content_set_corner_radius(struct wm_content* content, double corner_radius){
    if(fabs(content->corner_radius - corner_radius) < 0.01) return;

    content->corner_radius = corner_radius;
    wm_layout_damage_from(content->wm_server->wm_layout, content, NULL);

    /* Update scene node corner radius if this is a view */
    if (wm_content_is_view(content)) {
        struct wm_view* view = wm_cast(wm_view, content);
        if (wm_view_is_xdg(view)) {
            struct wm_view_xdg* xdg_view = wm_cast(wm_view_xdg, view);
            if (xdg_view->scene_node) {
                struct wlr_scene_buffer* scene_buffer = wlr_scene_buffer_from_node(xdg_view->scene_node);
                if (scene_buffer) {
                    /* Note: wlr_scene doesn't directly support corner radius,
                     * but we can set the opaque region to achieve a similar effect */
                    /* For now, we'll just update the content's corner radius */
                }
            }
        }
    }
}

void wm_content_set_lock_enabled(struct wm_content* content, bool lock_enabled){
    if(lock_enabled == content->lock_enabled) return;

    content->lock_enabled = lock_enabled;
    wm_layout_damage_from(content->wm_server->wm_layout, content, NULL);
}

double wm_content_get_corner_radius(struct wm_content* content){
    return content->corner_radius;
}

void wm_content_destroy(struct wm_content* content){
    wm_layout_damage_from(content->wm_server->wm_layout, content, NULL);
    (*content->vtable->destroy)(content);
}

void wm_content_render(struct wm_content* content, struct wm_output* output, pixman_region32_t* output_damage, struct timespec now){
    if(!wm_content_is_on_output(content, output)) return;

    pixman_region32_t damage_on_workspace;
    pixman_region32_init(&damage_on_workspace);
    pixman_region32_copy(&damage_on_workspace, output_damage);
    if(wm_content_has_workspace(content)){
        int x = round((content->workspace_x - output->layout_x) * output->wlr_output->scale);
        int y = round((content->workspace_y - output->layout_y) * output->wlr_output->scale);
        int w = round(content->workspace_width * output->wlr_output->scale);
        int h = round(content->workspace_height * output->wlr_output->scale);
        pixman_region32_intersect_rect(&damage_on_workspace, &damage_on_workspace, x, y, w, h);
    }

    (*content->vtable->render)(content, output, &damage_on_workspace, now);

    pixman_region32_fini(&damage_on_workspace);
}

void wm_content_damage_output_base(struct wm_content* content, struct wm_output* output, struct wlr_surface* origin){
    pixman_region32_t region;
    pixman_region32_init(&region);

    double x, y, w, h;
    wm_content_get_box(content, &x, &y, &w, &h);
    x -= output->layout_x;
    y -= output->layout_y;

    x *= output->wlr_output->scale;
    y *= output->wlr_output->scale;
    w *= output->wlr_output->scale;
    h *= output->wlr_output->scale;
    pixman_region32_union_rect(&region, &region,
            floor(x), floor(y),
            ceil(x + w) - floor(x), ceil(y + h) - floor(y));

    if(wm_content_has_workspace(content)){
        double workspace_x, workspace_y, workspace_w, workspace_h;
        wm_content_get_workspace(content, &workspace_x, &workspace_y,
                                 &workspace_w, &workspace_h);
        workspace_x = (workspace_x - output->layout_x) * output->wlr_output->scale;
        workspace_y = (workspace_y - output->layout_y) * output->wlr_output->scale;
        workspace_w *= output->wlr_output->scale;
        workspace_h *= output->wlr_output->scale;
        pixman_region32_intersect_rect(
            &region, &region,
            floor(workspace_x),
            floor(workspace_y),
            ceil(workspace_x + workspace_w) - floor(workspace_x),
            ceil(workspace_y + workspace_h) - floor(workspace_y));
    }

    wm_layout_damage_output(output->wm_layout, output, &region, content);
    pixman_region32_fini(&region);
}

struct wm_content_vtable wm_content_base_vtable = {
    .destroy = wm_content_base_destroy,
};
