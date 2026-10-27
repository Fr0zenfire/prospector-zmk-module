#pragma once

/*
 * "Track" layout palette — modelled on the GR86 / BRZ digital cluster in
 * Track mode: white tach segments on black, amber/red redline, red-orange
 * TRACK pill and grey bezel lines.
 */

#define TRK_COLOR_BG             0x000000
#define TRK_COLOR_BEZEL          0x5C5C5C
#define TRK_COLOR_TEXT           0xF2F2F2
#define TRK_COLOR_TEXT_DIM       0x8A8A8A

/* Tachometer (WPM) segments */
#define TRK_COLOR_BAR_ON         0xF0F0F0
#define TRK_COLOR_BAR_OFF        0x2E2E2E
#define TRK_COLOR_AMBER_ON       0xFFB300
#define TRK_COLOR_AMBER_OFF      0x4A3300
#define TRK_COLOR_RED_ON         0xFF3D1F
#define TRK_COLOR_RED_OFF        0x4D150B
#define TRK_COLOR_RAMP           0xE8E8E8

/* Centre bowl */
#define TRK_COLOR_PILL_TOP       0xF0573A
#define TRK_COLOR_PILL_BOTTOM    0xB8321E
#define TRK_COLOR_PILL_TEXT      0xFFFFFF

/* Side gauges (battery / output) */
#define TRK_COLOR_GAUGE_FRAME    0xD0D0D0
#define TRK_COLOR_GAUGE_FILL     0xE6E6E6
#define TRK_COLOR_GAUGE_EMPTY    0x3A3A3A
#define TRK_COLOR_GAUGE_LOW      0xFF3D1F
#define TRK_COLOR_GAUGE_OFFLINE  0x2A2A2A

/* Telltales (modifiers) */
#define TRK_COLOR_TELL_OFF       0x262626
#define TRK_COLOR_TELL_GUI       0x4FC3F7
#define TRK_COLOR_TELL_ALT       0x2EE66B
#define TRK_COLOR_TELL_CTRL      0xFFB300
#define TRK_COLOR_TELL_SHIFT     0xFF2A1A
#define TRK_COLOR_TELL_CAPS_WORD 0xFFB300

/* Link status */
#define TRK_COLOR_LINK_OK        0x2EE66B
#define TRK_COLOR_LINK_WARN      0xFFA000
#define TRK_COLOR_LINK_USB       0x4FC3F7
