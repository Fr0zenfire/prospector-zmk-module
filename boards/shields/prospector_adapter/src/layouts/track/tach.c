/*
 * GR86 Track-mode style tachometer, driven by WPM.
 *
 * - 38 vertical segments whose height ramps up left -> right (wedge shape).
 * - Non-linear scale like the car: 0-40 WPM is compressed into the first
 *   few segments, 40-130 WPM spreads across the rest.
 * - Amber zone from 100 WPM, red line from 110 WPM.
 * - Scale numerals are x10 WPM ("x10 WPM" caption, like "x1000RPM").
 * - Peak-hold segment that decays after a few seconds.
 *
 * Everything except the numerals is drawn in a single LV_EVENT_DRAW_MAIN
 * callback, and only the segments that change are invalidated, keeping SPI
 * traffic small enough for smooth 60 FPS animation.
 */

#include "tach.h"

#include <zephyr/kernel.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/wpm.h>

#include "display_colors.h"
#include "track_common.h"

#define BAR_COUNT   38
#define BAR_W       5
#define BAR_GAP     2
#define BAR_PITCH   (BAR_W + BAR_GAP)
#define BAR_SPAN    (BAR_COUNT * BAR_PITCH - BAR_GAP)
#define BAR_X0      ((TACH_W - BAR_SPAN) / 2)
#define BAR_BOTTOM  84
#define BAR_H_MIN   16
#define BAR_H_MAX   52
#define RAMP_GAP    4

#define LOW_BARS    6   /* segments used by the compressed 0..LOW_WPM range */
#define LOW_WPM     40

#define FRAME_MS    16

enum zone { ZONE_NORMAL = 0, ZONE_AMBER, ZONE_RED };

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

static uint8_t bar_zone[BAR_COUNT];
static int amber_first = BAR_COUNT;
static int red_first = BAR_COUNT;

static lv_timer_t *tach_timer;
static float displayed_wpm;
static float target_wpm;
static int active_bars = 1;
static int peak_bars = 0;
static int peak_hold = 0;
static int peak_decay = 0;
static int last_readout = -1;

static const float smoothing_up = 0.18f;
static const float smoothing_down = 0.04f;

/* ---- geometry helpers ---------------------------------------------------- */

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

/* y of the sloped "ramp" line above the segments, at local x */
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

/* ---- drawing ------------------------------------------------------------- */

static void draw_hline(lv_layer_t *layer, lv_draw_line_dsc_t *dsc, int ox, int oy,
                       int x1, int y1, int x2, int y2, uint32_t color) {
    if (x2 <= x1) {
        return;
    }
    dsc->color = lv_color_hex(color);
    dsc->p1.x = ox + x1;
    dsc->p1.y = oy + y1;
    dsc->p2.x = ox + x2;
    dsc->p2.y = oy + y2;
    lv_draw_line(layer, dsc);
}

static void tach_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const int ox = coords.x1;
    const int oy = coords.y1;

    /* Segments */
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_COVER;
    rect.radius = 0;
    rect.border_width = 0;

    int peak_slot = (peak_bars > active_bars) ? peak_bars - 1 : -1;

    for (int i = 0; i < BAR_COUNT; i++) {
        bool on = i < active_bars;
        rect.bg_color = lv_color_hex(zone_color(bar_zone[i], on));
        rect.bg_opa = LV_OPA_COVER;

        lv_area_t a = {
            .x1 = ox + bar_x(i),
            .y1 = oy + BAR_BOTTOM - bar_h(i),
            .x2 = ox + bar_x(i) + BAR_W - 1,
            .y2 = oy + BAR_BOTTOM - 1,
        };
        lv_draw_rect(layer, &rect, &a);

        if (i == peak_slot) {
            rect.bg_color = lv_color_hex(zone_color(bar_zone[i], true));
            rect.bg_opa = LV_OPA_60;
            lv_draw_rect(layer, &rect, &a);
        }
    }

    /* Sloped ramp line + baseline, white -> amber -> red */
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 2;
    line.round_start = 0;
    line.round_end = 0;
    line.opa = LV_OPA_COVER;

    int x_start = bar_x(0);
    int x_amber = bar_x(amber_first) - 1;
    int x_red = bar_x(red_first) - 1;
    int x_end = bar_x(BAR_COUNT - 1) + BAR_W;
    if (x_amber > x_end) x_amber = x_end;
    if (x_red > x_end) x_red = x_end;

    draw_hline(layer, &line, ox, oy, x_start, ramp_y(x_start), x_amber, ramp_y(x_amber),
               TRK_COLOR_RAMP);
    draw_hline(layer, &line, ox, oy, x_amber, ramp_y(x_amber), x_red, ramp_y(x_red),
               TRK_COLOR_AMBER_ON);
    draw_hline(layer, &line, ox, oy, x_red, ramp_y(x_red), x_end, ramp_y(x_end),
               TRK_COLOR_RED_ON);

    int base_y = BAR_BOTTOM + 2;
    draw_hline(layer, &line, ox, oy, x_start, base_y, x_amber, base_y, TRK_COLOR_RAMP);
    draw_hline(layer, &line, ox, oy, x_amber, base_y, x_red, base_y, TRK_COLOR_AMBER_ON);
    draw_hline(layer, &line, ox, oy, x_red, base_y, x_end, base_y, TRK_COLOR_RED_ON);
}

