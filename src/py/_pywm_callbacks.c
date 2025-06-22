#include <Python.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <wlr/util/log.h>
#include "wm/wm.h"
#include "wm/wm_layout.h"
#include "wm/wm_output.h"
#include "py/_pywm_callbacks.h"
#include "py/_pywm_view.h"

static struct _pywm_callbacks callbacks = { 0 };

/*
 * Helpers
 */
static bool call_bool(PyObject* callable, PyObject* args){
    PyObject *_result = PyObject_Call(callable, args, NULL);
    Py_XDECREF(args);

    int result = false;
    if(!_result || _result == Py_None || !PyArg_Parse(_result, "b", &result)){
        wlr_log(WLR_DEBUG, "Python error: Expected boolean return");
    }
    Py_XDECREF(_result);
    return result;
}

static void call_void(PyObject* callable, PyObject* args){
    PyObject *_result = PyObject_Call(callable, args, NULL);
    Py_XDECREF(args);

    if(!_result){
        wlr_log(WLR_DEBUG, "Python error: Exception thrown");
    }
    Py_XDECREF(_result);
}

/*
 * Callbacks
 */
static void call_layout_change(struct wm_layout* layout){
    if(callbacks.layout_change){
        PyGILState_STATE gil = PyGILState_Ensure();

        PyObject* list = PyList_New(wl_list_length(&layout->wm_outputs));
        struct wm_output* output;
        int i=0;


        wl_list_for_each(output, &layout->wm_outputs, link){
            int width, height;
            wlr_output_effective_resolution(output->wlr_output, &width, &height);

            PyList_SetItem(list, i, Py_BuildValue(
                               "(sidiiii)",
                               output->wlr_output->name,
                               output->key,
                               output->wlr_output->scale,
                               width,
                               height,
                               output->layout_x,
                               output->layout_y));
            i++;
        }
        PyObject* args = Py_BuildValue("(O)", list);
        call_void(callbacks.layout_change, args);
        PyGILState_Release(gil);
    }
}

