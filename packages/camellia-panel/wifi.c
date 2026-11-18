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
			 strstr(nets[i].sec, "802.1X") ? "   enterprise" :
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

/* ---- network form: password, hidden networks, WPA Enterprise ---- */

enum sec { SEC_OPEN, SEC_WEP, SEC_WPA, SEC_SAE, SEC_EAP };
enum eap { EAP_PEAP, EAP_TTLS, EAP_TLS, EAP_PWD };

static const char *const eap_names[] = { "peap", "ttls", "tls", "pwd" };
static const char *const phase2_names[] = { "mschapv2", "pap", "mschap", "gtc" };

static struct {
	lv_obj_t *overlay, *panel, *kb;
	lv_obj_t *ssid, *sec, *eap, *phase2, *ident, *anon, *pw, *ca, *domain;
	lv_obj_t *cert, *key, *keypw, *show;
	int hidden;    /* SSID and security entered by hand */
	int fixed_sec; /* security of a visible network, when !hidden */
} f;

/* a command sequence run one after another; argv strings are owned copies */
#define MAX_STEPS 3
#define MAX_ARGS 48
struct chain {
	char *argv[MAX_STEPS][MAX_ARGS];
	int n, cur, ignore_first;
};

static void chain_free(struct chain *c)
{
	for (int i = 0; i < c->n; i++)
		for (int a = 0; c->argv[i][a]; a++)
			free(c->argv[i][a]);
	free(c);
}

static void chain_step(int status, const char *out, void *user)
{
	struct chain *c = user;

	if (c->cur >= 0 && status != 0 && !(c->cur == 0 && c->ignore_first)) {
		action_done(status, out, (void *)"Connecting");
		chain_free(c);
		return;
	}
	if (++c->cur == c->n) {
		action_done(status, out, (void *)"Connecting");
		chain_free(c);
		return;
	}
	if (exec_async((const char *const *)c->argv[c->cur], chain_step, c)) {
		ui_set_status(status_label, 0, "cannot run nmcli");
		chain_free(c);
	}
}

static void add(char **argv, int *n, const char *v)
{
	if (*n < MAX_ARGS - 1)
		argv[(*n)++] = strdup(v);
	argv[*n] = NULL;
}

static const char *txt(lv_obj_t *ta)
{
	return lv_textarea_get_text(ta);
}

static int form_sec(void)
{
	return f.hidden ? (int)lv_dropdown_get_selected(f.sec) : f.fixed_sec;
}

static void form_close(void)
{
	if (f.overlay) {
		lv_obj_delete_async(f.overlay);
		f.overlay = NULL;
	}
}

static void form_connect(void)
{
	int sec = form_sec(), eap = lv_dropdown_get_selected(f.eap), n = 0;
	struct chain *c;
	char **a;
	char msg[160];

	if (f.hidden)
		snprintf(sel.ssid, sizeof(sel.ssid), "%s", txt(f.ssid));
	if (!sel.ssid[0]) {
		ui_set_status(status_label, 0, "Enter the network name (SSID)");
		return;
	}

	/* visible WPA/WPA2/WPA3 Personal or WEP: NetworkManager picks the key management */
	if (!f.hidden && sec != SEC_EAP) {
		const char *argv[] = { "nmcli", "device", "wifi", "connect", sel.ssid, "password",
				       txt(f.pw), "ifname", "wlan0", NULL };
		run("Connecting", argv); /* argv is copied by fork before this returns */
		form_close();
		return;
	}

	/* otherwise: replace any profile of that name, add one, bring it up */
	c = calloc(1, sizeof(*c));
	c->n = 3;
	c->cur = -1;
	c->ignore_first = 1;
	a = c->argv[0];
	n = 0;
	add(a, &n, "nmcli"); add(a, &n, "connection"); add(a, &n, "delete");
	add(a, &n, "id"); add(a, &n, sel.ssid);

	a = c->argv[1];
	n = 0;
	add(a, &n, "nmcli"); add(a, &n, "connection"); add(a, &n, "add");
	add(a, &n, "type"); add(a, &n, "wifi"); add(a, &n, "ifname"); add(a, &n, "wlan0");
	add(a, &n, "con-name"); add(a, &n, sel.ssid); add(a, &n, "ssid"); add(a, &n, sel.ssid);
	if (f.hidden) {
		add(a, &n, "802-11-wireless.hidden"); add(a, &n, "yes");
	}
	switch (sec) {
	case SEC_WEP:
		add(a, &n, "wifi-sec.key-mgmt"); add(a, &n, "none");
		add(a, &n, "wifi-sec.wep-key-type"); add(a, &n, "key");
		add(a, &n, "wifi-sec.wep-key0"); add(a, &n, txt(f.pw));
		break;
	case SEC_WPA:
	case SEC_SAE:
		add(a, &n, "wifi-sec.key-mgmt"); add(a, &n, sec == SEC_SAE ? "sae" : "wpa-psk");
		add(a, &n, "wifi-sec.psk"); add(a, &n, txt(f.pw));
		break;
	case SEC_EAP:
		add(a, &n, "wifi-sec.key-mgmt"); add(a, &n, "wpa-eap");
		add(a, &n, "802-1x.eap"); add(a, &n, eap_names[eap]);
		add(a, &n, "802-1x.identity"); add(a, &n, txt(f.ident));
		if (eap == EAP_PEAP || eap == EAP_TTLS) {
			add(a, &n, "802-1x.phase2-auth");
			add(a, &n, phase2_names[lv_dropdown_get_selected(f.phase2)]);
			if (txt(f.anon)[0]) {
				add(a, &n, "802-1x.anonymous-identity"); add(a, &n, txt(f.anon));
			}
		}
		if (eap == EAP_TLS) {
			add(a, &n, "802-1x.client-cert"); add(a, &n, txt(f.cert));
			add(a, &n, "802-1x.private-key"); add(a, &n, txt(f.key));
			add(a, &n, "802-1x.private-key-password"); add(a, &n, txt(f.keypw));
		} else {
			add(a, &n, "802-1x.password"); add(a, &n, txt(f.pw));
		}
		if (lv_dropdown_get_selected(f.ca) == 0) {
			add(a, &n, "802-1x.system-ca-certs"); add(a, &n, "yes");
			if (txt(f.domain)[0]) {
				add(a, &n, "802-1x.domain-suffix-match"); add(a, &n, txt(f.domain));
			}
		}
		break;
	}

	a = c->argv[2];
	n = 0;
	add(a, &n, "nmcli"); add(a, &n, "connection"); add(a, &n, "up");
	add(a, &n, "id"); add(a, &n, sel.ssid);

	form_close();
	snprintf(msg, sizeof(msg), "Connecting %s...", sel.ssid);
	ui_set_status(status_label, 1, msg);
	chain_step(0, "", c);
}

