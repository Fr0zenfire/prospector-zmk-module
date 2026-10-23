#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(display_idle, CONFIG_ZMK_LOG_LEVEL);

#include <lvgl.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_KEYMAP_SENSORS)
#include <zmk/events/sensor_event.h>
#endif

#if IS_ENABLED(CONFIG_PM_DEVICE)
#include <zephyr/pm/device.h>
#endif

#include "brightness.h"
#include "prospector/display_power.h"

static const struct device *display_dev;
static const struct device *pwm_leds_dev;

#define DISP_BL DT_NODE_CHILD_IDX(DT_NODELABEL(disp_bl))

#if defined(CONFIG_PROSPECTOR_FIXED_BRIGHTNESS)
#define DEFAULT_SAVED_BRIGHTNESS CONFIG_PROSPECTOR_FIXED_BRIGHTNESS
#else
#define DEFAULT_SAVED_BRIGHTNESS 80
#endif

#define SLEEP_ANIM_DURATION_MS 260
#define SLEEP_ANIM_STAGGER_MS  24
#define SLEEP_TOTAL_MS         380

#define WAKE_ANIM_DURATION_MS  340
#define WAKE_ANIM_STAGGER_MS   32
#define FADE_TOTAL_MS          320

static struct k_work_delayable idle_work;
static struct k_work_delayable bl_fade_work;
static struct k_work_delayable blank_on_work;
static struct k_work sleep_anim_work;
static struct k_work wake_anim_work;

static atomic_t sleeping = ATOMIC_INIT(0);
static int64_t last_wake_uptime_ms = 0;

static uint8_t saved_brightness = DEFAULT_SAVED_BRIGHTNESS;
static int16_t current_bl_level = DEFAULT_SAVED_BRIGHTNESS;
static int16_t target_bl_level = DEFAULT_SAVED_BRIGHTNESS;

static void prospector_display_blank_on(void) {
    if (!display_dev) {
        return;
    }

#if IS_ENABLED(CONFIG_PROSPECTOR_DISPLAY_SLEEP_USE_PM) && IS_ENABLED(CONFIG_PM_DEVICE)
    pm_device_action_run(display_dev, PM_DEVICE_ACTION_SUSPEND);
#else
    display_blanking_on(display_dev);
#endif
}

static void prospector_display_blank_off(void) {
    if (!display_dev) {
        return;
    }

#if IS_ENABLED(CONFIG_PROSPECTOR_DISPLAY_SLEEP_USE_PM) && IS_ENABLED(CONFIG_PM_DEVICE)
    pm_device_action_run(display_dev, PM_DEVICE_ACTION_RESUME);
#else
    display_blanking_off(display_dev);
#endif
}

static void anim_set_translate_x(void *obj, int32_t v) {
    lv_obj_set_style_translate_x((lv_obj_t *)obj, (lv_coord_t)v, LV_PART_MAIN);
}

static void anim_set_translate_y(void *obj, int32_t v) {
    lv_obj_set_style_translate_y((lv_obj_t *)obj, (lv_coord_t)v, LV_PART_MAIN);
}

static void get_child_anim_offsets(int index, int32_t *off_x, int32_t *off_y) {
    switch (index) {
    case 0: /* Top bar (modifiers) -> slide in/out from top */
        *off_x = 0;
        *off_y = -40;
        break;
    case 1: /* Upper-middle (WPM meter) -> slide in/out from left */
        *off_x = -140;
        *off_y = 0;
        break;
    case 2: /* Lower-middle (Layer bar) -> slide in/out from right */
        *off_x = 140;
        *off_y = 0;
        break;
    case 3: /* Bottom-left (Battery circles) -> pop up / drop down */
        *off_x = 0;
        *off_y = 75;
        break;
    default: /* Bottom-right (Output status) & others -> pop up / drop down */
        *off_x = 0;
        *off_y = 75;
        break;
    }
}

static void start_backlight_fade(uint8_t target) {
    target_bl_level = (int16_t)target;
    k_work_reschedule(&bl_fade_work, K_NO_WAIT);
}

