/*
 * GR86 Track-mode style tachometer, driven by WPM.
 *
 * - 26 vertical segment objects whose height ramps up left -> right (wedge shape).
 * - Non-linear scale like the car: 0-40 WPM compressed into first few segments,
 *   40-130 WPM spreads across the rest.
 * - Amber zone from 100 WPM, red line from 110 WPM.
 * - Smooth animation via Zephyr delayable workqueue (safe from LVGL draw task issues).
 */

#include "tach.h"

#include <zephyr/kernel.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/wpm.h>

#include "display_colors.h"
#include "track_common.h"

#define BAR_COUNT   TRK_BAR_COUNT
#define BAR_W       7
#define BAR_GAP     2
#define BAR_PITCH   (BAR_W + BAR_GAP)
#define BAR_SPAN    (BAR_COUNT * BAR_PITCH - BAR_GAP)
#define BAR_X0      ((TACH_W - BAR_SPAN) / 2)
#define BAR_BOTTOM  84
#define BAR_H_MIN   16
#define BAR_H_MAX   52
#define RAMP_GAP    2

#define LOW_BARS    4   /* segments used by compressed 0..LOW_WPM */
#define LOW_WPM     40

enum zone { ZONE_NORMAL = 0, ZONE_AMBER, ZONE_RED };

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static struct k_work_delayable wpm_work;

static uint8_t bar_zone[BAR_COUNT];
static int amber_first = BAR_COUNT;
static int red_first = BAR_COUNT;

static float displayed_wpm = 0.0f;
static float target_wpm = 0.0f;
static int active_bars = 0;
static int peak_bars = 0;
static int peak_hold = 0;
static int peak_decay = 0;
static int last_readout = -1;

static const float smoothing_up = 0.18f;
static const float smoothing_down = 0.04f;

static inline int bar_x(int i) { return BAR_X0 + i * BAR_PITCH; }

static inline int bar_h(int i) {
    return BAR_H_MIN + ((BAR_H_MAX - BAR_H_MIN) * i) / (BAR_COUNT - 1);
}

static float wpm_to_pos(float wpm) {
    if (wpm <= 0.0f) {
        return 0.0f;
    }
    if (wpm <= LOW_WPM) {
        return wpm * LOW_BARS / LOW_WPM;
    }
    float pos = LOW_BARS + (wpm - LOW_WPM) * (BAR_COUNT - LOW_BARS) /
                               (float)(TRK_TACH_MAX_WPM - LOW_WPM);
    return pos > BAR_COUNT ? BAR_COUNT : pos;
}

static float bar_start_wpm(int i) {
    if (i < LOW_BARS) {
        return (float)i * LOW_WPM / LOW_BARS;
    }
    return LOW_WPM + (float)(i - LOW_BARS) * (TRK_TACH_MAX_WPM - LOW_WPM) /
                         (BAR_COUNT - LOW_BARS);
}

static int ramp_y(int x) {
    int x0 = bar_x(0);
    int x1 = bar_x(BAR_COUNT - 1) + BAR_W;
    int y0 = BAR_BOTTOM - bar_h(0) - RAMP_GAP;
    int y1 = BAR_BOTTOM - bar_h(BAR_COUNT - 1) - RAMP_GAP;
    if (x <= x0) return y0;
    if (x >= x1) return y1;
    return y0 + ((y1 - y0) * (x - x0)) / (x1 - x0);
}

static uint32_t zone_color(int zone, bool on) {
    switch (zone) {
    case ZONE_RED:
        return on ? TRK_COLOR_RED_ON : TRK_COLOR_RED_OFF;
    case ZONE_AMBER:
        return on ? TRK_COLOR_AMBER_ON : TRK_COLOR_AMBER_OFF;
    default:
        return on ? TRK_COLOR_BAR_ON : TRK_COLOR_BAR_OFF;
    }
}

