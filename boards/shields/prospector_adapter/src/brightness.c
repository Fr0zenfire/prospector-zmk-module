#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/led.h>
#include <zephyr/sys/printk.h>
#include <stdlib.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(als, 4);

#include <brightness.h>
#include "prospector/display_power.h"

static const struct device *pwm_leds_dev = DEVICE_DT_GET_ONE(pwm_leds);
#define DISP_BL DT_NODE_CHILD_IDX(DT_NODELABEL(disp_bl))

#if defined(CONFIG_PROSPECTOR_BRIGHTNESS_AUTO)

static uint8_t current_brightness = 100;
static uint8_t pre_sleep_als_brightness = 80;

#define SENSOR_MIN      0       // Minimum sensor reading
#define SENSOR_MAX      100     // Maximum sensor reading
#define PWM_MIN         15      // Minimum PWM duty cycle (%) - keep display visible even in dark
#define PWM_MAX         100     // Maximum PWM duty cycle (%)

#define FADE_STEP                        1
#define FADE_SLEEP_BRIGHTEN_MS           3
#define FADE_SLEEP_DARKEN_MS             10
#define FADE_THRESHOLD                   10

#define NORMAL_SAMPLE_SLEEP_MS           100

#define BURST_SAMPLE_SLEEP_MS            30
#define BURST_SAMPLE_TIMEOUT             10
#define BURST_SAMPLE_CONSECUTIVE         3

uint8_t map_light_to_pwm(int32_t sensor_reading) {
    if (sensor_reading < SENSOR_MIN) {
        return PWM_MIN;
    }
    if (sensor_reading > SENSOR_MAX) {
        sensor_reading = SENSOR_MAX;
    }

    uint8_t pwm_value = (uint8_t)(
        PWM_MIN + ((PWM_MAX - PWM_MIN) *
        (sensor_reading - SENSOR_MIN)) / (SENSOR_MAX - SENSOR_MIN)
    );

    return pwm_value;
}

uint8_t bl_fade(uint8_t source, uint8_t target) {
    bool increasing = target > source;

    while ((increasing && current_brightness < target) ||
           (!increasing && current_brightness > target)) {
        if (prospector_display_is_sleeping()) {
            break;
        }

        if (led_set_brightness(pwm_leds_dev, DISP_BL, current_brightness)) {
            LOG_ERR("Failed to set brightness");
        }

        current_brightness += increasing ? FADE_STEP : -FADE_STEP;

        if (current_brightness > 100) {
            current_brightness = 100;
        } else if (current_brightness < 1) {
            current_brightness = 1;
        }

        k_msleep(increasing ? FADE_SLEEP_BRIGHTEN_MS : FADE_SLEEP_DARKEN_MS);
    }

    return 0;
}

uint8_t prospector_get_brightness(void) {
    return (current_brightness > 0) ? current_brightness : pre_sleep_als_brightness;
}

void prospector_set_brightness(uint8_t val) {
    if (val < 1) val = 1;
    if (val > 100) val = 100;
    current_brightness = val;
    pre_sleep_als_brightness = val;
}

void prospector_adjust_brightness(int delta) {
    ARG_UNUSED(delta);
}

