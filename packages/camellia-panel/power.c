/* Power tab: reboot and power off, each behind a confirmation. */
#include "exec.h"
#include "ui.h"

static lv_obj_t *status_label;

static void do_power(void *user)
{
	const char *cmd = user;
	const char *const argv[] = { cmd, NULL };

	ui_set_status(status_label, 1, cmd[0] == 'r' ? "Rebooting..." : "Powering off...");
	if (exec_async(argv, NULL, NULL))
		ui_set_status(status_label, 0, "Cannot run the command");
}

static void ask_cb(lv_event_t *e)
{
	const char *cmd = lv_event_get_user_data(e);
	lv_obj_t *d;

	if (cmd[0] == 'r')
		d = ui_dialog("Reboot", "Reboot the phone now?");
	else
		d = ui_dialog("Power off", "Power off the phone now? It stays off until the power "
					   "key is held or a charger is connected.");
	ui_dialog_button(d, cmd[0] == 'r' ? "Reboot" : "Power off", do_power, (void *)cmd);
	ui_dialog_button(d, "Cancel", NULL, NULL);
}

void power_create(lv_obj_t *tab)
{
	lv_obj_t *c, *b;

	lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(tab, 30, 0);
	lv_obj_set_style_pad_hor(tab, 30, 0);

	c = ui_card(tab, LV_SYMBOL_POWER "  Power");
	lv_obj_set_style_pad_row(c, 36, 0);
	b = ui_button(c, LV_SYMBOL_REFRESH " Reboot", ask_cb, (void *)"reboot");
	lv_obj_set_width(b, lv_pct(100));
	lv_obj_set_height(b, 180);
	b = ui_button(c, LV_SYMBOL_POWER " Power off", ask_cb, (void *)"poweroff");
	lv_obj_set_width(b, lv_pct(100));
	lv_obj_set_height(b, 180);
	lv_obj_set_style_bg_color(b, lv_palette_main(LV_PALETTE_RED), 0);
	status_label = lv_label_create(c);
	lv_obj_set_style_text_font(status_label, &lv_font_montserrat_28, 0);
	lv_label_set_text(status_label, "");
}
