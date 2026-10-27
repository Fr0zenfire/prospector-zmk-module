#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define TELLTALE_W 280
#define TELLTALE_H 24

struct zmk_widget_track_telltales {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *labels[4];
};

int zmk_widget_track_telltales_init(struct zmk_widget_track_telltales *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_track_telltales_obj(struct zmk_widget_track_telltales *widget);
