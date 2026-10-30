#pragma once

#include <lvgl.h>

/* Fonts used by the Track layout (all font sources are always compiled). */
LV_FONT_DECLARE(DINish_Medium_24);            /* digits + '-' only: tach scale */
LV_FONT_DECLARE(DINishCondensed_SemiBold_20); /* full ASCII: labels           */
LV_FONT_DECLARE(FR_Regular_48);               /* full ASCII: big WPM readout  */

/* Screen geometry (landscape) */
#define TRK_SCREEN_W 280
#define TRK_SCREEN_H 240

/* Tachometer scale */
#define TRK_TACH_MAX_WPM   130
#define TRK_TACH_AMBER_WPM 100
#define TRK_TACH_RED_WPM   110

/* Standard LVGL transparent/borderless container matching Operator. */
static inline lv_obj_t *trk_box(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj) {
        return NULL;
    }
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static inline lv_obj_t *trk_fill(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *obj = trk_box(parent);
    if (!obj) {
        return NULL;
    }
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    return obj;
}

static inline lv_obj_t *trk_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                                  int x, int y, int w, lv_text_align_t align, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    if (!label) {
        return NULL;
    }
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_obj_set_pos(label, x, y);
    if (w > 0) {
        lv_obj_set_width(label, w);
    }
    lv_label_set_text(label, text);
    return label;
}

/* GR86-style gauge frame: 4-sided 1px border. */
static inline lv_obj_t *trk_gauge_frame(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *frame = trk_box(parent);
    if (!frame) {
        return NULL;
    }
    lv_obj_set_pos(frame, x, y);
    lv_obj_set_size(frame, w, h);
    lv_obj_set_style_border_width(frame, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(frame, lv_color_hex(0xD0D0D0), LV_PART_MAIN);
    return frame;
}

/* Cross-widget hooks (all called from the LVGL / display thread). */
void track_center_set_wpm(int wpm);
