/*
 * "Track" status screen — inspired by the Toyota GR86 / Subaru BRZ digital
 * cluster in Track mode.
 *
 *   +--------------------------------------------------------------+
 *   |   WIN        ALT        CTRL        SHFT     <- telltales    |
 *   |                                         8  10 12             |
 *   |  0 2 4   6                    ____----||||||||  <- WPM tach  |
 *   |  ||||||||||||||||||||||||||||||||||||||||||||                |
 *   |  x10 WPM  ____________________________________               |
 *   |  L   87% |            |  BLE        P1                       |
 *   |  [=====] |     72 WPM |  [ ][#][ ][ ][ ]                     |
 *   |  R   92% |            |  PEAK      112                       |
 *   |  [=====] |            |  [------|---]                        |
 *   |  ------- \  [ BASE ] /   -------                             |
 *   |  ODO 2138 \________/   (BT) LINK                             |
 *   +--------------------------------------------------------------+
 *
 * Child order matters: display_idle.c animates screen children 0..4 as
 * top / left / right / bottom / bottom for the sleep & wake transitions.
 */

#include <lvgl.h>
#include <stdio.h>
#include <zephyr/kernel.h>

#include "telltales.h"
#include "tach.h"
#include "center.h"
#include "side_panels.h"
#include "track_common.h"

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

#if IS_ENABLED(CONFIG_PROSPECTOR_SHOW_MODIFIERS)
static struct zmk_widget_track_telltales telltales_widget;
#endif
static struct zmk_widget_track_tach tach_widget;
static struct zmk_widget_track_center center_widget;
static struct zmk_widget_track_battery battery_widget;
static struct zmk_widget_track_output output_widget;

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
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 0: telltales (top with padding) */
#if IS_ENABLED(CONFIG_PROSPECTOR_SHOW_MODIFIERS)
    zmk_widget_track_telltales_init(&telltales_widget, screen);
    lv_obj_t *top_obj = zmk_widget_track_telltales_obj(&telltales_widget);
#else
    lv_obj_t *top_obj = trk_box(screen);
    lv_obj_set_size(top_obj, TELLTALE_W, TELLTALE_H);
#endif
    lv_obj_set_pos(top_obj, 0, 8);

    /* 1: WPM tachometer */
    zmk_widget_track_tach_init(&tach_widget, screen);
    lv_obj_t *tach_obj = zmk_widget_track_tach_obj(&tach_widget);
    lv_obj_set_pos(tach_obj, 0, 28);

    /* 2: centre bowl (WPM readout + layer pill) */
    zmk_widget_track_center_init(&center_widget, screen);
    lv_obj_t *center_obj = zmk_widget_track_center_obj(&center_widget);
    lv_obj_set_pos(center_obj, (TRK_SCREEN_W - CENTER_W) / 2, TRK_SCREEN_H - CENTER_H);

    /* 3: left panel (batteries + odometer) */
    zmk_widget_track_battery_init(&battery_widget, screen);
    lv_obj_t *bat_obj = zmk_widget_track_battery_obj(&battery_widget);
    lv_obj_set_pos(bat_obj, 0, TRK_SCREEN_H - SIDE_H);

    /* 4: right panel (output + peak + link) */
    zmk_widget_track_output_init(&output_widget, screen);
    lv_obj_t *out_obj = zmk_widget_track_output_obj(&output_widget);
    lv_obj_set_pos(out_obj, TRK_SCREEN_W - SIDE_W, TRK_SCREEN_H - SIDE_H);

#if IS_ENABLED(CONFIG_PROSPECTOR_FPS_MONITOR)
    fps_label = lv_label_create(screen);
    lv_label_set_text(fps_label, "-- FPS");
    lv_obj_set_style_text_font(fps_label, &DINishCondensed_SemiBold_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0x00FF90), LV_PART_MAIN);
    lv_obj_set_pos(fps_label, 218, 0);
    lv_timer_create(fps_timer_cb, 120, NULL);
#endif

    start_boot_slide(top_obj, 0, -40, 0);
    start_boot_slide(tach_obj, -140, 0, 32);
    start_boot_slide(center_obj, 0, 120, 64);
    start_boot_slide(bat_obj, -90, 0, 96);
    start_boot_slide(out_obj, 90, 0, 128);

    return screen;
}
