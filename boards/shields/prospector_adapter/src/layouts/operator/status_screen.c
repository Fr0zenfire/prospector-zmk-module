#include <lvgl.h>

#include "modifier_indicator.h"
#include "wpm_meter.h"
#include "layer_display.h"
#include "battery_circles.h"
#include "output.h"

#include <fonts.h>

static struct zmk_widget_modifier_indicator modifier_indicator_widget;
static struct zmk_widget_wpm_meter wpm_meter_widget;
static struct zmk_widget_layer_display layer_display_widget;
static struct zmk_widget_battery_circles battery_circles_widget;
static struct zmk_widget_output output_widget;

static void boot_anim_tx(void *obj, int32_t v) {
    lv_obj_set_style_translate_x((lv_obj_t *)obj, (lv_coord_t)v, LV_PART_MAIN);
}

static void boot_anim_ty(void *obj, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)obj, (lv_coord_t)v, LV_PART_MAIN);
}

static void start_boot_slide(lv_obj_t *obj, int32_t off_x, int32_t off_y, uint32_t delay_ms) {
    lv_obj_set_style_translate_x(obj, (lv_coord_t)off_x, LV_PART_MAIN);
    lv_obj_set_style_translate_y(obj, (lv_coord_t)off_y, LV_PART_MAIN);

    if (off_x != 0) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, obj);
        lv_anim_set_values(&a, off_x, 0);
        lv_anim_set_duration(&a, 340);
        lv_anim_set_delay(&a, delay_ms);
        lv_anim_set_exec_cb(&a, boot_anim_tx);
        lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
        lv_anim_start(&a);
    }

    if (off_y != 0) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, obj);
        lv_anim_set_values(&a, off_y, 0);
        lv_anim_set_duration(&a, 340);
        lv_anim_set_delay(&a, delay_ms);
        lv_anim_set_exec_cb(&a, boot_anim_ty);
        lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
        lv_anim_start(&a);
    }
}

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    zmk_widget_modifier_indicator_init(&modifier_indicator_widget, screen);
    lv_obj_t *mod_obj = zmk_widget_modifier_indicator_obj(&modifier_indicator_widget);
    lv_obj_set_pos(mod_obj, 25, 8);

    zmk_widget_wpm_meter_init(&wpm_meter_widget, screen);
    lv_obj_t *wpm_obj = zmk_widget_wpm_meter_obj(&wpm_meter_widget);
    lv_obj_set_pos(wpm_obj, 10, 42);

    zmk_widget_layer_display_init(&layer_display_widget, screen);
    lv_obj_t *layer_obj = zmk_widget_layer_display_obj(&layer_display_widget);
    lv_obj_set_pos(layer_obj, 10, 142);

    zmk_widget_battery_circles_init(&battery_circles_widget, screen);
    lv_obj_t *bat_obj = zmk_widget_battery_circles_obj(&battery_circles_widget);
    lv_obj_set_pos(bat_obj, 11, 170);

    zmk_widget_output_init(&output_widget, screen);
    lv_obj_t *out_obj = zmk_widget_output_obj(&output_widget);
    lv_obj_set_pos(out_obj, 148, 170);

    start_boot_slide(mod_obj, 0, -40, 0);
    start_boot_slide(wpm_obj, -140, 0, 32);
    start_boot_slide(layer_obj, 140, 0, 64);
    start_boot_slide(bat_obj, 0, 75, 96);
    start_boot_slide(out_obj, 0, 75, 128);

    return screen;
}
