/* LVGL configuration for camellia-panel (1080x2400 DSI panel, ~400 ppi).
 * Anything not set here keeps LVGL's default from lv_conf_internal.h. */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 32

#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_OS LV_OS_NONE

/* Widget sizes (padding, radius, scrollbars) scale with the DPI */
#define LV_DPI_DEF 400

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_EVDEV 1

#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_36

#define LV_THEME_DEFAULT_DARK 1

/* DejaVu Sans (font-dejavu) at run time: accented letters of the keyboard */
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_CACHE_GLYPH_CNT 256

#endif /* LV_CONF_H */
