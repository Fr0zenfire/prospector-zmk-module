#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zmk/ble.h>

#define SIDE_W 70
#define SIDE_H 126

#define TRK_MAX_PERIPHERALS 4

struct zmk_widget_track_battery {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *pct_labels[TRK_MAX_PERIPHERALS];
    lv_obj_t *fills[TRK_MAX_PERIPHERALS];
    lv_obj_t *frames[TRK_MAX_PERIPHERALS];
    lv_obj_t *odo_label;
};

struct zmk_widget_track_output {
    sys_snode_t node;
    lv_obj_t *obj;
    lv_obj_t *transport_label;
    lv_obj_t *profile_label;
    lv_obj_t *cells[ZMK_BLE_PROFILE_COUNT];
    lv_obj_t *link_icon;
    lv_obj_t *link_label;
};

int zmk_widget_track_battery_init(struct zmk_widget_track_battery *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_track_battery_obj(struct zmk_widget_track_battery *widget);

int zmk_widget_track_output_init(struct zmk_widget_track_output *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_track_output_obj(struct zmk_widget_track_output *widget);