static void bl_fade_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (!pwm_leds_dev) {
        current_bl_level = target_bl_level;
        return;
    }

    if (current_bl_level == target_bl_level) {
        led_set_brightness(pwm_leds_dev, DISP_BL, (uint8_t)current_bl_level);
        return;
    }

    int16_t span = (int16_t)saved_brightness;
    if (span < 10) {
        span = 10;
    }

    int16_t step = (span > 30) ? ((span + 15) / 30) : 1;
    int32_t tick_ms = (FADE_TOTAL_MS * step) / span;
    if (tick_ms < 10) {
        tick_ms = 10;
    } else if (tick_ms > 40) {
        tick_ms = 40;
    }

    if (current_bl_level < target_bl_level) {
        current_bl_level += step;
        if (current_bl_level > target_bl_level) {
            current_bl_level = target_bl_level;
        }
    } else {
        current_bl_level -= step;
        if (current_bl_level < target_bl_level) {
            current_bl_level = target_bl_level;
        }
    }

    led_set_brightness(pwm_leds_dev, DISP_BL, (uint8_t)current_bl_level);

    if (current_bl_level != target_bl_level) {
        k_work_reschedule(&bl_fade_work, K_MSEC(tick_ms));
    }
}

static void blank_on_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (!prospector_display_is_sleeping()) {
        return;
    }
    if (pwm_leds_dev) {
        led_set_brightness(pwm_leds_dev, DISP_BL, 0);
    }
    current_bl_level = 0;
    prospector_display_blank_on();
}

static void sleep_anim_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (!prospector_display_is_sleeping()) {
        return;
    }

    lv_obj_t *scr = lv_scr_act();
    if (!scr) {
        return;
    }
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 8; i++) {
        lv_obj_t *child = lv_obj_get_child(scr, i);
        if (!child) {
            break;
        }

        lv_anim_delete(child, anim_set_translate_x);
        lv_anim_delete(child, anim_set_translate_y);

        int32_t off_x = 0, off_y = 0;
        get_child_anim_offsets(i, &off_x, &off_y);

        if (off_x != 0) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, child);
            lv_anim_set_values(&a, 0, off_x);
            lv_anim_set_duration(&a, SLEEP_ANIM_DURATION_MS);
            lv_anim_set_delay(&a, i * SLEEP_ANIM_STAGGER_MS);
            lv_anim_set_exec_cb(&a, anim_set_translate_x);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
            lv_anim_start(&a);
        }

        if (off_y != 0) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, child);
            lv_anim_set_values(&a, 0, off_y);
            lv_anim_set_duration(&a, SLEEP_ANIM_DURATION_MS);
            lv_anim_set_delay(&a, i * SLEEP_ANIM_STAGGER_MS);
            lv_anim_set_exec_cb(&a, anim_set_translate_y);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
            lv_anim_start(&a);
        }
    }
}

static void wake_anim_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (prospector_display_is_sleeping()) {
        return;
    }

    lv_obj_t *scr = lv_scr_act();
    if (scr) {
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

        for (int i = 0; i < 8; i++) {
            lv_obj_t *child = lv_obj_get_child(scr, i);
            if (!child) {
                break;
            }

            lv_anim_delete(child, anim_set_translate_x);
            lv_anim_delete(child, anim_set_translate_y);

            int32_t off_x = 0, off_y = 0;
            get_child_anim_offsets(i, &off_x, &off_y);

            lv_obj_set_style_translate_x(child, (lv_coord_t)off_x, LV_PART_MAIN);
            lv_obj_set_style_translate_y(child, (lv_coord_t)off_y, LV_PART_MAIN);

            if (off_x != 0) {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, child);
                lv_anim_set_values(&a, off_x, 0);
                lv_anim_set_duration(&a, WAKE_ANIM_DURATION_MS);
                lv_anim_set_delay(&a, i * WAKE_ANIM_STAGGER_MS);
                lv_anim_set_exec_cb(&a, anim_set_translate_x);
                lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
                lv_anim_start(&a);
            }

            if (off_y != 0) {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, child);
                lv_anim_set_values(&a, off_y, 0);
                lv_anim_set_duration(&a, WAKE_ANIM_DURATION_MS);
                lv_anim_set_delay(&a, i * WAKE_ANIM_STAGGER_MS);
                lv_anim_set_exec_cb(&a, anim_set_translate_y);
                lv_anim_set_path_cb(&a, lv_anim_path_overshoot);
                lv_anim_start(&a);
            }
        }

        lv_refr_now(NULL);
    }

    prospector_display_blank_off();
    start_backlight_fade(saved_brightness);
}

void prospector_display_set_saved_brightness(uint8_t brightness) {
    if (brightness > 0) {
        saved_brightness = brightness;
    }
    if (!prospector_display_is_sleeping()) {
        current_bl_level = (int16_t)saved_brightness;
        target_bl_level = (int16_t)saved_brightness;
    }
}

