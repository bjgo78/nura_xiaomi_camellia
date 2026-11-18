/* Wi-Fi tab: scan, pick a network, enter the password, connect; saved networks
 * can be connected or forgotten, the current one disconnected. Everything goes
 * through nmcli (NetworkManager), on wlan0 only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exec.h"
#include "ui.h"

#define MAX_NETS 48

struct net {
	char ssid[96];
	char sec[48];
	int signal;
	int inuse;
	int saved;
};

static struct net nets[MAX_NETS];
static int n_nets;
static struct net sel;

static lv_obj_t *cur_label, *status_label, *list, *scan_btn;
static lv_obj_t *pw_overlay, *pw_ta;

static void scan(int rescan);

/* nmcli -t: fields separated by ':', a literal ':' or '\' is escaped with '\' */
static int split_terse(char *line, char **f, int max)
{
	int n = 0;
	char *w = line, *r = line;

	f[n++] = w;
	while (*r && *r != '\n') {
		if (*r == '\\' && r[1]) {
			*w++ = r[1];
			r += 2;
		} else if (*r == ':' && n < max) {
			*w++ = 0;
			r++;
			f[n++] = w;
		} else {
			*w++ = *r++;
		}
	}
	*w = 0;
	return n;
}

static int net_cmp(const void *a, const void *b)
{
	const struct net *x = a, *y = b;

	if (x->inuse != y->inuse)
		return y->inuse - x->inuse;
	if (x->saved != y->saved)
		return y->saved - x->saved;
	return y->signal - x->signal;
}

static void item_cb(lv_event_t *e);

static void rebuild_list(void)
{
	const char *cur = NULL;

	lv_obj_clean(list);
	qsort(nets, n_nets, sizeof(nets[0]), net_cmp);
	for (int i = 0; i < n_nets; i++) {
		char txt[256];
		lv_obj_t *btn;

		if (nets[i].inuse)
			cur = nets[i].ssid;
		snprintf(txt, sizeof(txt), "%.95s\n%d %%%s%s%s", nets[i].ssid, nets[i].signal,
			 nets[i].sec[0] ? "   secured" : "   open",
			 nets[i].saved ? "   saved" : "", nets[i].inuse ? "   connected" : "");
		btn = lv_list_add_button(list, nets[i].sec[0] ? LV_SYMBOL_WIFI : LV_SYMBOL_WARNING, txt);
		lv_obj_set_style_pad_ver(btn, 24, 0);
		if (nets[i].inuse)
			lv_obj_set_style_text_color(btn, lv_palette_main(LV_PALETTE_BLUE), 0);
		lv_obj_add_event_cb(btn, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
	}
	if (!n_nets)
		lv_list_add_text(list, "No networks found");
	lv_label_set_text_fmt(cur_label, "Connected: %s", cur ? cur : "-");
}

static void saved_cb(int status, const char *out, void *user)
{
	char *copy = strdup(out), *line, *save = NULL;

	(void)status;
	(void)user;
	for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char *f[2];
		if (split_terse(line, f, 2) == 2 && !strcmp(f[1], "802-11-wireless"))
			for (int i = 0; i < n_nets; i++)
				if (!strcmp(nets[i].ssid, f[0]))
					nets[i].saved = 1;
	}
	free(copy);
	rebuild_list();
	lv_obj_remove_state(scan_btn, LV_STATE_DISABLED);
	if (!strcmp(lv_label_get_text(status_label), "Scanning..."))
		ui_set_status(status_label, 1, "");
}

static void list_cb(int status, const char *out, void *user)
{
	static const char *const argv[] = { "nmcli", "-t", "-f", "NAME,TYPE", "connection", "show", NULL };
	char *copy = strdup(out), *line, *save = NULL;

	(void)user;
	n_nets = 0;
	if (status == 0) {
		for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
			char *f[4];
			int dup = 0;

			if (split_terse(line, f, 4) != 4 || !f[1][0])
				continue; /* hidden networks have no SSID */
			for (int i = 0; i < n_nets; i++) {
				if (!strcmp(nets[i].ssid, f[1])) { /* same SSID on several BSSs */
					if (atoi(f[2]) > nets[i].signal)
						nets[i].signal = atoi(f[2]);
					nets[i].inuse |= f[0][0] == '*';
					dup = 1;
				}
			}
			if (dup || n_nets == MAX_NETS)
				continue;
			memset(&nets[n_nets], 0, sizeof(nets[0]));
			snprintf(nets[n_nets].ssid, sizeof(nets[0].ssid), "%s", f[1]);
			snprintf(nets[n_nets].sec, sizeof(nets[0].sec), "%s", strcmp(f[3], "--") ? f[3] : "");
			nets[n_nets].signal = atoi(f[2]);
			nets[n_nets].inuse = f[0][0] == '*';
			n_nets++;
		}
	} else {
		ui_set_status(status_label, 0, "Scan failed");
	}
	free(copy);
	exec_async(argv, saved_cb, NULL);
}

