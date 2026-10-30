/*
 * Side panels of the GR86 Track cluster.
 *
 * Left  (voltage / oil-temp bars)  -> L / R half battery gauges
 *       (ODO)                      -> keystroke odometer since boot
 * Right (out temp + clock)         -> output transport + BLE profile
 *       (fuel gauge, segmented)    -> BLE profile slots
 *       (coolant gauge, marker)    -> session peak WPM marker
 *       (traction "OFF" telltale)  -> host link status lamp
 */

#include "side_panels.h"

#include <stdio.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/split_central_status_changed.h>

#include <symbols.h>
#include "display_colors.h"
#include "track_common.h"

#define PERIPHERAL_COUNT MIN(ZMK_SPLIT_BLE_PERIPHERAL_COUNT, TRK_MAX_PERIPHERALS)

#define LOW_BATTERY 20

/* Shared row geometry (panel-local coordinates, panel top at screen y = 114) */
#define ROW_PITCH   34
#define ROW_BAR_DY  21
#define BAR_H       10
#define BEZEL_Y     86
#define BOTTOM_Y    92

#define BATT_ROW_Y0 11
#define OUT_ROW_Y   27

/* 16x16 4bpp embedded glyphs for Gasoline ('F') and Wireless waves ('W') */
static const uint8_t trk_icons_16_bitmaps[] = {
    /* 'F' (0x46) - Gasoline pump icon */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0f, 0xff, 0xf0, 0x00, 0x00, 0x00,
    0x00, 0x0f, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xf0, 0x00, 0xff, 0xfc, 0x00, 0x00,
    0x00, 0xff, 0xf0, 0x00, 0xff, 0xf0, 0xf0, 0x00,
    0x00, 0xff, 0xf0, 0x00, 0xff, 0xf0, 0x0f, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xdf, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x0f, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x0f, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xf0, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00,
    0x0f, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    /* 'W' (0x57) - Compact wireless / connection waves icon */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0f, 0xff, 0xf0, 0x00, 0x00, 0x00,
    0x00, 0x0f, 0xf0, 0x00, 0x0f, 0xf0, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00,
    0x0f, 0xf0, 0x00, 0x00, 0x00, 0x0f, 0xf0, 0x00,
    0x00, 0x00, 0x0f, 0xff, 0xf0, 0x00, 0x00, 0x00,
    0x00, 0x0f, 0xf0, 0x00, 0x0f, 0xf0, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0f, 0xff, 0xf0, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xff, 0x00, 0x0f, 0xf0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const lv_font_fmt_txt_glyph_dsc_t trk_icons_16_glyph_dsc[] = {
    {.bitmap_index = 0,   .adv_w = 0,   .box_w = 0,  .box_h = 0,  .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0,   .adv_w = 256, .box_w = 16, .box_h = 16, .ofs_x = 0, .ofs_y = 0}, /* 'F' */
    {.bitmap_index = 128, .adv_w = 256, .box_w = 16, .box_h = 16, .ofs_x = 0, .ofs_y = 0}, /* 'W' */
};

static const uint16_t trk_icons_16_unicode_list[] = {
    0,           /* 'F' - 0x46 = 0 */
    0x57 - 0x46  /* 'W' - 0x46 = 17 */
};

static const lv_font_fmt_txt_cmap_t trk_icons_16_cmaps[] = {
    {
        .range_start = 0x46,
        .range_length = 0x57 - 0x46 + 1,
        .glyph_id_start = 1,
        .unicode_list = trk_icons_16_unicode_list,
        .glyph_id_ofs_list = NULL,
        .list_length = 2,
        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
    }
};

#if LVGL_VERSION_MAJOR == 8
static lv_font_fmt_txt_glyph_cache_t trk_icons_16_cache;
#endif

static const lv_font_fmt_txt_dsc_t trk_icons_16_font_dsc = {
    .glyph_bitmap = trk_icons_16_bitmaps,
    .glyph_dsc = trk_icons_16_glyph_dsc,
    .cmaps = trk_icons_16_cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &trk_icons_16_cache,
#endif
};

static const lv_font_t trk_icons_16 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = 16,
    .base_line = 0,
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &trk_icons_16_font_dsc,
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};