static bool call_key(struct wlr_keyboard_key_event* event, const char* keysyms){
    if(callbacks.key){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(iiis)", event->time_msec, event->keycode, event->state, keysyms);
        bool result = call_bool(callbacks.key, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}

static bool call_modifiers(struct wlr_keyboard_modifiers* modifiers){
    if(callbacks.modifiers){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(iiii)", modifiers->depressed, modifiers->latched, modifiers->locked, modifiers->group);
        bool result = call_bool(callbacks.modifiers, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}

static bool call_motion(double delta_x, double delta_y, double abs_x, double abs_y, uint32_t time_msec){
    if(callbacks.motion){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(idddd)", time_msec, delta_x, delta_y, abs_x, abs_y);
        bool result = call_bool(callbacks.motion, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}


static bool call_button(struct wlr_pointer_button_event* event){
    if(callbacks.button){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(iii)", event->time_msec, event->button, event->state);
        bool result = call_bool(callbacks.button, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}

static bool call_axis(struct wlr_pointer_axis_event* event){
    if(callbacks.axis){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(iiidi)", event->time_msec, event->source, event->orientation,
                event->delta, event->delta_discrete);
        bool result = call_bool(callbacks.axis, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}

static bool call_pinch_begin(struct wlr_pointer_pinch_begin_event* event){
    if(callbacks.gesture){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(sii)", "pinch", event->time_msec, event->fingers);
        bool result = call_bool(callbacks.gesture, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}
static bool call_pinch_update(struct wlr_pointer_pinch_update_event* event){
    if(callbacks.gesture){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(siidddd)", "pinch", event->time_msec, event->fingers, event->dx, event->dy, event->rotation, event->scale);
        bool result = call_bool(callbacks.gesture, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}
static bool call_pinch_end(struct wlr_pointer_pinch_end_event* event){
    if(callbacks.gesture){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(sii)", "pinch", event->time_msec, event->cancelled);
        bool result = call_bool(callbacks.gesture, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}
static bool call_swipe_begin(struct wlr_pointer_swipe_begin_event* event){
    if(callbacks.gesture){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(sii)", "swipe", event->time_msec, event->fingers);
        bool result = call_bool(callbacks.gesture, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}
static bool call_swipe_update(struct wlr_pointer_swipe_update_event* event){
    if(callbacks.gesture){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(siidd)", "swipe", event->time_msec, event->fingers, event->dx, event->dy);
        bool result = call_bool(callbacks.gesture, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}
static bool call_swipe_end(struct wlr_pointer_swipe_end_event* event){
    if(callbacks.gesture){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(sii)", "swipe", event->time_msec, event->cancelled);
        bool result = call_bool(callbacks.gesture, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}
static bool call_hold_begin(struct wlr_pointer_hold_begin_event* event){
    if(callbacks.gesture){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(sii)", "hold", event->time_msec, event->fingers);
        bool result = call_bool(callbacks.gesture, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}
static bool call_hold_end(struct wlr_pointer_hold_end_event* event){
    if(callbacks.gesture){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(sii)", "hold", event->time_msec, event->cancelled);
        bool result = call_bool(callbacks.gesture, args);
        PyGILState_Release(gil);
        return result;
    }

    return false;
}

static void call_init_view(struct wm_view* view){
    _pywm_views_add(view);

    PyGILState_STATE gil = PyGILState_Ensure();
    _pywm_views_update_single(view);
    PyGILState_Release(gil);
}

static void call_destroy_view(struct wm_view* view){
    if(callbacks.destroy_view){
        long handle = _pywm_views_remove(view);
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(l)", handle);
        call_void(callbacks.destroy_view, args);
        PyGILState_Release(gil);
    }
}

static void call_view_event(struct wm_view* view, const char* event){
    if(callbacks.view_event){
        long handle = _pywm_views_get_handle(view);
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("(ls)", handle, event);
        call_void(callbacks.view_event, args);
        PyGILState_Release(gil);
    }
}


static void call_ready(){
    if(callbacks.ready){
        PyGILState_STATE gil = PyGILState_Ensure();
        PyObject* args = Py_BuildValue("()");
        call_void(callbacks.ready, args);
        PyGILState_Release(gil);
    }
}

/*
 * Public interface
 */
void _pywm_callbacks_init(){
    get_wm()->callback_ready = &call_ready;
    get_wm()->callback_layout_change = &call_layout_change;
    get_wm()->callback_key = &call_key;
    get_wm()->callback_modifiers = &call_modifiers;
    get_wm()->callback_motion = &call_motion;
    get_wm()->callback_button = &call_button;
    get_wm()->callback_axis = &call_axis;

    get_wm()->callback_gesture_pinch_begin = &call_pinch_begin;
    get_wm()->callback_gesture_pinch_update = &call_pinch_update;
    get_wm()->callback_gesture_pinch_end = &call_pinch_end;
    get_wm()->callback_gesture_swipe_begin = &call_swipe_begin;
    get_wm()->callback_gesture_swipe_update = &call_swipe_update;
    get_wm()->callback_gesture_swipe_end = &call_swipe_end;
    get_wm()->callback_gesture_hold_begin = &call_hold_begin;
    get_wm()->callback_gesture_hold_end = &call_hold_end;

    get_wm()->callback_init_view = &call_init_view;
    get_wm()->callback_destroy_view = &call_destroy_view;
    get_wm()->callback_view_event = &call_view_event;
}

PyObject** _pywm_callbacks_get(const char* name){
    if(!strcmp(name, "motion")){
        return &callbacks.motion;
    }else if(!strcmp(name, "button")){
        return &callbacks.button;
    }else if(!strcmp(name, "axis")){
        return &callbacks.axis;
    }else if(!strcmp(name, "key")){
        return &callbacks.key;
    }else if(!strcmp(name, "modifiers")){
        return &callbacks.modifiers;
    }else if(!strcmp(name, "gesture")){
        return &callbacks.gesture;
    }else if(!strcmp(name, "layout_change")){
        return &callbacks.layout_change;
    }else if(!strcmp(name, "ready")){
        return &callbacks.ready;
    }else if(!strcmp(name, "update_view")){
        return &callbacks.update_view;
    }else if(!strcmp(name, "destroy_view")){
        return &callbacks.destroy_view;
    }else if(!strcmp(name, "query_new_widget")){
        return &callbacks.query_new_widget;
    }else if(!strcmp(name, "update_widget")){
        return &callbacks.update_widget;
    }else if(!strcmp(name, "query_destroy_widget")){
        return &callbacks.query_destroy_widget;
    }else if(!strcmp(name, "update")){
        return &callbacks.update;
    }else if(!strcmp(name, "view_event")){
        return &callbacks.view_event;
    }

    return NULL;
}

struct _pywm_callbacks* _pywm_callbacks_get_all(){
    return &callbacks;
}