static void scan(int rescan)
{
	const char *argv[] = { "nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi",
			       "list", "ifname", "wlan0", "--rescan", rescan ? "yes" : "no", NULL };

	lv_obj_add_state(scan_btn, LV_STATE_DISABLED);
	if (rescan)
		ui_set_status(status_label, 1, "Scanning...");
	exec_async(argv, list_cb, NULL);
}

static void scan_cb(lv_event_t *e)
{
	(void)e;
	scan(1);
}

/* ---- actions ---- */
static void action_done(int status, const char *out, void *user)
{
	char msg[256];
	const char *what = user;

	if (status == 0) {
		snprintf(msg, sizeof(msg), "%s: done", what);
		ui_set_status(status_label, 1, msg);
	} else {
		/* first line of nmcli's error, e.g. "Error: Connection activation failed: ..." */
		snprintf(msg, sizeof(msg), "%s failed: %.*s", what, (int)strcspn(out, "\n"), out);
		ui_set_status(status_label, 0, msg);
	}
	scan(0);
}

static void run(const char *what, const char *const argv[])
{
	char msg[160];

	snprintf(msg, sizeof(msg), "%s %s...", what, sel.ssid);
	ui_set_status(status_label, 1, msg);
	if (exec_async(argv, action_done, (void *)what))
		ui_set_status(status_label, 0, "cannot run nmcli");
}

static void do_connect_saved(void *u)
{
	const char *argv[] = { "nmcli", "connection", "up", "id", sel.ssid, NULL };
	(void)u;
	run("Connecting", argv);
}

static void do_connect_open(void *u)
{
	const char *argv[] = { "nmcli", "device", "wifi", "connect", sel.ssid, "ifname", "wlan0", NULL };
	(void)u;
	run("Connecting", argv);
}

static void do_forget(void *u)
{
	const char *argv[] = { "nmcli", "connection", "delete", "id", sel.ssid, NULL };
	(void)u;
	run("Forgetting", argv);
}

static void do_disconnect(void *u)
{
	static const char *const argv[] = { "nmcli", "device", "disconnect", "wlan0", NULL };
	(void)u;
	run("Disconnecting", argv);
}

/* ---- password entry ---- */
static void pw_close(void)
{
	if (pw_overlay) {
		lv_obj_delete_async(pw_overlay);
		pw_overlay = NULL;
	}
}

static void pw_connect(void)
{
	char pw[128];
	const char *argv[] = { "nmcli", "device", "wifi", "connect", sel.ssid, "password", pw,
			       "ifname", "wlan0", NULL };

	snprintf(pw, sizeof(pw), "%s", lv_textarea_get_text(pw_ta));
	pw_close();
	run("Connecting", argv); /* argv is copied by fork before this returns */
}

static void pw_kb_cb(lv_event_t *e)
{
	if (lv_event_get_code(e) == LV_EVENT_READY)
		pw_connect();
	else if (lv_event_get_code(e) == LV_EVENT_CANCEL)
		pw_close();
}

static void pw_btn_cb(lv_event_t *e)
{
	if (lv_event_get_user_data(e))
		pw_connect();
	else
		pw_close();
}

static void pw_show_cb(lv_event_t *e)
{
	lv_obj_t *cb = lv_event_get_target(e);

	lv_textarea_set_password_mode(pw_ta, !lv_obj_has_state(cb, LV_STATE_CHECKED));
}