static void show_if(lv_obj_t *w, int on)
{
	lv_obj_t *row = lv_obj_get_parent(w);

	if (on)
		lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
	else
		lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
}

static void form_update(void)
{
	int sec = form_sec(), eap = lv_dropdown_get_selected(f.eap);
	int ent = sec == SEC_EAP, inner = ent && (eap == EAP_PEAP || eap == EAP_TTLS);

	show_if(f.ssid, f.hidden);
	show_if(f.sec, f.hidden);
	show_if(f.eap, ent);
	show_if(f.phase2, inner);
	show_if(f.ident, ent);
	show_if(f.anon, inner);
	show_if(f.pw, sec != SEC_OPEN && !(ent && eap == EAP_TLS));
	show_if(f.cert, ent && eap == EAP_TLS);
	show_if(f.key, ent && eap == EAP_TLS);
	show_if(f.keypw, ent && eap == EAP_TLS);
	show_if(f.ca, ent);
	show_if(f.domain, ent && lv_dropdown_get_selected(f.ca) == 0);
	show_if(f.show, sec != SEC_OPEN);
}

static void kb_show(lv_obj_t *ta)
{
	lv_keyboard_set_textarea(f.kb, ta);
	lv_obj_remove_flag(f.kb, LV_OBJ_FLAG_HIDDEN);
	lv_obj_update_layout(f.overlay);
	lv_obj_set_height(f.panel, lv_obj_get_y(f.kb) - lv_obj_get_y(f.panel) - 30);
	lv_obj_update_layout(f.panel);
	lv_obj_scroll_to_view(lv_obj_get_parent(ta), LV_ANIM_OFF);
}

static void kb_hide(void)
{
	lv_obj_add_flag(f.kb, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_height(f.panel, LV_SIZE_CONTENT);
}

static void ta_cb(lv_event_t *e)
{
	kb_show(lv_event_get_target(e));
}

static void kb_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
		kb_hide();
}

static void dd_cb(lv_event_t *e)
{
	(void)e;
	form_update();
}

static void show_cb(lv_event_t *e)
{
	int show = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);

	lv_textarea_set_password_mode(f.pw, !show);
	lv_textarea_set_password_mode(f.keypw, !show);
}

static void btn_cb(lv_event_t *e)
{
	if (lv_event_get_user_data(e))
		form_connect();
	else
		form_close();
}

static lv_obj_t *new_row(void)
{
	lv_obj_t *row = lv_obj_create(f.panel);

	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
	return row;
}

