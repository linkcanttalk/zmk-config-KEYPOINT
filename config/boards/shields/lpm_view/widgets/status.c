/*
 *
 * Copyright (c) 2023 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 */

#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include "status.h"
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/usb.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/wpm.h>

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
};

struct layer_status_state {
    uint8_t index;
    const char *label;
};

struct wpm_status_state {
    uint8_t wpm;
};

static void draw_top(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_label_dsc_t label_dsc_wpm;
    init_label_dsc(&label_dsc_wpm, LVGL_FOREGROUND, &lv_font_unscii_8, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);
    lv_draw_line_dsc_t line_dsc;
    init_line_dsc(&line_dsc, LVGL_FOREGROUND, 2);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw battery
    draw_battery(canvas, state);

    // Draw output status
    char output_text[10] = {};

    switch (state->selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        strcat(output_text, LV_SYMBOL_USB);
        break;
    case ZMK_TRANSPORT_BLE:
        if (state->active_profile_bonded) {
            if (state->active_profile_connected) {
                strcat(output_text, LV_SYMBOL_WIFI);
            } else {
                strcat(output_text, LV_SYMBOL_CLOSE);
            }
        } else {
            strcat(output_text, LV_SYMBOL_SETTINGS);
        }
        break;
    }

    lv_canvas_draw_text(canvas, 0, 0, CANVAS_SIZE, &label_dsc, output_text);

    // Draw WPM
    lv_canvas_draw_rect(canvas, 0, 21, 70, 44, &rect_white_dsc);
    lv_canvas_draw_rect(canvas, 1, 22, 66, 42, &rect_black_dsc);

    char wpm_text[6] = {};
    snprintf(wpm_text, sizeof(wpm_text), "%d", state->wpm[9]);
    lv_canvas_draw_text(canvas, 42, 52, 24, &label_dsc_wpm, wpm_text);

    int max = 0;
    int min = 256;

    for (int i = 0; i < 10; i++) {
        if (state->wpm[i] > max) {
            max = state->wpm[i];
        }
        if (state->wpm[i] < min) {
            min = state->wpm[i];
        }
    }

    int range = max - min;
    if (range == 0) {
        range = 1;
    }

    lv_point_t points[10];
    for (int i = 0; i < 10; i++) {
        points[i].x = 2 + i * 7;
        points[i].y = 62 - (int)(((float)(state->wpm[i] - min) * 38.0f) / range + 0.5f);
    }
    lv_canvas_draw_line(canvas, points, 10, &line_dsc);

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void draw_dashed_rect(lv_obj_t *canvas, int x, int y, int w, int h,
                              int border_w, int dash, int gap) {
    lv_draw_line_dsc_t line_dsc;
    init_line_dsc(&line_dsc, LVGL_FOREGROUND, border_w);
    int pat = dash + gap;

    for (int p = 0; p < w; p += pat) {
        int end = p + dash < w ? p + dash : w;
        lv_point_t pts[2] = {{x + p, y}, {x + end, y}};
        lv_canvas_draw_line(canvas, pts, 2, &line_dsc);
    }
    for (int p = 0; p < w; p += pat) {
        int end = p + dash < w ? p + dash : w;
        lv_point_t pts[2] = {{x + p, y + h - 1}, {x + end, y + h - 1}};
        lv_canvas_draw_line(canvas, pts, 2, &line_dsc);
    }
    for (int p = 0; p < h; p += pat) {
        int end = p + dash < h ? p + dash : h;
        lv_point_t pts[2] = {{x, y + p}, {x, y + end}};
        lv_canvas_draw_line(canvas, pts, 2, &line_dsc);
    }
    for (int p = 0; p < h; p += pat) {
        int end = p + dash < h ? p + dash : h;
        lv_point_t pts[2] = {{x + w - 1, y + p}, {x + w - 1, y + end}};
        lv_canvas_draw_line(canvas, pts, 2, &line_dsc);
    }
}

static void draw_rounded_icon(lv_obj_t *canvas, int x, int y, int w, int h,
                               const char *icon, bool invert, const lv_font_t *font,
                               int x_offset, int y_offset) {
    int font_h = font->line_height;
    int icon_radius = 6;
    int expand = 2;

    if (invert) {
        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_color = LVGL_FOREGROUND;
        rect_dsc.bg_opa = LV_OPA_COVER;
        rect_dsc.radius = icon_radius;
        lv_canvas_draw_rect(canvas, x - expand, y - expand, w + expand * 2, h + expand * 2, &rect_dsc);

        lv_draw_label_dsc_t label_dsc;
        init_label_dsc(&label_dsc, LVGL_BACKGROUND, font, LV_TEXT_ALIGN_CENTER);
        lv_canvas_draw_text(canvas, x - expand + x_offset, y + (h - font_h) / 2 + y_offset, w + expand * 2, &label_dsc, icon);
    } else {
        draw_dashed_rect(canvas, x, y, w, h, 2, 3, 4);

        lv_draw_label_dsc_t label_dsc;
        init_label_dsc(&label_dsc, LVGL_FOREGROUND, font, LV_TEXT_ALIGN_CENTER);
        lv_canvas_draw_text(canvas, x + x_offset, y + (h - font_h) / 2 + y_offset, w, &label_dsc, icon);
    }
}

static void draw_traffic_light(lv_obj_t *canvas, int x, int y, int width, int height,
                               enum vibe_coding_state vibe_state, bool timeout) {
    int padding = 4;
    int gap = 4;
    int size = (width - padding * 2 - gap * 2) / 3;
    int iy = y + (height - size) / 2;

    int ix_left = x + padding;
    int ix_right = x + width - padding - size;
    int ix_mid = x + (width - size) / 2;

    const char *icons[] = {"x", "!", ">"};
    const lv_font_t *fonts[] = {&lv_font_montserrat_18, &lv_font_montserrat_14, &lv_font_montserrat_22};
    const int x_offsets[] = {0, 0, 1};
    const int y_offsets[] = {-2, 0, -1};
    bool inversions[] = {false, false, false};

    if (timeout) {
        inversions[0] = inversions[1] = inversions[2] = true;
    } else if (vibe_state == VIBE_CODING_RUNNING) {
        inversions[2] = true;
    } else if (vibe_state == VIBE_CODING_WARNING) {
        inversions[1] = true;
    } else if (vibe_state == VIBE_CODING_CRITICAL) {
        inversions[0] = true;
    }

    int ix_pos[3] = {ix_left, ix_mid, ix_right};
    for (int i = 0; i < 3; i++) {
        draw_rounded_icon(canvas, ix_pos[i], iy, size, size, icons[i], inversions[i], fonts[i], x_offsets[i], y_offsets[i]);
    }
}

static void draw_middle(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 1);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_dsc_black;
    init_label_dsc(&label_dsc_black, LVGL_BACKGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    int box_w = 18;
    int normal_h = 22;
    int selected_h = 24;
    int y_center = 46;
    int text_margin_bottom = 3;
    int font_height = 18;

    for (int i = 0; i < 4; i++) {
        bool selected = i == state->active_profile_index;
        int h = selected ? selected_h : normal_h;
        int x = i * box_w;
        int y = y_center - h / 2;

        lv_draw_rect_dsc_t rect_dsc;
        lv_draw_rect_dsc_init(&rect_dsc);

        if (selected) {
            rect_dsc.bg_color = LVGL_FOREGROUND;
            rect_dsc.bg_opa = LV_OPA_COVER;
            rect_dsc.radius = 4;
            lv_canvas_draw_rect(canvas, x, y, box_w, h, &rect_dsc);
        } else {
            rect_dsc.bg_opa = LV_OPA_TRANSP;
            rect_dsc.border_color = LVGL_FOREGROUND;
            rect_dsc.border_width = 2;
            rect_dsc.radius = 3;
            if (i == 0) {
                rect_dsc.border_side = LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM;
            } else if (i == 3) {
                rect_dsc.border_side = LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM;
            } else {
                rect_dsc.border_side = LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM;
            }
            lv_canvas_draw_rect(canvas, x, y, box_w, h, &rect_dsc);
        }

        char label[2];
        snprintf(label, sizeof(label), "%d", i + 1);
        int text_y = y + h - text_margin_bottom - font_height;
        lv_canvas_draw_text(canvas, x, text_y, box_w,
                            (selected ? &label_dsc_black : &label_dsc), label);
    }

    int tl_width = 72;
    int tl_height = 30;
    int tl_x = 0;
    int tl_y = 2;
    draw_traffic_light(canvas, tl_x, tl_y, tl_width, tl_height, state->vibe_state,
                       state->vibe_timeout);

    rotate_canvas(canvas, cbuf);
}

static void draw_bottom(lv_obj_t *widget, lv_color_t cbuf[], const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 2);

    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    // Fill background
    lv_canvas_draw_rect(canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);

    // Draw layer
    if (state->layer_label == NULL) {
        char text[10] = {};

        sprintf(text, "LAYER %i", state->layer_index);

        lv_canvas_draw_text(canvas, 0, 0, 72, &label_dsc, text);
    } else {
        lv_canvas_draw_text(canvas, 0, 0, 72, &label_dsc, state->layer_label);
    }

    // Rotate canvas
    rotate_canvas(canvas, cbuf);
}