static void open_password(void)
{
	lv_obj_t *panel, *l, *row, *cb, *kb;

	pw_overlay = lv_obj_create(lv_layer_top());
	lv_obj_remove_style_all(pw_overlay);
	lv_obj_set_size(pw_overlay, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(pw_overlay, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(pw_overlay, LV_OPA_80, 0);
	lv_obj_add_flag(pw_overlay, LV_OBJ_FLAG_CLICKABLE);

	panel = lv_obj_create(pw_overlay);
	lv_obj_set_size(panel, lv_pct(92), LV_SIZE_CONTENT);
	lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, UI_TOP_BAR + 40);
	lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(panel, 36, 0);
	lv_obj_set_style_pad_row(panel, 30, 0);

	l = lv_label_create(panel);
	lv_obj_set_width(l, lv_pct(100));
	lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
	lv_label_set_text_fmt(l, "Password for %s", sel.ssid);

	pw_ta = lv_textarea_create(panel);
	lv_obj_set_width(pw_ta, lv_pct(100));
	lv_textarea_set_one_line(pw_ta, true);
	lv_textarea_set_password_mode(pw_ta, true);
	lv_textarea_set_placeholder_text(pw_ta, "Password");
	lv_obj_add_state(pw_ta, LV_STATE_FOCUSED);

	cb = lv_checkbox_create(panel);
	lv_checkbox_set_text(cb, "Show password");
	lv_obj_add_event_cb(cb, pw_show_cb, LV_EVENT_VALUE_CHANGED, NULL);

	row = lv_obj_create(panel);
	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	ui_button(row, "Cancel", pw_btn_cb, NULL);
	ui_button(row, "Connect", pw_btn_cb, (void *)1);

	kb = lv_keyboard_create(pw_overlay);
	lv_obj_set_size(kb, lv_pct(96), 860);
	lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -50); /* clear of the rounded corners */
	lv_obj_set_style_text_font(kb, &lv_font_montserrat_36, 0);
	lv_keyboard_set_textarea(kb, pw_ta);
	lv_obj_add_event_cb(kb, pw_kb_cb, LV_EVENT_ALL, NULL);
}

static void item_cb(lv_event_t *e)
{
	int i = (int)(intptr_t)lv_event_get_user_data(e);
	lv_obj_t *d;

	if (i < 0 || i >= n_nets)
		return;
	sel = nets[i];
	if (sel.inuse) {
		d = ui_dialog(sel.ssid, "Connected. Disconnecting ends SSH sessions over Wi-Fi.");
		ui_dialog_button(d, "Disconnect", do_disconnect, NULL);
		ui_dialog_button(d, "Forget", do_forget, NULL);
		ui_dialog_button(d, "Close", NULL, NULL);
	} else if (sel.saved) {
		d = ui_dialog(sel.ssid, "Saved network.");
		ui_dialog_button(d, "Connect", do_connect_saved, NULL);
		ui_dialog_button(d, "Forget", do_forget, NULL);
		ui_dialog_button(d, "Cancel", NULL, NULL);
	} else if (sel.sec[0]) {
		open_password();
	} else {
		d = ui_dialog(sel.ssid, "Open network without encryption.");
		ui_dialog_button(d, "Connect", do_connect_open, NULL);
		ui_dialog_button(d, "Cancel", NULL, NULL);
	}
}

void wifi_create(lv_obj_t *tab)
{
	lv_obj_t *c, *row;

	lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(tab, 30, 0);
	lv_obj_set_style_pad_hor(tab, 30, 0);
	lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);

	c = ui_card(tab, LV_SYMBOL_WIFI "  Wi-Fi (wlan0)");
	row = lv_obj_create(c);
	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	cur_label = lv_label_create(row);
	lv_obj_set_flex_grow(cur_label, 1);
	lv_label_set_long_mode(cur_label, LV_LABEL_LONG_MODE_DOTS);
	lv_label_set_text(cur_label, "Connected: -");
	scan_btn = ui_button(row, LV_SYMBOL_REFRESH " Scan", scan_cb, NULL);

	status_label = lv_label_create(c);
	lv_obj_set_width(status_label, lv_pct(100));
	lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
	lv_obj_set_style_text_font(status_label, &lv_font_montserrat_28, 0);
	lv_label_set_text(status_label, "");

	list = lv_list_create(tab);
	lv_obj_set_width(list, lv_pct(100));
	lv_obj_set_flex_grow(list, 1);

	scan(0);
}
