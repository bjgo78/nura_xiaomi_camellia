/* On-screen keyboard of the setup screens (Gboard-style). */
#ifndef CAMELLIA_KEYBOARD_H
#define CAMELLIA_KEYBOARD_H

#include "lvgl.h"

/* Full-width keyboard at the bottom of parent (one at a time). The done key
 * sends LV_EVENT_READY to the returned object. */
lv_obj_t *kb_create(lv_obj_t *parent);
void kb_set_textarea(lv_obj_t *kb, lv_obj_t *ta);

/* switch to the ?123 page (digits, '.', ':', '/'; for addresses) or back to
 * the letters */
void kb_set_symbols(lv_obj_t *kb, int symbols);

/* Montserrat 36 with DejaVu Sans as fallback, for text fields that can get
 * accented letters from the keyboard */
const lv_font_t *kb_text_font(void);

#endif