static void set_battery_status(struct zmk_widget_status *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

static void set_output_status(struct zmk_widget_status *widget,
                              const struct output_status_state *state) {
    widget->state.selected_endpoint = state->selected_endpoint;
    widget->state.active_profile_index = state->active_profile_index;
    widget->state.active_profile_connected = state->active_profile_connected;
    widget->state.active_profile_bonded = state->active_profile_bonded;
    widget->state.vibe_timeout = false;

    draw_top(widget->obj, widget->cbuf, &widget->state);
    draw_middle(widget->obj, widget->cbuf2, &widget->state);
}

static void output_status_update_cb(struct output_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_output_status(widget, &state); }
}

static struct output_status_state output_status_get_state(const zmk_event_t *_eh) {
    return (struct output_status_state){
        .selected_endpoint = zmk_endpoints_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

static void set_layer_status(struct zmk_widget_status *widget, struct layer_status_state state) {
    widget->state.layer_index = state.index;
    widget->state.layer_label = state.label;

    draw_bottom(widget->obj, widget->cbuf3, &widget->state);
}

static void layer_status_update_cb(struct layer_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_layer_status(widget, state); }
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh) {
    uint8_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){.index = index, .label = zmk_keymap_layer_name(index)};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(widget_layer_status, zmk_layer_state_changed);

static void set_wpm_status(struct zmk_widget_status *widget, struct wpm_status_state state) {
    for (int i = 0; i < 9; i++) {
        widget->state.wpm[i] = widget->state.wpm[i + 1];
    }
    widget->state.wpm[9] = state.wpm;

    draw_top(widget->obj, widget->cbuf, &widget->state);
}

static void wpm_status_update_cb(struct wpm_status_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_wpm_status(widget, state); }
}