void prospector_display_set_sleeping(bool s) {
    atomic_set(&sleeping, s ? 1 : 0);
}

bool prospector_display_is_sleeping(void) {
    return atomic_get(&sleeping) != 0;
}

bool prospector_display_just_woke(void) {
    if (last_wake_uptime_ms == 0) {
        return false;
    }
    return (k_uptime_get() - last_wake_uptime_ms) < 250;
}

void prospector_display_notify_activity(void) {
#if IS_ENABLED(CONFIG_PROSPECTOR_DISPLAY_SLEEP_ENABLE)
    k_work_reschedule(&idle_work, K_MSEC(CONFIG_PROSPECTOR_DISPLAY_IDLE_TIMEOUT_MS));
#endif
}

static void display_go_to_sleep(void) {
    if (prospector_display_is_sleeping()) {
        return;
    }

    uint8_t cur = prospector_get_brightness();
    if (cur > 0) {
        saved_brightness = cur;
    }

    prospector_display_set_sleeping(true);

    if (zmk_display_work_q()) {
        k_work_submit_to_queue(zmk_display_work_q(), &sleep_anim_work);
    }

    start_backlight_fade(0);
    k_work_reschedule(&blank_on_work, K_MSEC(SLEEP_TOTAL_MS));
}

void prospector_display_wake_up(void) {
    prospector_display_notify_activity();

    if (!prospector_display_is_sleeping()) {
        return;
    }

    last_wake_uptime_ms = k_uptime_get();
    prospector_display_set_sleeping(false);
    k_work_cancel_delayable(&blank_on_work);

    if (zmk_display_work_q()) {
        k_work_submit_to_queue(zmk_display_work_q(), &wake_anim_work);
    } else {
        prospector_display_blank_off();
        start_backlight_fade(saved_brightness);
    }
}

static void idle_work_handler(struct k_work *work) {
    ARG_UNUSED(work);
    if (!IS_ENABLED(CONFIG_PROSPECTOR_DISPLAY_SLEEP_ENABLE)) {
        return;
    }
    display_go_to_sleep();
}

static int display_idle_init(const struct device *unused) {
    ARG_UNUSED(unused);

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        return 0;
    }

    pwm_leds_dev = DEVICE_DT_GET_ONE(pwm_leds);
    if (!device_is_ready(pwm_leds_dev)) {
        pwm_leds_dev = NULL;
    }

    uint8_t initial = prospector_get_brightness();
    if (initial > 0) {
        saved_brightness = initial;
        current_bl_level = initial;
        target_bl_level = initial;
    }

    k_work_init_delayable(&idle_work, idle_work_handler);
    k_work_init_delayable(&bl_fade_work, bl_fade_work_handler);
    k_work_init_delayable(&blank_on_work, blank_on_work_handler);
    k_work_init(&sleep_anim_work, sleep_anim_work_handler);
    k_work_init(&wake_anim_work, wake_anim_work_handler);

#if IS_ENABLED(CONFIG_PROSPECTOR_DISPLAY_SLEEP_ENABLE)
    k_work_schedule(&idle_work, K_MSEC(CONFIG_PROSPECTOR_DISPLAY_IDLE_TIMEOUT_MS));
#endif

    return 0;
}
SYS_INIT(display_idle_init, APPLICATION, 70);

static int on_any_key_event(const zmk_event_t *eh) {
    if (!IS_ENABLED(CONFIG_PROSPECTOR_DISPLAY_SLEEP_ENABLE)) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_keycode_state_changed *kc = as_zmk_keycode_state_changed(eh);
    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);

    bool active = false;
    if (kc) {
        active = kc->state;
    } else if (pos) {
        active = pos->state;
    }
#if IS_ENABLED(CONFIG_ZMK_KEYMAP_SENSORS)
    else if (as_zmk_sensor_event(eh) != NULL) {
        active = true;
    }
#endif

    if (!active) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    prospector_display_wake_up();

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(prospector_display_idle, on_any_key_event);
ZMK_SUBSCRIPTION(prospector_display_idle, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(prospector_display_idle, zmk_position_state_changed);
#if IS_ENABLED(CONFIG_ZMK_KEYMAP_SENSORS)
ZMK_SUBSCRIPTION(prospector_display_idle, zmk_sensor_event);
#endif
