/*
 * Prospector display power state helper
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

void prospector_display_set_sleeping(bool sleeping);
bool prospector_display_is_sleeping(void);
bool prospector_display_just_woke(void);
void prospector_display_wake_up(void);
void prospector_display_notify_activity(void);
void prospector_display_set_saved_brightness(uint8_t brightness);