struct wpm_status_state wpm_status_get_state(const zmk_event_t *eh) {
    return (struct wpm_status_state){.wpm = zmk_wpm_get_state()};
};

ZMK_DISPLAY_WIDGET_LISTENER(widget_wpm_status, struct wpm_status_state, wpm_status_update_cb,
                             wpm_status_get_state)
ZMK_SUBSCRIPTION(widget_wpm_status, zmk_wpm_state_changed);

static void vibe_coding_state_changed(enum vibe_coding_state state) {
    struct zmk_widget_status *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        widget->state.vibe_state = state;
        widget->state.vibe_timeout = vibe_coding_service_peek_timeout();
        draw_middle(widget->obj, widget->cbuf2, &widget->state);
    }
}

int zmk_widget_status_init(struct zmk_widget_status *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, 144, 72);

    // top battery status and output, wpm status
    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    // middle connecion status
    lv_obj_t *middle = lv_canvas_create(widget->obj);
    lv_obj_align(middle, LV_ALIGN_TOP_LEFT, 68, 0);
    lv_canvas_set_buffer(middle, widget->cbuf2, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);
    // bottom layer status
    lv_obj_t *bottom = lv_canvas_create(widget->obj);
    lv_obj_align(bottom, LV_ALIGN_TOP_LEFT, 128, 0);
    lv_canvas_set_buffer(bottom, widget->cbuf3, CANVAS_SIZE, CANVAS_SIZE, LV_IMG_CF_TRUE_COLOR);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_output_status_init();
    widget_layer_status_init();
    widget_wpm_status_init();
    vibe_coding_service_init(vibe_coding_state_changed);

    return 0;
}

lv_obj_t *zmk_widget_status_obj(struct zmk_widget_status *widget) { return widget->obj; }