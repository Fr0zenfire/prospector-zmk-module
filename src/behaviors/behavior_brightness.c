/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>

#if IS_ENABLED(CONFIG_SHIELD_PROSPECTOR_ADAPTER)
#include <brightness.h>
#if IS_ENABLED(CONFIG_PROSPECTOR_DISPLAY_SLEEP_ENABLE)
#include <prospector/display_power.h>
#endif
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ── Brightness Increment ─────────────────────────────────────────── */

#define DT_DRV_COMPAT zmk_behavior_brightness_inc

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_brightness_inc_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_SHIELD_PROSPECTOR_ADAPTER)
#if IS_ENABLED(CONFIG_PROSPECTOR_DISPLAY_SLEEP_ENABLE)
    if (prospector_display_is_sleeping()) {
        prospector_display_wake_up();
        return ZMK_BEHAVIOR_OPAQUE;
    }
#endif
    prospector_adjust_brightness(CONFIG_PROSPECTOR_BRIGHTNESS_STEP);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_brightness_inc_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api brightness_inc_driver_api = {
    .binding_pressed = on_brightness_inc_pressed,
    .binding_released = on_brightness_inc_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int behavior_brightness_inc_init(const struct device *dev) { return 0; }

#define BRI_INC_INST(n)                                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_brightness_inc_init, NULL, NULL, NULL, POST_KERNEL,        \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &brightness_inc_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BRI_INC_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(zmk_behavior_brightness_inc) */

/* ── Brightness Decrement ─────────────────────────────────────────── */

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT zmk_behavior_brightness_dec

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_brightness_dec_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_SHIELD_PROSPECTOR_ADAPTER)
#if IS_ENABLED(CONFIG_PROSPECTOR_DISPLAY_SLEEP_ENABLE)
    if (prospector_display_is_sleeping()) {
        prospector_display_wake_up();
        return ZMK_BEHAVIOR_OPAQUE;
    }
#endif
    prospector_adjust_brightness(-CONFIG_PROSPECTOR_BRIGHTNESS_STEP);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_brightness_dec_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api brightness_dec_driver_api = {
    .binding_pressed = on_brightness_dec_pressed,
    .binding_released = on_brightness_dec_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int behavior_brightness_dec_init(const struct device *dev) { return 0; }

#define BRI_DEC_INST(n)                                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_brightness_dec_init, NULL, NULL, NULL, POST_KERNEL,        \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &brightness_dec_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BRI_DEC_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(zmk_behavior_brightness_dec) */
