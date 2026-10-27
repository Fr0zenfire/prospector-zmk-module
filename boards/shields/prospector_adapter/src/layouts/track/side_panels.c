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

#include "display_colors.h"
#include "track_common.h"

#define PERIPHERAL_COUNT MIN(ZMK_SPLIT_BLE_PERIPHERAL_COUNT, TRK_MAX_PERIPHERALS)

#define LOW_BATTERY 20

/* Shared row geometry (panel-local coordinates, panel top at screen y = 114) */
#define ROW_PITCH   34
#define ROW_BAR_DY  21
#define BAR_H       10
#define BEZEL_Y     86
#define BOTTOM_Y    96

#define BATT_ROW_Y0 11
#define OUT_ROW_Y   27

/* ========================================================================== */
/*  Left panel: battery gauges + odometer                                      */
/* ========================================================================== */

#define L_X0    4
#define L_BAR_W 60

static sys_slist_t battery_widgets = SYS_SLIST_STATIC_INIT(&battery_widgets);

static uint8_t battery_level[TRK_MAX_PERIPHERALS];
static bool battery_connected[TRK_MAX_PERIPHERALS];
static bool battery_known[TRK_MAX_PERIPHERALS];

static void render_battery(struct zmk_widget_track_battery *w, int i) {
    if (i >= PERIPHERAL_COUNT || !w->pct_labels[i]) {
        return;
    }

    uint8_t lvl = battery_level[i] > 100 ? 100 : battery_level[i];
    bool online = battery_connected[i];
    char text[8];

    if (online && !battery_known[i]) {
        snprintf(text, sizeof(text), "--");
        lv_obj_add_flag(w->fills[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(w->frames[i], lv_color_hex(TRK_COLOR_GAUGE_FRAME),
                                      LV_PART_MAIN);
        lv_obj_set_style_text_color(w->pct_labels[i], lv_color_hex(TRK_COLOR_TEXT),
                                    LV_PART_MAIN);
    } else if (online) {
        snprintf(text, sizeof(text), "%u%%", (unsigned int)lvl);
        uint32_t fill = (lvl <= LOW_BATTERY) ? TRK_COLOR_GAUGE_LOW : TRK_COLOR_GAUGE_FILL;
        int width = ((L_BAR_W - 4) * lvl) / 100;
        lv_obj_set_width(w->fills[i], width > 0 ? width : 1);
        lv_obj_set_style_bg_color(w->fills[i], lv_color_hex(fill), LV_PART_MAIN);
        lv_obj_clear_flag(w->fills[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(w->frames[i], lv_color_hex(TRK_COLOR_GAUGE_FRAME),
                                      LV_PART_MAIN);
        lv_obj_set_style_text_color(w->pct_labels[i],
            lv_color_hex(lvl <= LOW_BATTERY ? TRK_COLOR_GAUGE_LOW : TRK_COLOR_TEXT),
            LV_PART_MAIN);
    } else {
        snprintf(text, sizeof(text), "--");
        lv_obj_add_flag(w->fills[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_border_color(w->frames[i], lv_color_hex(TRK_COLOR_BEZEL),
                                      LV_PART_MAIN);
        lv_obj_set_style_text_color(w->pct_labels[i], lv_color_hex(TRK_COLOR_TEXT_DIM),
                                    LV_PART_MAIN);
    }

    lv_label_set_text(w->pct_labels[i], text);
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

    for (int i = 0; i < TRK_MAX_PERIPHERALS; i++) {
        widget->pct_labels[i] = NULL;
    }

    for (int i = 0; i < PERIPHERAL_COUNT && i < 2; i++) {
        int y = BATT_ROW_Y0 + i * ROW_PITCH;
        char name[4];
        if (PERIPHERAL_COUNT == 2) {
            snprintf(name, sizeof(name), "%s", i == 0 ? "L" : "R");
        } else {
            snprintf(name, sizeof(name), "%d", i + 1);
        }

        trk_label(widget->obj, &DINishCondensed_SemiBold_20, TRK_COLOR_TEXT, L_X0, y, 0,
                  LV_TEXT_ALIGN_LEFT, name);
        widget->pct_labels[i] = trk_label(widget->obj, &DINishCondensed_SemiBold_20,
                                          TRK_COLOR_TEXT_DIM, L_X0 + 20, y, L_BAR_W - 20,
                                          LV_TEXT_ALIGN_RIGHT, "--");

        trk_fill(widget->obj, L_X0 + 2, y + ROW_BAR_DY, L_BAR_W - 4, BAR_H - 2,
                 TRK_COLOR_GAUGE_EMPTY);
        widget->fills[i] = trk_fill(widget->obj, L_X0 + 2, y + ROW_BAR_DY, 1, BAR_H - 2,
                                    TRK_COLOR_GAUGE_FILL);
        lv_obj_add_flag(widget->fills[i], LV_OBJ_FLAG_HIDDEN);
        widget->frames[i] = trk_gauge_frame(widget->obj, L_X0, y + ROW_BAR_DY - 2, L_BAR_W,
                                            BAR_H + 2);
        lv_obj_set_style_border_color(widget->frames[i], lv_color_hex(TRK_COLOR_BEZEL),
                                      LV_PART_MAIN);
    }

    /* bezel line meeting the curved line in the centre */
    trk_fill(widget->obj, 0, BEZEL_Y, SIDE_W, 2, TRK_COLOR_BEZEL);

    trk_label(widget->obj, &DINishCondensed_SemiBold_20, TRK_COLOR_TEXT_DIM, 2, BOTTOM_Y, 0,
              LV_TEXT_ALIGN_LEFT, "ODO");
    widget->odo_label = trk_label(widget->obj, &DINishCondensed_SemiBold_20, TRK_COLOR_TEXT,
                                  36, BOTTOM_Y, 0, LV_TEXT_ALIGN_LEFT, "0");

    for (int i = 0; i < PERIPHERAL_COUNT; i++) {
        render_battery(widget, i);
    }

    widget_track_battery_init();
    widget_track_split_link_init();
    widget_track_odo_init();

    sys_slist_append(&battery_widgets, &widget->node);

    return 0;
}

lv_obj_t *zmk_widget_track_battery_obj(struct zmk_widget_track_battery *widget) {
    return widget->obj;
}

/* ========================================================================== */
/*  Right panel: output + link lamp                                            */
/* ========================================================================== */

#define R_X0    6
#define R_BAR_W 60
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
    const char *icon;
    const char *text;
    if (usb) {
        lamp = TRK_COLOR_LINK_USB;
        icon = LV_SYMBOL_USB;
        text = "USB";
    } else if (active_profile_connected) {
        lamp = TRK_COLOR_LINK_OK;
        icon = LV_SYMBOL_BLUETOOTH;
        text = "LINK";
    } else {
        lamp = TRK_COLOR_LINK_WARN;
        icon = LV_SYMBOL_BLUETOOTH;
        text = "OFF";
    }

    if (w->link_icon) {
        lv_label_set_text(w->link_icon, icon);
        lv_obj_set_style_text_color(w->link_icon, lv_color_hex(lamp), LV_PART_MAIN);
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

    /* Row 1: transport + profile */
    widget->transport_label = trk_label(widget->obj, &DINishCondensed_SemiBold_20,
                                        TRK_COLOR_TEXT, R_X0, OUT_ROW_Y, 0, LV_TEXT_ALIGN_LEFT, "BLE");
    widget->profile_label = trk_label(widget->obj, &DINishCondensed_SemiBold_20,
                                      TRK_COLOR_TEXT, R_X0 + 30, OUT_ROW_Y, R_BAR_W - 30,
                                      LV_TEXT_ALIGN_RIGHT, "P1");

    /* Fuel-gauge style profile cells */
    int inner = R_BAR_W - 4;
    int cell_w = (inner - (ZMK_BLE_PROFILE_COUNT - 1) * CELL_GAP) / ZMK_BLE_PROFILE_COUNT;
    if (cell_w < 2) cell_w = 2;
    int used = cell_w * ZMK_BLE_PROFILE_COUNT + CELL_GAP * (ZMK_BLE_PROFILE_COUNT - 1);
    int cx = R_X0 + 2 + (inner - used) / 2;
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        widget->cells[i] = trk_fill(widget->obj, cx + i * (cell_w + CELL_GAP), OUT_ROW_Y + ROW_BAR_DY,
                                    cell_w, BAR_H - 2, TRK_COLOR_GAUGE_EMPTY);
    }
    trk_gauge_frame(widget->obj, R_X0, OUT_ROW_Y + ROW_BAR_DY - 2, R_BAR_W, BAR_H + 2);

    /* bezel line meeting the centre cluster */
    trk_fill(widget->obj, 0, BEZEL_Y, SIDE_W, 2, TRK_COLOR_BEZEL);

    /* Link telltale */
    widget->link_icon = lv_label_create(widget->obj);
    lv_obj_set_pos(widget->link_icon, R_X0, BOTTOM_Y);
    lv_label_set_text(widget->link_icon, LV_SYMBOL_BLUETOOTH);
    widget->link_label = trk_label(widget->obj, &DINishCondensed_SemiBold_20,
                                   TRK_COLOR_LINK_WARN, R_X0 + 24, BOTTOM_Y, R_BAR_W - 24,
                                   LV_TEXT_ALIGN_RIGHT, "OFF");

    update_output_widget(widget);
    sys_slist_append(&output_widgets, &widget->node);

    return 0;
}

lv_obj_t *zmk_widget_track_output_obj(struct zmk_widget_track_output *widget) {
    return widget->obj;
}
