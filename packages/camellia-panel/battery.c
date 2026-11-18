/* Battery tab: the charge limit window of the camellia-battery service
 * (CHARGE_START / CHARGE_STOP in /etc/conf.d/camellia-battery). Save writes the
 * file and restarts the service. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "exec.h"
#include "ui.h"

#define CONF "/etc/conf.d/camellia-battery"
#define STEP 5

static int start_pct = 75, stop_pct = 80;
static lv_obj_t *start_val, *stop_val, *status_label;

static void load(void)
{
	FILE *f = fopen(CONF, "r");
	char line[256];

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "CHARGE_START=", 13))
			start_pct = atoi(line + 13 + (line[13] == '"'));
		else if (!strncmp(line, "CHARGE_STOP=", 12))
			stop_pct = atoi(line + 12 + (line[12] == '"'));
	}
	fclose(f);
}

/* rewrite only the two lines, keep comments and CHARGE_INTERVAL */
static int save(void)
{
	FILE *in = fopen(CONF, "r"), *out;
	char line[256];
	int have_start = 0, have_stop = 0;

	out = fopen(CONF ".tmp", "w");
	if (!out) {
		if (in)
			fclose(in);
		return -1;
	}
	while (in && fgets(line, sizeof(line), in)) {
		if (!strncmp(line, "CHARGE_START=", 13)) {
			fprintf(out, "CHARGE_START=%d\n", start_pct);
			have_start = 1;
		} else if (!strncmp(line, "CHARGE_STOP=", 12)) {
			fprintf(out, "CHARGE_STOP=%d\n", stop_pct);
			have_stop = 1;
		} else {
			fputs(line, out);
		}
	}
	if (!have_start)
		fprintf(out, "CHARGE_START=%d\n", start_pct);
	if (!have_stop)
		fprintf(out, "CHARGE_STOP=%d\n", stop_pct);
	if (in)
		fclose(in);
	if (fclose(out))
		return -1;
	return rename(CONF ".tmp", CONF);
}

static void show(void)
{
	lv_label_set_text_fmt(start_val, "%d %%", start_pct);
	lv_label_set_text_fmt(stop_val, "%d %%", stop_pct);
}

/* user data: 1 start+, 2 start-, 3 stop+, 4 stop- */
static void step_cb(lv_event_t *e)
{
	switch ((int)(intptr_t)lv_event_get_user_data(e)) {
	case 1: if (start_pct + STEP <= stop_pct - STEP) start_pct += STEP; break;
	case 2: if (start_pct - STEP >= 20) start_pct -= STEP; break;
	case 3: if (stop_pct + STEP <= 100) stop_pct += STEP; break;
	case 4: if (stop_pct - STEP >= start_pct + STEP) stop_pct -= STEP; break;
	}
	show();
	ui_set_status(status_label, 1, "Not saved yet");
}

static void restarted(int status, const char *out, void *user)
{
	(void)out;
	(void)user;
	ui_set_status(status_label, status == 0, status == 0 ? "Saved, limiter restarted"
							     : "Saved, but the service restart failed");
}

static void save_cb(lv_event_t *e)
{
	static const char *const argv[] = { "rc-service", "camellia-battery", "restart", NULL };

	(void)e;
	if (save()) {
		ui_set_status(status_label, 0, "Cannot write " CONF);
		return;
	}
	ui_set_status(status_label, 1, "Saving...");
	exec_async(argv, restarted, NULL);
}

static lv_obj_t *stepper(lv_obj_t *card, const char *key, intptr_t up, intptr_t down)
{
	lv_obj_t *row = lv_obj_create(card), *k, *v, *b;

	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(row, 20, 0);

	k = lv_label_create(row);
	lv_obj_set_flex_grow(k, 1);
	lv_obj_set_style_text_font(k, &lv_font_montserrat_28, 0);
	lv_label_set_text(k, key);
	b = ui_button(row, LV_SYMBOL_MINUS, step_cb, (void *)down);
	lv_obj_set_width(b, 130);
	v = lv_label_create(row);
	lv_obj_set_width(v, 150);
	lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
	b = ui_button(row, LV_SYMBOL_PLUS, step_cb, (void *)up);
	lv_obj_set_width(b, 130);
	return v;
}

void battery_create(lv_obj_t *tab)
{
	lv_obj_t *c, *l, *btn;

	lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(tab, 30, 0);
	lv_obj_set_style_pad_hor(tab, 30, 0);

	c = ui_card(tab, LV_SYMBOL_CHARGE "  Charge limit");
	lv_obj_set_style_pad_row(c, 36, 0);
	l = lv_label_create(c);
	lv_obj_set_width(l, lv_pct(100));
	lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
	lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
	lv_label_set_text(l, "On the charger, charging stops at the upper value and resumes "
			     "at the lower one; the phone keeps running from the charger.");
	start_val = stepper(c, "Resume at", 1, 2);
	stop_val = stepper(c, "Stop at", 3, 4);

	btn = ui_button(c, LV_SYMBOL_SAVE " Save", save_cb, NULL);
	lv_obj_set_width(btn, lv_pct(100));
	status_label = lv_label_create(c);
	lv_obj_set_style_text_font(status_label, &lv_font_montserrat_28, 0);
	lv_label_set_text(status_label, "");

	load();
	show();
}
