#include <lvgl.h>
#include <stdio.h>
#include <zephyr/kernel.h>

#include "modifier_indicator.h"
#include "wpm_meter.h"
#include "layer_display.h"
#include "battery_circles.h"
#include "output.h"

#include <fonts.h>

#if IS_ENABLED(CONFIG_PROSPECTOR_FPS_MONITOR)
extern volatile uint32_t prospector_instant_fps;
extern volatile int64_t prospector_last_frame_time_ms;

static lv_obj_t *fps_label;

static void fps_timer_cb(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    if (!fps_label) return;

    int64_t now = k_uptime_get();
    if (now - prospector_last_frame_time_ms > 350) {
        lv_label_set_text(fps_label, "-- FPS");
    } else {
        uint32_t fps = prospector_instant_fps;
        if (fps == 0) fps = 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "%u FPS", (unsigned int)fps);
        lv_label_set_text(fps_label, buf);
    }
}
#endif

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
        lv_anim_path_cb_t path = lv_anim_path_overshoot;
        lv_anim_set_path_cb(&a, path);
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
        lv_anim_path_cb_t path = lv_anim_path_overshoot;
        lv_anim_set_path_cb(&a, path);
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

#if IS_ENABLED(CONFIG_PROSPECTOR_FPS_MONITOR)
    /* Diagnostic FPS counter */
    fps_label = lv_label_create(screen);
    lv_label_set_text(fps_label, "-- FPS");
    lv_obj_set_style_text_font(fps_label, &FG_Medium_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0x00FF90), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(fps_label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_pos(fps_label, 195, 218);

    lv_timer_create(fps_timer_cb, 120, NULL);
#endif

    start_boot_slide(mod_obj, 0, -40, 0);
    start_boot_slide(wpm_obj, -140, 0, 32);
    start_boot_slide(layer_obj, 140, 0, 64);
    start_boot_slide(bat_obj, 0, 75, 96);
    start_boot_slide(out_obj, 0, 75, 128);

    return screen;
}
