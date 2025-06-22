#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <wayland-server.h>
#include <wlr/util/log.h>
#include <wlr/backend.h>
#include <wlr/backend/multi.h>
#include <xkbcommon/xkbcommon.h>

#include "wm/wm_keyboard.h"
#include "wm/wm_seat.h"
#include "wm/wm_server.h"
#include "wm/wm_config.h"
#include "wm/wm.h"


#define KEYS_STRING_LENGTH 256

/*
 * Callbacks
 */
static void handle_destroy(struct wl_listener* listener, void* data){
    struct wm_keyboard* keyboard = wl_container_of(listener, keyboard, destroy);
    wm_keyboard_destroy(keyboard);
}

static void handle_key(struct wl_listener* listener, void* data){
    struct wm_keyboard* keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event* event = data;

    struct wlr_keyboard* wlr_keyboard = wlr_keyboard_from_input_device(keyboard->wlr_input_device);
    xkb_keycode_t keycode = event->keycode + 8;
    size_t keysyms_len;
    const xkb_keysym_t* keysyms;

    xkb_layout_index_t layout_index = xkb_state_key_get_layout(
            wlr_keyboard->xkb_state, keycode);
    keysyms_len = xkb_keymap_key_get_syms_by_level(
            wlr_keyboard->keymap,
            keycode, layout_index, 0, &keysyms);

    /* Translated logic consuming modifiers */
    size_t keysyms_trans_len;
    const xkb_keysym_t* keysyms_trans;
    keysyms_trans_len = xkb_state_key_get_syms(
            wlr_keyboard->xkb_state, keycode, &keysyms_trans);

    /* Copied from sway - switch VT on CTRL-ALT-Fx */
    for (size_t i = 0; i < keysyms_trans_len; ++i) {
        xkb_keysym_t keysym = keysyms_trans[i];
        if (keysym >= XKB_KEY_XF86Switch_VT_1 &&
            keysym <= XKB_KEY_XF86Switch_VT_12) {
            /*
            // FIXME: rewine
            if (wlr_backend_is_multi(
                    keyboard->wm_seat->wm_server->wlr_backend)) {
                struct wlr_session *session = wlr_backend_get_session(
                    keyboard->wm_seat->wm_server->wlr_backend);
                if (session) {
                    unsigned vt = keysym - XKB_KEY_XF86Switch_VT_1 + 1;
                    wlr_session_change_vt(session, vt);
                }
            }
            */
            return;
        }
    }

    char keys[KEYS_STRING_LENGTH] = { 0 };
    size_t at=0;
    for(size_t i=0; i<keysyms_len; i++){
        at += xkb_keysym_get_name(keysyms[i], keys + at, KEYS_STRING_LENGTH - at);
    }
    assert(at < KEYS_STRING_LENGTH - 1);


    if(keyboard->wm_seat->wm_server->wm_config->debug){
        if(!strcmp(keys, "F1") && event->state == WL_KEYBOARD_KEY_STATE_PRESSED){
            wm_server_printf(stderr, keyboard->wm_seat->wm_server);
        }
    }

    if(wm_callback_key(event, keys)){
        return;
    }

    wm_seat_dispatch_key(keyboard->wm_seat, keyboard->wlr_input_device, event);
}

static void handle_modifiers(struct wl_listener* listener, void* data){
    struct wm_keyboard* keyboard = wl_container_of(listener, keyboard, modifiers);
    struct wlr_keyboard* wlr_keyboard = wlr_keyboard_from_input_device(keyboard->wlr_input_device);

    if(wm_callback_modifiers(&wlr_keyboard->modifiers)){
        return;
    }

    wm_seat_dispatch_modifiers(keyboard->wm_seat, keyboard->wlr_input_device);
}

/*
 * Class implementation
 */
void wm_keyboard_init(struct wm_keyboard* keyboard, struct wm_seat* seat, struct wlr_input_device* input_device){
    keyboard->wm_seat = seat;
    keyboard->wlr_input_device = input_device;

    wm_keyboard_reconfigure(keyboard);

    /* Handlers */
    keyboard->destroy.notify = handle_destroy;
    wl_signal_add(&keyboard->wlr_input_device->events.destroy, &keyboard->destroy);

    struct wlr_keyboard* wlr_keyboard = wlr_keyboard_from_input_device(keyboard->wlr_input_device);
    keyboard->key.notify = handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

    keyboard->modifiers.notify = handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
}

void wm_keyboard_destroy(struct wm_keyboard* keyboard){
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->link);
}

void wm_keyboard_reconfigure(struct wm_keyboard* keyboard){
    struct xkb_rule_names rules = {0};
    rules.model = keyboard->wm_seat->wm_server->wm_config->xkb_model;
    rules.layout = keyboard->wm_seat->wm_server->wm_config->xkb_layout;
    rules.variant = keyboard->wm_seat->wm_server->wm_config->xkb_variant;
    rules.options = keyboard->wm_seat->wm_server->wm_config->xkb_options;

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    assert(context);

    struct xkb_keymap* keymap = xkb_map_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if(keymap){
        struct wlr_keyboard* wlr_keyboard = wlr_keyboard_from_input_device(keyboard->wlr_input_device);
        wlr_keyboard_set_keymap(wlr_keyboard, keymap);
        wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

        xkb_keymap_unref(keymap);
    }else{
        wlr_log(WLR_ERROR, "Could not load keymap for %s / %s / %s", rules.model, rules.layout, rules.options);
    }

    xkb_context_unref(context);
}
