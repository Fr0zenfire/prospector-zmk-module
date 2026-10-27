#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

#define TACH_W 280
#define TACH_H 112

#define TRK_BAR_COUNT 38

struct zmk_widget_track_tach {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *bars[TRK_BAR_COUNT];
    lv_obj_t *peak_bar;
};

int zmk_widget_track_tach_init(struct zmk_widget_track_tach *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_track_tach_obj(struct zmk_widget_track_tach *widget);
