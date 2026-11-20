/* Shared layout helpers and screen geometry. */
#ifndef CAMELLIA_UI_H
#define CAMELLIA_UI_H

#include "lvgl.h"

/* Panel geometry from the LineageOS device overlay: the punch-hole camera sits
 * at the top centre (x 503-577, y 0-103), the status bar is 103 px high and the
 * corners are rounded with a 104 px radius. */
#define UI_TOP_BAR      103
#define UI_CUTOUT_X1    466   /* cutout 503-577 plus a margin */
#define UI_CUTOUT_X2    614
#define UI_SIDE_PAD     60    /* keeps text out of the rounded corners */
#define UI_TAB_BAR      180

lv_obj_t *ui_card(lv_obj_t *parent, const char *title);
lv_obj_t *ui_row(lv_obj_t *card, const char *key);

void status_create(lv_obj_t *tab);
void status_update(int slow);
void wifi_create(lv_obj_t *tab);
void lan_create(lv_obj_t *tab);
void lan_update(int slow);
void battery_create(lv_obj_t *tab);
void power_create(lv_obj_t *tab);

/* modal message box; buttons added with ui_dialog_button() close it before
 * their callback runs */
typedef void (*ui_action_cb)(void *user);
lv_obj_t *ui_dialog(const char *title, const char *text);
void ui_dialog_button(lv_obj_t *dlg, const char *txt, ui_action_cb cb, void *user);
lv_obj_t *ui_button(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, void *user);
void ui_set_status(lv_obj_t *label, int ok, const char *text);

#endif
