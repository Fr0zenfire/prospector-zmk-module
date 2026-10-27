#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define CENTER_W 140
#define CENTER_H 126

struct zmk_widget_track_center {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *wpm_label;
    lv_obj_t *pill;
    lv_obj_t *layer_label;
};

int zmk_widget_track_center_init(struct zmk_widget_track_center *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_track_center_obj(struct zmk_widget_track_center *widget);
