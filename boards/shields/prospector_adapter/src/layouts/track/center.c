/*
 * Centre "bowl" of the GR86 Track cluster:
 *   - big digital readout (speed -> live WPM) with a small unit label
 *   - red-orange TRACK pill -> current layer name
 *   - grey U-shaped bezel
 */

#include "center.h"

#include <ctype.h>
#include <stdio.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

#include "display_colors.h"
#include "track_common.h"

#define PILL_W 104
#define PILL_H 24
#define PILL_Y 62

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

void track_center_set_wpm(int wpm) {
    char text[8];
    snprintf(text, sizeof(text), "%d", wpm);

    struct zmk_widget_track_center *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        lv_label_set_text(widget->wpm_label, text);
    }
}

struct layer_state {
    uint8_t index;
};

static void layer_update_cb(struct layer_state state) {
    const char *name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(state.index));
    char text[20];

    if (name && *name) {
        snprintf(text, sizeof(text), "%s", name);
    } else {
        snprintf(text, sizeof(text), "LAYER %d", state.index);
    }

#if IS_ENABLED(CONFIG_PROSPECTOR_LAYER_NAME_UPPERCASE)
    for (int i = 0; text[i]; i++) {
        text[i] = toupper((unsigned char)text[i]);
    }
#endif

    struct zmk_widget_track_center *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        lv_label_set_text(widget->layer_label, text);
    }
}

static struct layer_state layer_get_state(const zmk_event_t *eh) {
    return (struct layer_state){.index = zmk_keymap_highest_layer_active()};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_track_center_layer, struct layer_state, layer_update_cb,
                            layer_get_state)
ZMK_SUBSCRIPTION(widget_track_center_layer, zmk_layer_state_changed);

/* Precomputed true circular arc points connecting left line (0,86) to right line (140,86) */
/* R = 132.5 px, depth = 20 px, cradling smoothly under the TRACK pill */
static const struct {
    int16_t x;
    int16_t y;
} curve_pts[] = {
    {0, 86},   {5, 89},   {10, 92},  {15, 94},  {20, 96},
    {25, 98},  {30, 100}, {35, 101}, {40, 103}, {45, 104},
    {50, 104}, {55, 105}, {60, 106}, {65, 106}, {70, 106},
    {75, 106}, {80, 106}, {85, 105}, {90, 104}, {95, 104},
    {100, 103},{105, 101},{110, 100},{115, 98}, {120, 96},
    {125, 94}, {130, 92}, {135, 89}, {140, 86}
};

static void center_draw_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const int ox = coords.x1;
    const int oy = coords.y1;

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(TRK_COLOR_BEZEL);
    line.width = 2;
    line.round_start = 1;
    line.round_end = 1;
    line.opa = LV_OPA_COVER;

    for (size_t i = 0; i < sizeof(curve_pts) / sizeof(curve_pts[0]) - 1; i++) {
        line.p1.x = ox + curve_pts[i].x;
        line.p1.y = oy + curve_pts[i].y;
        line.p2.x = ox + curve_pts[i + 1].x;
        line.p2.y = oy + curve_pts[i + 1].y;
        lv_draw_line(layer, &line);
    }
}

int zmk_widget_track_center_init(struct zmk_widget_track_center *widget, lv_obj_t *parent) {
    widget->obj = trk_box(parent);
    lv_obj_set_size(widget->obj, CENTER_W, CENTER_H);
    lv_obj_add_event_cb(widget->obj, center_draw_cb, LV_EVENT_DRAW_MAIN, widget);

    /* Big readout, centred in the left 112px so the unit sits to its right */
    widget->wpm_label = trk_label(widget->obj, &FR_Regular_48, TRK_COLOR_TEXT, 0, 10, 112,
                                  LV_TEXT_ALIGN_CENTER, "0");

    trk_label(widget->obj, &DINishCondensed_SemiBold_20, TRK_COLOR_TEXT, 102, 36, 0,
              LV_TEXT_ALIGN_LEFT, "WPM");

    /* TRACK pill -> layer name */
    widget->pill = trk_box(widget->obj);
    lv_obj_set_size(widget->pill, PILL_W, PILL_H);
    lv_obj_set_pos(widget->pill, (CENTER_W - PILL_W) / 2, PILL_Y);
    lv_obj_set_style_radius(widget->pill, PILL_H / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(widget->pill, lv_color_hex(TRK_COLOR_PILL_TOP), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(widget->pill, lv_color_hex(TRK_COLOR_PILL_BOTTOM),
                                   LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(widget->pill, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(widget->pill, LV_OPA_COVER, LV_PART_MAIN);

    widget->layer_label = trk_label(widget->pill, &DINishCondensed_SemiBold_20,
                                    TRK_COLOR_PILL_TEXT, 0, 1, PILL_W, LV_TEXT_ALIGN_CENTER,
                                    "");

    sys_slist_append(&widgets, &widget->node);
    widget_track_center_layer_init();

    return 0;
}

lv_obj_t *zmk_widget_track_center_obj(struct zmk_widget_track_center *widget) {
    return widget->obj;
}