/* Invalidate only the segment columns in [lo, hi) */
static void invalidate_bars(lv_obj_t *obj, int lo, int hi) {
    if (lo < 0) lo = 0;
    if (hi > BAR_COUNT) hi = BAR_COUNT;
    if (hi <= lo) {
        return;
    }

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_area_t a = {
        .x1 = coords.x1 + bar_x(lo),
        .y1 = coords.y1 + BAR_BOTTOM - bar_h(hi - 1),
        .x2 = coords.x1 + bar_x(hi - 1) + BAR_W - 1,
        .y2 = coords.y1 + BAR_BOTTOM - 1,
    };
    lv_obj_invalidate_area(obj, &a);
}

/* ---- animation ----------------------------------------------------------- */

static void tach_timer_cb(lv_timer_t *timer) {
    float diff = target_wpm - displayed_wpm;
    bool at_target = (diff > -0.3f && diff < 0.3f);

    if (at_target) {
        displayed_wpm = target_wpm;
    } else {
        displayed_wpm += diff * ((diff > 0) ? smoothing_up : smoothing_down);
    }

    float pos = wpm_to_pos(displayed_wpm);
    int new_active = (int)(pos + 0.999f);
    if (new_active < 1) new_active = 1; /* idle segment, like the car at idle */
    if (new_active > BAR_COUNT) new_active = BAR_COUNT;

    int old_active = active_bars;
    int old_peak = peak_bars;

    if (new_active > peak_bars) {
        peak_bars = new_active;
        peak_hold = 0;
        peak_decay = 0;
    } else if (peak_bars > new_active) {
        if (peak_hold < 180) { /* ~3 s hold */
            peak_hold++;
        } else if (++peak_decay >= 24) {
            peak_bars--;
            peak_decay = 0;
        }
    }

    active_bars = new_active;

    struct zmk_widget_track_tach *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        if (old_active != active_bars) {
            int lo = MIN(old_active, active_bars);
            int hi = MAX(old_active, active_bars);
            invalidate_bars(widget->obj, lo, hi);
        }
        if (old_peak != peak_bars) {
            invalidate_bars(widget->obj, MIN(old_peak, peak_bars) - 1, MAX(old_peak, peak_bars));
        }
    }

    int readout = (int)(displayed_wpm + 0.5f);
    if (readout != last_readout) {
        last_readout = readout;
        track_center_set_wpm(readout);
    }

    if (at_target && peak_bars <= active_bars) {
        lv_timer_pause(timer);
    }
}

/* ---- ZMK event plumbing -------------------------------------------------- */

struct tach_state {
    uint8_t wpm;
};

static void tach_update_cb(struct tach_state state) {
    target_wpm = (float)state.wpm;

    if (tach_timer) {
        lv_timer_resume(tach_timer);
    }
}

static struct tach_state tach_get_state(const zmk_event_t *eh) {
    return (struct tach_state){.wpm = zmk_wpm_get_state()};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_track_tach, struct tach_state, tach_update_cb, tach_get_state)
ZMK_SUBSCRIPTION(widget_track_tach, zmk_wpm_state_changed);

/* ---- init ---------------------------------------------------------------- */

static void add_scale_numeral(lv_obj_t *parent, int wpm, const char *text) {
    int cx = bar_x(0) + (int)(wpm_to_pos((float)wpm) * BAR_PITCH);
    int line_h = lv_font_get_line_height(&DINish_Medium_24);
    int y = ramp_y(cx) - 2 - line_h;
    trk_label(parent, &DINish_Medium_24, TRK_COLOR_TEXT, cx - 15, y, 30, LV_TEXT_ALIGN_CENTER,
              text);
}

int zmk_widget_track_tach_init(struct zmk_widget_track_tach *widget, lv_obj_t *parent) {
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
    lv_obj_add_event_cb(widget->obj, tach_draw_cb, LV_EVENT_DRAW_MAIN, widget);

    /* x10 WPM scale, compressed low end like the GR86's "0 2 4" */
    add_scale_numeral(widget->obj, 0, "0");
    add_scale_numeral(widget->obj, 20, "2");
    add_scale_numeral(widget->obj, 40, "4");
    add_scale_numeral(widget->obj, 60, "6");
    add_scale_numeral(widget->obj, 80, "8");
    add_scale_numeral(widget->obj, 100, "10");
    add_scale_numeral(widget->obj, 120, "12");

    trk_label(widget->obj, &DINishCondensed_SemiBold_20, TRK_COLOR_TEXT, BAR_X0 - 2,
              BAR_BOTTOM + 6, 0, LV_TEXT_ALIGN_LEFT, "x10 WPM");

    sys_slist_append(&widgets, &widget->node);

    if (tach_timer == NULL) {
        tach_timer = lv_timer_create(tach_timer_cb, FRAME_MS, NULL);
        lv_timer_pause(tach_timer);
    }

    widget_track_tach_init();

    return 0;
}

lv_obj_t *zmk_widget_track_tach_obj(struct zmk_widget_track_tach *widget) {
    return widget->obj;
}
