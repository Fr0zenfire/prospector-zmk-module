/*
 * Prospector display power state helper
 */
#pragma once

#include <stdbool.h>

void prospector_display_set_sleeping(bool sleeping);
bool prospector_display_is_sleeping(void);
void prospector_display_wake_up(void);
