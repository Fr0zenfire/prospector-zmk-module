/*
 * Modifier status as dash "telltale" lamps across the top of the cluster,
 * like the BRAKE / turn-signal row above the GR86 tach. Unlit lamps stay
 * faintly visible; lit lamps glow in their own colour (SHFT = BRAKE red).
 */

#include "telltales.h"

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>
#ifdef CONFIG_DT_HAS_ZMK_BEHAVIOR_CAPS_WORD_ENABLED
#include <zmk/events/caps_word_state_changed.h>
#endif

#include <modifier_order.h>

#include "display_colors.h"
#include "track_common.h"

#define LAMP_W      52
#define TELL_INSET  26

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

struct telltale_state {
    bool mods[MOD_TYPE_COUNT];
    bool caps_word;
};

#ifdef CONFIG_DT_HAS_ZMK_BEHAVIOR_CAPS_WORD_ENABLED
static bool caps_word_active = false;
#endif

static uint32_t lamp_color(enum modifier_type type) {
    switch (type) {
    case MOD_TYPE_GUI:
        return TRK_COLOR_TELL_GUI;
    case MOD_TYPE_ALT:
        return TRK_COLOR_TELL_ALT;
    case MOD_TYPE_CTRL:
        return TRK_COLOR_TELL_CTRL;
    case MOD_TYPE_SHIFT:
    default:
        return TRK_COLOR_TELL_SHIFT;
    }
}

static void telltale_update_cb(struct telltale_state state) {
    struct zmk_widget_track_telltales *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        for (int i = 0; i < 4; i++) {
            enum modifier_type type = modifier_order_get(i);
            uint32_t color = state.mods[type] ? lamp_color(type) : TRK_COLOR_TELL_OFF;

            if (type == MOD_TYPE_SHIFT && state.caps_word) {
                color = TRK_COLOR_TELL_CAPS_WORD;
            }

            lv_obj_set_style_text_color(widget->labels[i], lv_color_hex(color), LV_PART_MAIN);
        }
    }
}

static struct telltale_state telltale_get_state(const zmk_event_t *eh) {
#ifdef CONFIG_DT_HAS_ZMK_BEHAVIOR_CAPS_WORD_ENABLED
    if (eh != NULL) {
        const struct zmk_caps_word_state_changed *ev = as_zmk_caps_word_state_changed(eh);
        if (ev != NULL) {
            caps_word_active = ev->active;
        }
    }
#endif

    zmk_mod_flags_t mods = zmk_hid_get_explicit_mods();

    struct telltale_state state = {0};
    state.mods[MOD_TYPE_GUI] = (mods & (MOD_LGUI | MOD_RGUI)) != 0;
    state.mods[MOD_TYPE_ALT] = (mods & (MOD_LALT | MOD_RALT)) != 0;
    state.mods[MOD_TYPE_CTRL] = (mods & (MOD_LCTL | MOD_RCTL)) != 0;
    state.mods[MOD_TYPE_SHIFT] = (mods & (MOD_LSFT | MOD_RSFT)) != 0;
#ifdef CONFIG_DT_HAS_ZMK_BEHAVIOR_CAPS_WORD_ENABLED
    state.caps_word = caps_word_active;
#endif
    return state;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_track_telltales, struct telltale_state, telltale_update_cb,
                            telltale_get_state)
ZMK_SUBSCRIPTION(widget_track_telltales, zmk_keycode_state_changed);
#ifdef CONFIG_DT_HAS_ZMK_BEHAVIOR_CAPS_WORD_ENABLED
ZMK_SUBSCRIPTION(widget_track_telltales, zmk_caps_word_state_changed);
#endif

int zmk_widget_track_telltales_init(struct zmk_widget_track_telltales *widget,
                                    lv_obj_t *parent) {
    widget->obj = trk_box(parent);
    lv_obj_set_size(widget->obj, TELLTALE_W, TELLTALE_H);

    int span = TELLTALE_W - 2 * TELL_INSET; /* 228px */
    int pitch = span / 4;                   /* 57px */
    for (int i = 0; i < 4; i++) {
        int cx = TELL_INSET + pitch / 2 + i * pitch;
        widget->labels[i] = trk_label(widget->obj, &DINishCondensed_SemiBold_20,
                                      TRK_COLOR_TELL_OFF, cx - LAMP_W / 2, 0, LAMP_W,
                                      LV_TEXT_ALIGN_CENTER, modifier_order_get_text(i));
    }

    sys_slist_append(&widgets, &widget->node);
    widget_track_telltales_init();

    return 0;
}

lv_obj_t *zmk_widget_track_telltales_obj(struct zmk_widget_track_telltales *widget) {
    return widget->obj;
}