static void tach_render(int new_active, int new_peak) {
    struct zmk_widget_track_tach *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, w, node) {
        if (new_active != active_bars) {
            int lo = (new_active < active_bars) ? new_active : active_bars;
            int hi = (new_active > active_bars) ? new_active : active_bars;
            for (int i = lo; i < hi; i++) {
                if (i < 0 || i >= BAR_COUNT || !w->bars[i]) {
                    continue;
                }
                bool on = (i < new_active);
                lv_obj_set_style_bg_color(w->bars[i], lv_color_hex(zone_color(bar_zone[i], on)),
                                          LV_PART_MAIN);
            }
        }

        if (w->peak_bar) {
            if (new_peak > new_active && new_peak > 0) {
                int slot = new_peak - 1;
                if (slot >= BAR_COUNT) slot = BAR_COUNT - 1;
                lv_obj_set_pos(w->peak_bar, bar_x(slot), BAR_BOTTOM - bar_h(slot));
                lv_obj_set_size(w->peak_bar, BAR_W, bar_h(slot));
                lv_obj_set_style_bg_color(w->peak_bar, lv_color_hex(zone_color(bar_zone[slot], true)),
                                          LV_PART_MAIN);
                lv_obj_clear_flag(w->peak_bar, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(w->peak_bar, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

static void wpm_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    float diff = target_wpm - displayed_wpm;
    bool at_target = (diff > -0.3f && diff < 0.3f);

    if (at_target) {
        displayed_wpm = target_wpm;
    } else {
        float factor = (diff > 0.0f) ? smoothing_up : smoothing_down;
        displayed_wpm += diff * factor;
    }

    int new_active = (int)(wpm_to_pos(displayed_wpm) + 0.5f);
    if (new_active > BAR_COUNT) new_active = BAR_COUNT;

    if (new_active > peak_bars) {
        peak_bars = new_active;
        peak_hold = 0;
        peak_decay = 0;
    } else if (peak_bars > new_active) {
        if (peak_hold < 180) {
            peak_hold++;
        } else if (++peak_decay >= 24) {
            peak_bars--;
            peak_decay = 0;
        }
    }

    tach_render(new_active, peak_bars);
    active_bars = new_active;

    int readout = (int)(displayed_wpm + 0.5f);
    if (readout != last_readout) {
        last_readout = readout;
        track_center_set_wpm(readout);
    }

    if (!at_target || peak_bars > active_bars) {
        k_work_schedule(&wpm_work, K_MSEC(16));
    }
}

struct tach_state {
    uint8_t wpm;
};

static void tach_update_cb(struct tach_state state) {
    target_wpm = (float)state.wpm;
    k_work_schedule(&wpm_work, K_NO_WAIT);
}

static struct tach_state tach_get_state(const zmk_event_t *eh) {
    return (struct tach_state){.wpm = zmk_wpm_get_state()};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_track_tach, struct tach_state, tach_update_cb, tach_get_state)
ZMK_SUBSCRIPTION(widget_track_tach, zmk_wpm_state_changed);

static void add_scale_numeral(lv_obj_t *parent, int wpm, const char *text) {
    int cx = bar_x(0) + (int)(wpm_to_pos((float)wpm) * BAR_PITCH);
    int line_h = lv_font_get_line_height(&DINish_Medium_24);
    int y = ramp_y(cx) - 6 - line_h;
    trk_label(parent, &DINish_Medium_24, TRK_COLOR_TEXT, cx - 15, y, 30, LV_TEXT_ALIGN_CENTER,
              text);
}

int zmk_widget_track_tach_init(struct zmk_widget_track_tach *widget, lv_obj_t *parent) {
    amber_first = BAR_COUNT;
    red_first = BAR_COUNT;

    for (int i = 0; i < BAR_COUNT; i++) {
        float w = bar_start_wpm(i);
        if (w >= TRK_TACH_RED_WPM) {
            bar_zone[i] = ZONE_RED;
            if (red_first == BAR_COUNT) red_first = i;
        } else if (w >= TRK_TACH_AMBER_WPM) {
            bar_zone[i] = ZONE_AMBER;
            if (amber_first == BAR_COUNT) amber_first = i;
        } else {
            bar_zone[i] = ZONE_NORMAL;
        }
    }
    if (amber_first > red_first) amber_first = red_first;

    widget->obj = trk_box(parent);
    lv_obj_set_size(widget->obj, TACH_W, TACH_H);

    /* 26 Segment bar objects */
    for (int i = 0; i < BAR_COUNT; i++) {
        widget->bars[i] = trk_fill(widget->obj, bar_x(i), BAR_BOTTOM - bar_h(i), BAR_W, bar_h(i),
                                   zone_color(bar_zone[i], false));
    }

    /* Peak bar overlay */
    widget->peak_bar = trk_fill(widget->obj, bar_x(0), BAR_BOTTOM - bar_h(0), BAR_W, bar_h(0),
                                TRK_COLOR_BAR_ON);
    if (widget->peak_bar) {
        lv_obj_add_flag(widget->peak_bar, LV_OBJ_FLAG_HIDDEN);
    }

    /* Sloped diagonal ramp line below the numbers (1 segment per bar, continuous across gaps) */
    for (int i = 0; i < BAR_COUNT; i++) {
        uint32_t rcolor = (bar_zone[i] == ZONE_RED) ? TRK_COLOR_RED_ON
                         : (bar_zone[i] == ZONE_AMBER) ? TRK_COLOR_AMBER_ON
                         : TRK_COLOR_RAMP;
        int seg_w = (i < BAR_COUNT - 1) ? BAR_PITCH : BAR_W;
        trk_fill(widget->obj, bar_x(i), BAR_BOTTOM - bar_h(i) - RAMP_GAP, seg_w, 2, rcolor);
    }

    /* Baseline under bars: normal, amber, red */
    int x_start = bar_x(0);
    int x_amber = bar_x(amber_first) - 1;
    int x_red = bar_x(red_first) - 1;
    int x_end = bar_x(BAR_COUNT - 1) + BAR_W;
    int base_y = BAR_BOTTOM;
    if (x_amber > x_end) x_amber = x_end;
    if (x_red > x_end) x_red = x_end;

    if (x_amber > x_start) {
        trk_fill(widget->obj, x_start, base_y, x_amber - x_start, 2, TRK_COLOR_RAMP);
    }
    if (x_red > x_amber) {
        trk_fill(widget->obj, x_amber, base_y, x_red - x_amber, 2, TRK_COLOR_AMBER_ON);
    }
    if (x_end > x_red) {
        trk_fill(widget->obj, x_red, base_y, x_end - x_red, 2, TRK_COLOR_RED_ON);
    }

    /* Numerals */
    add_scale_numeral(widget->obj, 0, "0");
    add_scale_numeral(widget->obj, 20, "2");
    add_scale_numeral(widget->obj, 40, "4");
    add_scale_numeral(widget->obj, 60, "6");
    add_scale_numeral(widget->obj, 80, "8");
    add_scale_numeral(widget->obj, 100, "10");
    add_scale_numeral(widget->obj, 120, "12");

    sys_slist_append(&widgets, &widget->node);
    k_work_init_delayable(&wpm_work, wpm_work_handler);
    widget_track_tach_init();

    return 0;
}

lv_obj_t *zmk_widget_track_tach_obj(struct zmk_widget_track_tach *widget) {
    return widget->obj;
}
