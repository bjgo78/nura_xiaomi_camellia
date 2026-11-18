/* Small shared widgets: modal dialogs, buttons, status lines. */
#include <string.h>

#include "ui.h"

struct dlg_btn {
	ui_action_cb cb;
	void *user;
};

static lv_obj_t *current;

static void dlg_btn_cb(lv_event_t *e)
{
	struct dlg_btn *b = lv_event_get_user_data(e);
	ui_action_cb cb = b->cb;
	void *user = b->user;

	if (current) {
		lv_msgbox_close_async(current);
		current = NULL;
	}
	if (cb)
		cb(user);
}

static void dlg_btn_free(lv_event_t *e)
{
	lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_dialog(const char *title, const char *text)
{
	if (current)
		lv_msgbox_close(current);
	current = lv_msgbox_create(NULL);
	lv_obj_set_width(current, lv_pct(90));
	lv_obj_set_style_pad_all(current, 30, 0);
	lv_msgbox_add_title(current, title);
	lv_msgbox_add_text(current, text);
	lv_obj_set_height(lv_msgbox_get_footer(current), 160);
	return current;
}

void ui_dialog_button(lv_obj_t *dlg, const char *txt, ui_action_cb cb, void *user)
{
	lv_obj_t *btn = lv_msgbox_add_footer_button(dlg, txt);
	struct dlg_btn *b = lv_malloc(sizeof(*b));

	b->cb = cb;
	b->user = user;
	lv_obj_set_flex_grow(btn, 1);
	lv_obj_set_height(btn, 120);
	lv_obj_add_event_cb(btn, dlg_btn_cb, LV_EVENT_CLICKED, b);
	lv_obj_add_event_cb(btn, dlg_btn_free, LV_EVENT_DELETE, b);
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *txt, lv_event_cb_t cb, void *user)
{
	lv_obj_t *btn = lv_button_create(parent);
	lv_obj_t *l = lv_label_create(btn);

	lv_obj_set_height(btn, 130);
	lv_obj_set_style_pad_hor(btn, 50, 0);
	lv_label_set_text(l, txt);
	lv_obj_center(l);
	if (cb)
		lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
	return btn;
}

void ui_set_status(lv_obj_t *label, int ok, const char *text)
{
	lv_obj_set_style_text_color(label, ok ? lv_palette_lighten(LV_PALETTE_GREY, 1)
					      : lv_palette_main(LV_PALETTE_RED), 0);
	lv_label_set_text(label, text);
}