/* ========================================================================== */
/*  Left panel: battery gauges + odometer                                      */
/* ========================================================================== */

#define L_X0    8
#define L_BAR_W 56

static sys_slist_t battery_widgets = SYS_SLIST_STATIC_INIT(&battery_widgets);

static uint8_t battery_level[TRK_MAX_PERIPHERALS];
static bool battery_connected[TRK_MAX_PERIPHERALS];
static bool battery_known[TRK_MAX_PERIPHERALS];

static void render_battery(struct zmk_widget_track_battery *w, int i) {
    if (i >= PERIPHERAL_COUNT) {
        return;
    }

    uint8_t lvl = battery_level[i] > 100 ? 100 : battery_level[i];
    bool online = battery_connected[i];
    char text[8];

    if (online && !battery_known[i]) {
        snprintf(text, sizeof(text), "--");
        if (w->fills[i]) {
            lv_obj_add_flag(w->fills[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (w->frames[i]) {
            lv_obj_set_style_border_color(w->frames[i], lv_color_hex(TRK_COLOR_GAUGE_FRAME),
                                          LV_PART_MAIN);
        }
        if (w->pct_labels[i]) {
            lv_obj_set_style_text_color(w->pct_labels[i], lv_color_hex(TRK_COLOR_TEXT),
                                        LV_PART_MAIN);
            lv_label_set_text(w->pct_labels[i], text);
        }
    } else if (online) {
        snprintf(text, sizeof(text), "%u", (unsigned int)lvl);
        uint32_t fill = (lvl <= LOW_BATTERY) ? TRK_COLOR_GAUGE_LOW : TRK_COLOR_GAUGE_FILL;
        int width = ((L_BAR_W - 4) * lvl) / 100;
        if (w->fills[i]) {
            lv_obj_set_width(w->fills[i], width > 0 ? width : 1);
            lv_obj_set_style_bg_color(w->fills[i], lv_color_hex(fill), LV_PART_MAIN);
            lv_obj_clear_flag(w->fills[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (w->frames[i]) {
            lv_obj_set_style_border_color(w->frames[i], lv_color_hex(TRK_COLOR_GAUGE_FRAME),
                                          LV_PART_MAIN);
        }
        if (w->pct_labels[i]) {
            lv_obj_set_style_text_color(w->pct_labels[i],
                lv_color_hex(lvl <= LOW_BATTERY ? TRK_COLOR_GAUGE_LOW : TRK_COLOR_TEXT),
                LV_PART_MAIN);
            lv_label_set_text(w->pct_labels[i], text);
        }
    } else {
        snprintf(text, sizeof(text), "--");
        if (w->fills[i]) {
            lv_obj_add_flag(w->fills[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (w->frames[i]) {
            lv_obj_set_style_border_color(w->frames[i], lv_color_hex(TRK_COLOR_BEZEL),
                                          LV_PART_MAIN);
        }
        if (w->pct_labels[i]) {
            lv_obj_set_style_text_color(w->pct_labels[i], lv_color_hex(TRK_COLOR_TEXT_DIM),
                                        LV_PART_MAIN);
            lv_label_set_text(w->pct_labels[i], text);
        }
    }
}

struct battery_state {
    uint8_t source;
    uint8_t level;
};

static void battery_update_cb(struct battery_state s) {
    if (s.source >= PERIPHERAL_COUNT) {
        return;
    }
    battery_level[s.source] = s.level;
    battery_known[s.source] = true;
    battery_connected[s.source] = true;

    struct zmk_widget_track_battery *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&battery_widgets, w, node) {
        render_battery(w, s.source);
    }
}

static struct battery_state battery_get_state(const zmk_event_t *eh) {
    if (eh) {
        const struct zmk_peripheral_battery_state_changed *ev =
            as_zmk_peripheral_battery_state_changed(eh);
        if (ev) {
            return (struct battery_state){.source = ev->source, .level = ev->state_of_charge};
        }
    }
    return (struct battery_state){.source = 0, .level = 0};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_track_battery, struct battery_state, battery_update_cb,
                            battery_get_state)
ZMK_SUBSCRIPTION(widget_track_battery, zmk_peripheral_battery_state_changed);

struct link_state {
    uint8_t slot;
    bool connected;
};

static void link_update_cb(struct link_state s) {
    if (s.slot >= PERIPHERAL_COUNT) {
        return;
    }
    battery_connected[s.slot] = s.connected;

    struct zmk_widget_track_battery *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&battery_widgets, w, node) {
        render_battery(w, s.slot);
    }
}

static struct link_state link_get_state(const zmk_event_t *eh) {
    if (eh) {
        const struct zmk_split_central_status_changed *ev =
            as_zmk_split_central_status_changed(eh);
        if (ev) {
            return (struct link_state){.slot = ev->slot, .connected = ev->connected};
        }
    }
    return (struct link_state){.slot = 0, .connected = false};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_track_split_link, struct link_state, link_update_cb,
                            link_get_state)
ZMK_SUBSCRIPTION(widget_track_split_link, zmk_split_central_status_changed);

/* --- keystroke odometer (driven by keycode events received on central dongle) --- */

static atomic_t keystrokes = ATOMIC_INIT(0);
static uint32_t odo_shown = UINT32_MAX;

struct odo_state {
    uint32_t count;
};

static void odo_update_cb(struct odo_state s) {
    if (s.count == odo_shown) {
        return;
    }
    odo_shown = s.count;

    char text[12];
    unsigned int count = (unsigned int)s.count;
    if (count < 10000) {
        snprintf(text, sizeof(text), "%u", count);
    } else if (count < 1000000) {
        snprintf(text, sizeof(text), "%uK", count / 1000);
    } else {
        snprintf(text, sizeof(text), "%uM", count / 1000000);
    }

    struct zmk_widget_track_battery *w;
    SYS_SLIST_FOR_EACH_CONTAINER(&battery_widgets, w, node) {
        if (w->odo_label) {
            lv_label_set_text(w->odo_label, text);
        }
    }
}

static struct odo_state odo_get_state(const zmk_event_t *eh) {
    if (eh) {
        const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
        if (ev && ev->state) {
            atomic_inc(&keystrokes);
        }
    }
    return (struct odo_state){.count = (uint32_t)atomic_get(&keystrokes)};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_track_odo, struct odo_state, odo_update_cb, odo_get_state)
ZMK_SUBSCRIPTION(widget_track_odo, zmk_keycode_state_changed);

int zmk_widget_track_battery_init(struct zmk_widget_track_battery *widget, lv_obj_t *parent) {
    widget->obj = trk_box(parent);
    lv_obj_set_size(widget->obj, SIDE_W, SIDE_H);
    lv_obj_add_flag(widget->obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    for (int i = 0; i < TRK_MAX_PERIPHERALS; i++) {
        widget->pct_labels[i] = NULL;
        widget->fills[i] = NULL;
        widget->frames[i] = NULL;
    }

    /* Single gasoline icon centered above both battery bars */
    trk_label(widget->obj, &trk_icons_16, TRK_COLOR_TEXT, L_X0, 14, L_BAR_W, LV_TEXT_ALIGN_CENTER, "F");

    for (int i = 0; i < PERIPHERAL_COUNT && i < 2; i++) {
        int bar_y = (i == 0) ? 36 : 54;

        trk_fill(widget->obj, L_X0 + 2, bar_y + 2, L_BAR_W - 4, BAR_H - 2,
                 TRK_COLOR_GAUGE_EMPTY);
        widget->fills[i] = trk_fill(widget->obj, L_X0 + 2, bar_y + 2, 1, BAR_H - 2,
                                    TRK_COLOR_GAUGE_FILL);
        if (widget->fills[i]) {
            lv_obj_add_flag(widget->fills[i], LV_OBJ_FLAG_HIDDEN);
        }
        widget->frames[i] = trk_gauge_frame(widget->obj, L_X0, bar_y, L_BAR_W,
                                            BAR_H + 2);
        if (widget->frames[i]) {
            lv_obj_set_style_border_color(widget->frames[i], lv_color_hex(TRK_COLOR_BEZEL),
                                          LV_PART_MAIN);
        }
    }

    /* bezel line meeting the curved line in the centre */
    trk_fill(widget->obj, L_X0, BEZEL_Y, SIDE_W - L_X0, 2, TRK_COLOR_BEZEL);

    trk_label(widget->obj, &lv_font_montserrat_12, TRK_COLOR_TEXT_DIM, 12, 96, 0,
              LV_TEXT_ALIGN_LEFT, "ODO");
    widget->odo_label = trk_label(widget->obj, &DINishCondensed_SemiBold_20, TRK_COLOR_TEXT,
                                  38, 90, 0, LV_TEXT_ALIGN_LEFT, "0");

    sys_slist_append(&battery_widgets, &widget->node);

    for (int i = 0; i < PERIPHERAL_COUNT; i++) {
        render_battery(widget, i);
    }

    widget_track_battery_init();
    widget_track_split_link_init();
    widget_track_odo_init();

    return 0;
}

lv_obj_t *zmk_widget_track_battery_obj(struct zmk_widget_track_battery *widget) {
    return widget->obj;
}

/* ========================================================================== */
/*  Right panel: output + link lamp                                            */
/* ========================================================================== */

#define R_X0    6
#define R_BAR_W 56
#define CELL_GAP 2

static sys_slist_t output_widgets = SYS_SLIST_STATIC_INIT(&output_widgets);

static uint8_t active_profile_index = 0;
static enum zmk_transport active_transport = ZMK_TRANSPORT_USB;
static bool active_profile_connected = false;

static void update_output_widget(struct zmk_widget_track_output *w) {
    bool usb = (active_transport == ZMK_TRANSPORT_USB);
    char profile_text[6];
    snprintf(profile_text, sizeof(profile_text), "P%u", (unsigned int)(active_profile_index + 1));

    if (w->transport_label) {
        lv_label_set_text(w->transport_label, usb ? "USB" : "BLE");
    }
    if (w->profile_label) {
        lv_label_set_text(w->profile_label, profile_text);
    }

    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        if (!w->cells[i]) continue;
        uint32_t color = TRK_COLOR_GAUGE_EMPTY;
        if (i == active_profile_index) {
            if (usb) {
                color = TRK_COLOR_TEXT_DIM;
            } else {
                color = active_profile_connected ? TRK_COLOR_GAUGE_FILL : TRK_COLOR_LINK_WARN;
            }
        }
        lv_obj_set_style_bg_color(w->cells[i], lv_color_hex(color), LV_PART_MAIN);
    }

    uint32_t lamp;
    const char *text;
    if (usb) {
        lamp = TRK_COLOR_LINK_USB;
        text = LV_SYMBOL_USB;
    } else if (active_profile_connected) {
        lamp = TRK_COLOR_LINK_OK;
        text = LV_SYMBOL_BLUETOOTH;
    } else {
        lamp = TRK_COLOR_LINK_WARN;
        text = LV_SYMBOL_BLUETOOTH;
    }

    if (w->link_icon) {
        lv_obj_add_flag(w->link_icon, LV_OBJ_FLAG_HIDDEN);
    }
    if (w->link_label) {
        lv_label_set_text(w->link_label, text);
        lv_obj_set_style_text_color(w->link_label, lv_color_hex(lamp), LV_PART_MAIN);
    }
}

static int endpoint_changed_listener(const zmk_event_t *eh) {
    const struct zmk_endpoint_changed *event = as_zmk_endpoint_changed(eh);
    if (event) {
        struct zmk_endpoint_instance selected = zmk_endpoint_get_selected();
        active_transport = selected.transport;

        struct zmk_widget_track_output *w;
        SYS_SLIST_FOR_EACH_CONTAINER(&output_widgets, w, node) {
            update_output_widget(w);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

static int ble_active_profile_changed_listener(const zmk_event_t *eh) {
    const struct zmk_ble_active_profile_changed *event = as_zmk_ble_active_profile_changed(eh);
    if (event) {
        active_profile_index = event->index;
        active_profile_connected = zmk_ble_active_profile_is_connected();

        struct zmk_widget_track_output *w;
        SYS_SLIST_FOR_EACH_CONTAINER(&output_widgets, w, node) {
            update_output_widget(w);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(widget_track_output_endpoint, endpoint_changed_listener);
ZMK_SUBSCRIPTION(widget_track_output_endpoint, zmk_endpoint_changed);

ZMK_LISTENER(widget_track_output_profile, ble_active_profile_changed_listener);
ZMK_SUBSCRIPTION(widget_track_output_profile, zmk_ble_active_profile_changed);

int zmk_widget_track_output_init(struct zmk_widget_track_output *widget, lv_obj_t *parent) {
    widget->obj = trk_box(parent);
    lv_obj_set_size(widget->obj, SIDE_W, SIDE_H);

    /* Row 1: compact wireless connection icon */
    widget->transport_label = NULL;
    widget->profile_label = NULL;
    trk_label(widget->obj, &trk_icons_16, TRK_COLOR_TEXT, R_X0, 20, R_BAR_W,
              LV_TEXT_ALIGN_CENTER, "W");

    /* Fuel-gauge style profile cells (at y = 48..60, giving 12px gap below icon) */
    int bar_y = 48;
    int inner = R_BAR_W - 4;
    int cell_w = (inner - (ZMK_BLE_PROFILE_COUNT - 1) * CELL_GAP) / ZMK_BLE_PROFILE_COUNT;
    if (cell_w < 2) cell_w = 2;
    int used = cell_w * ZMK_BLE_PROFILE_COUNT + CELL_GAP * (ZMK_BLE_PROFILE_COUNT - 1);
    int cx = R_X0 + 2 + (inner - used) / 2;
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        widget->cells[i] = trk_fill(widget->obj, cx + i * (cell_w + CELL_GAP), bar_y + 2,
                                    cell_w, BAR_H - 2, TRK_COLOR_GAUGE_EMPTY);
    }
    trk_gauge_frame(widget->obj, R_X0, bar_y, R_BAR_W, BAR_H + 2);

    /* bezel line meeting the centre cluster */
    trk_fill(widget->obj, 0, BEZEL_Y, R_X0 + R_BAR_W, 2, TRK_COLOR_BEZEL);

    /* Link telltale (using Montserrat 20 symbol font) */
    widget->link_icon = NULL;
    widget->link_label = trk_label(widget->obj, &lv_font_montserrat_20,
                                   TRK_COLOR_LINK_WARN, 0, BOTTOM_Y, 50,
                                   LV_TEXT_ALIGN_RIGHT, LV_SYMBOL_BLUETOOTH);

    if (sys_slist_is_empty(&output_widgets)) {
        active_profile_index = zmk_ble_active_profile_index();
        active_profile_connected = zmk_ble_active_profile_is_connected();
        struct zmk_endpoint_instance selected = zmk_endpoint_get_selected();
        active_transport = selected.transport;
    }

    update_output_widget(widget);
    sys_slist_append(&output_widgets, &widget->node);

    return 0;
}

lv_obj_t *zmk_widget_track_output_obj(struct zmk_widget_track_output *widget) {
    return widget->obj;
}