/* a labelled row; returns the widget, the row is its parent */
static lv_obj_t *form_row(const char *label, lv_obj_t *(*create)(lv_obj_t *))
{
	lv_obj_t *row = new_row(), *l, *w;

	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(row, 10, 0);
	l = lv_label_create(row);
	lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(l, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
	lv_label_set_text(l, label);
	w = create(row);
	lv_obj_set_width(w, lv_pct(100));
	return w;
}

static lv_obj_t *text_row(const char *label, const char *hint, int password)
{
	lv_obj_t *ta = form_row(label, lv_textarea_create);

	lv_textarea_set_one_line(ta, true);
	lv_textarea_set_password_mode(ta, password);
	lv_textarea_set_placeholder_text(ta, hint);
	lv_obj_add_event_cb(ta, ta_cb, LV_EVENT_FOCUSED, NULL);
	return ta;
}

static lv_obj_t *drop_row(const char *label, const char *options)
{
	lv_obj_t *dd = form_row(label, lv_dropdown_create);

	lv_dropdown_set_options(dd, options);
	lv_obj_add_event_cb(dd, dd_cb, LV_EVENT_VALUE_CHANGED, NULL);
	return dd;
}

/* hidden: SSID and security by hand; otherwise sel.ssid with security sec */
static void open_form(int hidden, int sec)
{
	lv_obj_t *l, *row;

	memset(&f, 0, sizeof(f));
	f.hidden = hidden;
	f.fixed_sec = sec;

	f.overlay = lv_obj_create(lv_layer_top());
	lv_obj_remove_style_all(f.overlay);
	lv_obj_set_size(f.overlay, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(f.overlay, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(f.overlay, LV_OPA_80, 0);
	lv_obj_add_flag(f.overlay, LV_OBJ_FLAG_CLICKABLE);

	f.panel = lv_obj_create(f.overlay);
	lv_obj_set_width(f.panel, lv_pct(92));
	lv_obj_set_style_max_height(f.panel, 2400 - UI_TOP_BAR - 140, 0);
	lv_obj_align(f.panel, LV_ALIGN_TOP_MID, 0, UI_TOP_BAR + 40);
	lv_obj_set_flex_flow(f.panel, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(f.panel, 36, 0);
	lv_obj_set_style_pad_row(f.panel, 26, 0);

	l = lv_label_create(f.panel);
	lv_obj_set_width(l, lv_pct(100));
	lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
	if (hidden)
		lv_label_set_text(l, "Add a hidden network");
	else
		lv_label_set_text_fmt(l, "Connect to %s", sel.ssid);

	f.ssid = text_row("Network name (SSID)", "SSID", 0);
	f.sec = drop_row("Security", "None (open)\nWEP\nWPA/WPA2 Personal\nWPA3 Personal\n"
				     "WPA/WPA2/WPA3 Enterprise");
	lv_dropdown_set_selected(f.sec, SEC_WPA);
	f.eap = drop_row("EAP method", "PEAP\nTTLS\nTLS\nPWD");
	f.phase2 = drop_row("Phase 2 authentication", "MSCHAPv2\nPAP\nMSCHAP\nGTC");
	f.ident = text_row("Identity", "user@example.org", 0);
	f.anon = text_row("Anonymous identity (optional)", "anonymous@example.org", 0);
	f.pw = text_row("Password", "Password", 1);
	f.cert = text_row("Client certificate (path)", "/home/user/client.pem", 0);
	f.key = text_row("Private key (path)", "/home/user/client.key", 0);
	f.keypw = text_row("Private key password", "Key password", 1);
	f.ca = drop_row("CA certificate", "System certificates\nDo not validate");
	f.domain = text_row("Domain (server certificate)", "example.org", 0);

	row = new_row(); /* own row, so show_if() can hide it like the others */
	f.show = lv_checkbox_create(row);
	lv_checkbox_set_text(f.show, "Show passwords");
	lv_obj_add_event_cb(f.show, show_cb, LV_EVENT_VALUE_CHANGED, NULL);

	row = new_row();
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	ui_button(row, "Cancel", btn_cb, NULL);
	ui_button(row, "Connect", btn_cb, (void *)1);

	f.kb = lv_keyboard_create(f.overlay);
	lv_obj_set_size(f.kb, lv_pct(96), 860);
	lv_obj_align(f.kb, LV_ALIGN_BOTTOM_MID, 0, -50); /* clear of the rounded corners */
	lv_obj_set_style_text_font(f.kb, &lv_font_montserrat_36, 0);
	lv_obj_add_event_cb(f.kb, kb_cb, LV_EVENT_ALL, NULL);

	kb_hide();
	form_update();
	/* a visible network that only needs a password: start typing right away */
	if (!hidden && sec != SEC_EAP) {
		lv_obj_add_state(f.pw, LV_STATE_FOCUSED);
		kb_show(f.pw);
	}
}

static void hidden_cb(lv_event_t *e)
{
	(void)e;
	memset(&sel, 0, sizeof(sel));
	open_form(1, SEC_WPA);
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
	} else if (strstr(sel.sec, "802.1X")) {
		open_form(0, SEC_EAP);
	} else if (sel.sec[0]) {
		open_form(0, strstr(sel.sec, "WEP") ? SEC_WEP : SEC_WPA);
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

	row = lv_obj_create(c);
	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	scan_btn = ui_button(row, LV_SYMBOL_REFRESH " Scan", scan_cb, NULL);
	ui_button(row, LV_SYMBOL_PLUS " Hidden network", hidden_cb, NULL);

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