extern void als_thread(void *d0, void *d1, void *d2) {
    ARG_UNUSED(d0);
    ARG_UNUSED(d1);
    ARG_UNUSED(d2);

    const struct device *dev;
    struct sensor_value intensity;
    uint8_t mapped_brightness;

    dev = DEVICE_DT_GET_ONE(avago_apds9960);
    if (!device_is_ready(dev)) {
        printk("sensor: device not ready.\n");
    }

    while (1) {
        if (prospector_display_is_sleeping()) {
            if (current_brightness != 0) {
                pre_sleep_als_brightness = current_brightness;
                current_brightness = 0;
            }
            k_msleep(NORMAL_SAMPLE_SLEEP_MS);
            continue;
        }

        if (current_brightness == 0) {
            current_brightness = pre_sleep_als_brightness;
        }

        k_msleep(NORMAL_SAMPLE_SLEEP_MS);

        if (sensor_sample_fetch(dev)) {
            LOG_ERR("sensor_sample fetch failed\n");
        }

        if (sensor_channel_get(dev, SENSOR_CHAN_LIGHT, &intensity)) {
            LOG_ERR("Cannot read ALS data.\n");
        }

        mapped_brightness = map_light_to_pwm(intensity.val1);

        if (abs(mapped_brightness - current_brightness) > FADE_THRESHOLD) {
            uint8_t integrator = 0;

            for (int i = 0; i < BURST_SAMPLE_TIMEOUT; i++) {
                if (prospector_display_is_sleeping()) {
                    break;
                }
                k_msleep(BURST_SAMPLE_SLEEP_MS);

                if (sensor_sample_fetch(dev)) {
                    LOG_ERR("sensor_sample fetch failed\n");
                }
                if (sensor_channel_get(dev, SENSOR_CHAN_LIGHT, &intensity)) {
                    LOG_ERR("Cannot read ALS data.\n");
                }

                mapped_brightness = map_light_to_pwm(intensity.val1);

                if (abs(mapped_brightness - current_brightness) > FADE_THRESHOLD) {
                    integrator++;
                    if (integrator >= BURST_SAMPLE_CONSECUTIVE) {
                        bl_fade(current_brightness, mapped_brightness);
                        current_brightness = mapped_brightness;
                        pre_sleep_als_brightness = mapped_brightness;
                        prospector_display_set_saved_brightness(mapped_brightness);
                        break;
                    }
                }
            }
        }
    }
}

K_THREAD_DEFINE(als_tid, 1024, als_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
                0);

#elif defined(CONFIG_PROSPECTOR_BRIGHTNESS_MANUAL)

static uint8_t manual_brightness = CONFIG_PROSPECTOR_FIXED_BRIGHTNESS;

void prospector_set_brightness(uint8_t val) {
    if (val < 1) val = 1;
    if (val > 100) val = 100;
    manual_brightness = val;
    prospector_display_set_saved_brightness(manual_brightness);
    if (!prospector_display_is_sleeping()) {
        led_set_brightness(pwm_leds_dev, DISP_BL, manual_brightness);
    }
}

void prospector_adjust_brightness(int delta) {
    if (prospector_display_is_sleeping()) {
        prospector_display_wake_up();
        return;
    }
    if (prospector_display_just_woke()) {
        /* First detent woke the screen; preserve pre-sleep brightness */
        return;
    }

    int new_val = (int)manual_brightness + delta;
    if (new_val < 1) new_val = 1;
    if (new_val > 100) new_val = 100;
    manual_brightness = (uint8_t)new_val;
    prospector_display_set_saved_brightness(manual_brightness);
    prospector_display_notify_activity();
    led_set_brightness(pwm_leds_dev, DISP_BL, manual_brightness);
}

uint8_t prospector_get_brightness(void) {
    return manual_brightness;
}

static int init_manual_brightness(void) {
    prospector_display_set_saved_brightness(manual_brightness);
    led_set_brightness(pwm_leds_dev, DISP_BL, manual_brightness);
    return 0;
}

SYS_INIT(init_manual_brightness, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#else /* CONFIG_PROSPECTOR_BRIGHTNESS_FIXED */

uint8_t prospector_get_brightness(void) {
    return CONFIG_PROSPECTOR_FIXED_BRIGHTNESS;
}

void prospector_set_brightness(uint8_t val) {
    ARG_UNUSED(val);
}

void prospector_adjust_brightness(int delta) {
    ARG_UNUSED(delta);
}

static int init_fixed_brightness(void) {
    prospector_display_set_saved_brightness(CONFIG_PROSPECTOR_FIXED_BRIGHTNESS);
    led_set_brightness(pwm_leds_dev, DISP_BL, CONFIG_PROSPECTOR_FIXED_BRIGHTNESS);
    return 0;
}

SYS_INIT(init_fixed_brightness, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif
