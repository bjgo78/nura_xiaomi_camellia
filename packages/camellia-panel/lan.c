/* LAN tab: the USB Ethernet adapter (eth0, for example a USB-C dock on OTG).
 * Shows the link and the addresses in use; Edit sets IPv4 and IPv6 (automatic,
 * manual or disabled; address/prefix, gateway, two DNS servers). The settings
 * live in one NetworkManager profile, camellia-eth, bound to eth0: saving
 * replaces it and removes NetworkManager's automatic "Wired connection"
 * profiles, so they cannot take eth0 over again. */
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "exec.h"
#include "keyboard.h"
#include "ui.h"

#define IFACE   "eth0"
#define PROFILE "camellia-eth"
#define SYSNET  "/sys/class/net/" IFACE

enum { M_AUTO, M_MANUAL, M_OFF };
static const char *const methods[] = { "auto", "manual", "disabled" };

struct ipcfg {
	int method;
	char addr[64], gw[64], dns1[64], dns2[64];
};

/* [0] IPv4, [1] IPv6: as saved in the profile, and as being saved */
static struct ipcfg cfg[2], newcfg[2];
static int have_profile, busy, dev_busy, prof_busy;

static lv_obj_t *v_link, *v_mac, *v_ip4, *v_ip6, *v_gw4, *v_gw6, *v_dns;
static lv_obj_t *v_set4, *v_set6, *status_label, *edit_btn;

static void load_profile(void);

/* ---- helpers ---- */

static int read_str(const char *path, char *buf, size_t len)
{
	FILE *f = fopen(path, "r");

	buf[0] = 0;
	if (!f)
		return -1;
	if (!fgets(buf, len, f))
		buf[0] = 0;
	fclose(f);
	buf[strcspn(buf, "\n")] = 0;
	return buf[0] ? 0 : -1;
}

static void copy(char *dst, size_t len, const char *src)
{
	while (*src == ' ')
		src++;
	snprintf(dst, len, "%s", strcmp(src, "--") ? src : "");
	dst[strcspn(dst, " ")] = 0;
}

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

static int prefix_len(const struct sockaddr *mask, int v6)
{
	const unsigned char *b = v6 ? ((const struct sockaddr_in6 *)mask)->sin6_addr.s6_addr
				    : (const unsigned char *)&((const struct sockaddr_in *)mask)->sin_addr;
	int n = 0;

	for (int i = 0; i < (v6 ? 16 : 4); i++)
		n += __builtin_popcount(b[i]);
	return n;
}

static int valid_ip(int v6, const char *s)
{
	unsigned char b[16];

	return inet_pton(v6 ? AF_INET6 : AF_INET, s, b) == 1;
}

static int valid_cidr(int v6, const char *s)
{
	const char *sl = strchr(s, '/');
	char ip[64], *end;
	long p;

	if (!sl || sl == s || (size_t)(sl - s) >= sizeof(ip) || !sl[1])
		return 0;
	memcpy(ip, s, sl - s);
	ip[sl - s] = 0;
	p = strtol(sl + 1, &end, 10);
	if (*end || p < 1 || p > (v6 ? 128 : 32))
		return 0;
	return valid_ip(v6, ip);
}

/* ---- status ---- */

static void update_link(void)
{
	char car[8], spd[16], dup[16], mac[32];

	if (access(SYSNET, F_OK)) {
		lv_label_set_text(v_link, "No adapter");
		lv_label_set_text(v_mac, "-");
		return;
	}
	read_str(SYSNET "/address", mac, sizeof(mac));
	lv_label_set_text(v_mac, mac[0] ? mac : "-");
	if (read_str(SYSNET "/carrier", car, sizeof(car))) {
		lv_label_set_text(v_link, "Down");
	} else if (car[0] != '1') {
		lv_label_set_text(v_link, "No cable");
	} else {
		read_str(SYSNET "/speed", spd, sizeof(spd));
		read_str(SYSNET "/duplex", dup, sizeof(dup));
		if (atoi(spd) > 0)
			lv_label_set_text_fmt(v_link, "Up, %d Mb/s %s duplex", atoi(spd),
					      dup[0] ? dup : "?");
		else
			lv_label_set_text(v_link, "Up");
	}
}

static void update_addrs(void)
{
	struct ifaddrs *ifa, *i;
	char v4[256] = "", v6[512] = "";

	if (getifaddrs(&ifa) == 0) {
		for (i = ifa; i; i = i->ifa_next) {
			char ip[INET6_ADDRSTRLEN];
			int is6;
			char *dst;
			size_t len;

			if (!i->ifa_addr || strcmp(i->ifa_name, IFACE))
				continue;
			is6 = i->ifa_addr->sa_family == AF_INET6;
			if (!is6 && i->ifa_addr->sa_family != AF_INET)
				continue;
			if (is6) {
				const struct in6_addr *a = &((struct sockaddr_in6 *)i->ifa_addr)->sin6_addr;
				if (IN6_IS_ADDR_LINKLOCAL(a))
					continue;
				inet_ntop(AF_INET6, a, ip, sizeof(ip));
			} else {
				inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, ip, sizeof(ip));
			}
			dst = is6 ? v6 : v4;
			len = is6 ? sizeof(v6) : sizeof(v4);
			snprintf(dst + strlen(dst), len - strlen(dst), "%s%s/%d", dst[0] ? "\n" : "", ip,
				 i->ifa_netmask ? prefix_len(i->ifa_netmask, is6) : (is6 ? 128 : 32));
		}
		freeifaddrs(ifa);
	}
	lv_label_set_text(v_ip4, v4[0] ? v4 : "-");
	lv_label_set_text(v_ip6, v6[0] ? v6 : "-");
}

/* "IP4.GATEWAY:172.17.99.1", "IP4.DNS[1]:1.1.1.1", ... */
static void device_cb(int status, const char *out, void *user)
{
	char *c = strdup(out), *line, *save = NULL;
	char gw4[64] = "", gw6[64] = "", dns[512] = "";

	(void)user;
	dev_busy = 0;
	if (status == 0) {
		for (line = strtok_r(c, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
			char *val = strchr(line, ':');

			if (!val)
				continue;
			*val++ = 0;
			if (!strcmp(line, "IP4.GATEWAY"))
				copy(gw4, sizeof(gw4), val);
			else if (!strcmp(line, "IP6.GATEWAY"))
				copy(gw6, sizeof(gw6), val);
			else if ((!strncmp(line, "IP4.DNS", 7) || !strncmp(line, "IP6.DNS", 7)) && val[0])
				snprintf(dns + strlen(dns), sizeof(dns) - strlen(dns), "%s%s",
					 dns[0] ? "\n" : "", val);
		}
	}
	free(c);
	lv_label_set_text(v_gw4, gw4[0] ? gw4 : "-");
	lv_label_set_text(v_gw6, gw6[0] ? gw6 : "-");
	lv_label_set_text(v_dns, dns[0] ? dns : "-");
}

void lan_update(int slow)
{
	static const char *const argv[] = { "nmcli", "--escape", "no", "-t", "-f",
		"IP4.GATEWAY,IP4.DNS,IP6.GATEWAY,IP6.DNS", "device", "show", IFACE, NULL };

	if (!v_link)
		return;
	update_link();
	update_addrs();
	if (!slow)
		return;
	if (!dev_busy && !access(SYSNET, F_OK)) {
		dev_busy = 1;
		if (exec_async(argv, device_cb, NULL))
			dev_busy = 0;
	} else if (access(SYSNET, F_OK)) {
		lv_label_set_text(v_gw4, "-");
		lv_label_set_text(v_gw6, "-");
		lv_label_set_text(v_dns, "-");
	}
	if (!busy)
		load_profile();
}

/* ---- saved settings (the camellia-eth profile) ---- */

static void show_one(lv_obj_t *label, const struct ipcfg *c, int v6)
{
	char buf[320];

	if (!have_profile) {
		lv_label_set_text(label, "Automatic (default)");
		return;
	}
	switch (c->method) {
	case M_MANUAL:
		snprintf(buf, sizeof(buf), "Manual\n%s", c->addr);
		if (c->gw[0])
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nvia %s", c->gw);
		break;
	case M_OFF:
		snprintf(buf, sizeof(buf), "Disabled");
		break;
	default:
		snprintf(buf, sizeof(buf), "%s", v6 ? "Automatic" : "Automatic (DHCP)");
		break;
	}
	if (c->method != M_OFF && c->dns1[0])
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "\nDNS %s%s%s", c->dns1,
			 c->dns2[0] ? "\nDNS " : "", c->dns2);
	lv_label_set_text(label, buf);
}

static void show_settings(void)
{
	show_one(v_set4, &cfg[0], 0);
	show_one(v_set6, &cfg[1], 1);
}

/* "ipv4.method:manual", "ipv4.addresses:192.168.1.10/24", "ipv4.dns:1.1.1.1,8.8.8.8" */
static void profile_cb(int status, const char *out, void *user)
{
	char *c = strdup(out), *line, *save = NULL;

	(void)user;
	prof_busy = 0;
	if (busy) { /* a save started meanwhile; its own reload follows */
		free(c);
		return;
	}
	memset(cfg, 0, sizeof(cfg));
	have_profile = status == 0;
	for (line = strtok_r(c, "\n", &save); have_profile && line; line = strtok_r(NULL, "\n", &save)) {
		char *val = strchr(line, ':'), *key = line, *comma;
		struct ipcfg *ic;

		if (!val || (strncmp(key, "ipv4.", 5) && strncmp(key, "ipv6.", 5)))
			continue;
		*val++ = 0;
		ic = &cfg[key[3] == '6'];
		key += 5;
		if (!strcmp(key, "method")) {
			ic->method = !strcmp(val, "manual") ? M_MANUAL :
				     (!strcmp(val, "disabled") || !strcmp(val, "ignore")) ? M_OFF : M_AUTO;
		} else if (!strcmp(key, "addresses")) {
			val[strcspn(val, ",")] = 0;
			copy(ic->addr, sizeof(ic->addr), val);
		} else if (!strcmp(key, "gateway")) {
			copy(ic->gw, sizeof(ic->gw), val);
		} else if (!strcmp(key, "dns")) {
			comma = strchr(val, ',');
			if (comma) {
				*comma++ = 0;
				copy(ic->dns2, sizeof(ic->dns2), comma);
				ic->dns2[strcspn(ic->dns2, ",")] = 0;
			}
			copy(ic->dns1, sizeof(ic->dns1), val);
		}
	}
	free(c);
	show_settings();
}

static void load_profile(void)
{
	static const char *const argv[] = { "nmcli", "--escape", "no", "-t", "-f",
		"ipv4.method,ipv4.addresses,ipv4.gateway,ipv4.dns,"
		"ipv6.method,ipv6.addresses,ipv6.gateway,ipv6.dns",
		"connection", "show", "id", PROFILE, NULL };

	if (prof_busy)
		return;
	prof_busy = 1;
	if (exec_async(argv, profile_cb, NULL))
		prof_busy = 0;
}

/* ---- saving: list, delete the old profiles, add camellia-eth, bring it up ---- */

#define MAX_DEL 16
static char del_names[MAX_DEL][128];

static void save_done(int ok, const char *msg)
{
	busy = 0;
	lv_obj_remove_state(edit_btn, LV_STATE_DISABLED);
	ui_set_status(status_label, ok, msg);
	load_profile();
}

static void up_cb(int status, const char *out, void *user)
{
	char msg[256];

	(void)user;
	if (status == 0) {
		save_done(1, "Saved and applied");
	} else {
		snprintf(msg, sizeof(msg), "Saved, but activation failed: %.*s", (int)strcspn(out, "\n"), out);
		save_done(0, msg);
	}
}

static void add_arg(const char **argv, int *n, const char *v)
{
	if (*n < 62)
		argv[(*n)++] = v;
	argv[*n] = NULL;
}

static void add_family(const char **argv, int *n, int v6, char *dns)
{
	const struct ipcfg *c = &newcfg[v6];
	const char *p = v6 ? "ipv6." : "ipv4.";
	static char keys[2][6][24];
	char (*k)[24] = keys[v6];

	snprintf(k[0], 24, "%smethod", p);
	snprintf(k[1], 24, "%saddresses", p);
	snprintf(k[2], 24, "%sgateway", p);
	snprintf(k[3], 24, "%sdns", p);
	snprintf(k[4], 24, "%signore-auto-dns", p);

	add_arg(argv, n, k[0]);
	add_arg(argv, n, methods[c->method]);
	if (c->method == M_MANUAL) {
		add_arg(argv, n, k[1]);
		add_arg(argv, n, c->addr);
		if (c->gw[0]) {
			add_arg(argv, n, k[2]);
			add_arg(argv, n, c->gw);
		}
	}
	if (c->method != M_OFF && (c->dns1[0] || c->dns2[0])) {
		snprintf(dns, 140, "%s%s%s", c->dns1, c->dns1[0] && c->dns2[0] ? "," : "", c->dns2);
		add_arg(argv, n, k[3]);
		add_arg(argv, n, dns);
		if (c->method == M_AUTO) {
			/* own DNS servers instead of the ones from DHCP / router advertisements */
			add_arg(argv, n, k[4]);
			add_arg(argv, n, "yes");
		}
	}
}

static void add_cb(int status, const char *out, void *user)
{
	static const char *const up[] = { "nmcli", "connection", "up", "id", PROFILE, NULL };
	char car[8], msg[256];

	(void)user;
	if (status != 0) {
		snprintf(msg, sizeof(msg), "Saving failed: %.*s", (int)strcspn(out, "\n"), out);
		save_done(0, msg);
		return;
	}
	if (access(SYSNET, F_OK)) {
		save_done(1, "Saved; applies when the adapter is plugged in");
		return;
	}
	if (read_str(SYSNET "/carrier", car, sizeof(car)) == 0 && car[0] != '1') {
		save_done(1, "Saved; applies when a cable is plugged in");
		return;
	}
	ui_set_status(status_label, 1, "Applying...");
	if (exec_async(up, up_cb, NULL))
		save_done(0, "Saved, but cannot run nmcli");
}

static void del_cb(int status, const char *out, void *user)
{
	const char *argv[64];
	char dns4[140], dns6[140];
	int n = 0;

	(void)status; /* a profile that is already gone is fine */
	(void)out;
	(void)user;
	add_arg(argv, &n, "nmcli");
	add_arg(argv, &n, "connection");
	add_arg(argv, &n, "add");
	add_arg(argv, &n, "type");
	add_arg(argv, &n, "ethernet");
	add_arg(argv, &n, "ifname");
	add_arg(argv, &n, IFACE);
	add_arg(argv, &n, "con-name");
	add_arg(argv, &n, PROFILE);
	add_arg(argv, &n, "connection.autoconnect");
	add_arg(argv, &n, "yes");
	add_family(argv, &n, 0, dns4);
	add_family(argv, &n, 1, dns6);
	if (exec_async(argv, add_cb, NULL)) /* argv is copied by fork before this returns */
		save_done(0, "Cannot run nmcli");
}

/* ethernet profiles that could take eth0: camellia-eth itself, anything on
 * eth0 now, and NetworkManager's automatic "Wired connection N" */
static void list_cb(int status, const char *out, void *user)
{
	char *c = strdup(out), *line, *save = NULL;
	const char *argv[4 + 2 * MAX_DEL + 1];
	int n = 0, nd = 0;

	(void)user;
	argv[n++] = "nmcli";
	argv[n++] = "connection";
	argv[n++] = "delete";
	for (line = strtok_r(c, "\n", &save); status == 0 && line && nd < MAX_DEL;
	     line = strtok_r(NULL, "\n", &save)) {
		char *f[3];

		if (split_terse(line, f, 3) != 3 || strcmp(f[1], "802-3-ethernet"))
			continue;
		if (strcmp(f[0], PROFILE) && strcmp(f[2], IFACE) && strncmp(f[0], "Wired connection", 16))
			continue;
		snprintf(del_names[nd], sizeof(del_names[0]), "%s", f[0]);
		argv[n++] = "id";
		argv[n++] = del_names[nd++];
	}
	argv[n] = NULL;
	free(c);
	if (!nd)
		del_cb(0, "", NULL);
	else if (exec_async(argv, del_cb, NULL))
		save_done(0, "Cannot run nmcli");
}

static void save_start(void)
{
	static const char *const argv[] = { "nmcli", "-t", "-f", "NAME,TYPE,DEVICE", "connection", "show", NULL };

	busy = 1;
	lv_obj_add_state(edit_btn, LV_STATE_DISABLED);
	ui_set_status(status_label, 1, "Saving...");
	if (exec_async(argv, list_cb, NULL))
		save_done(0, "Cannot run nmcli");
}

/* ---- edit form ---- */

static struct {
	lv_obj_t *overlay, *panel, *kb, *err;
	lv_obj_t *m[2], *addr[2], *gw[2], *dns1[2], *dns2[2];
} f;

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
	for (int v6 = 0; v6 < 2; v6++) {
		int m = lv_dropdown_get_selected(f.m[v6]);

		show_if(f.addr[v6], m == M_MANUAL);
		show_if(f.gw[v6], m == M_MANUAL);
		show_if(f.dns1[v6], m != M_OFF);
		show_if(f.dns2[v6], m != M_OFF);
	}
}

static void form_close(void)
{
	if (f.overlay) {
		lv_obj_delete_async(f.overlay);
		f.overlay = NULL;
	}
}

static void kb_show(lv_obj_t *ta)
{
	kb_set_textarea(f.kb, ta);
	kb_set_symbols(f.kb, 1); /* digits, '.', ':' and '/' */
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
	(void)e;
	kb_hide();
}

static void dd_cb(lv_event_t *e)
{
	(void)e;
	form_update();
}

static const char *txt(lv_obj_t *ta)
{
	return lv_textarea_get_text(ta);
}

static int form_error(const char *msg)
{
	lv_label_set_text(f.err, msg);
	lv_obj_remove_flag(f.err, LV_OBJ_FLAG_HIDDEN);
	lv_obj_scroll_to_view(f.err, LV_ANIM_OFF);
	return -1;
}

static int form_read(int v6)
{
	struct ipcfg *c = &newcfg[v6];
	const char *fam = v6 ? "IPv6" : "IPv4";
	char msg[96];

	memset(c, 0, sizeof(*c));
	c->method = lv_dropdown_get_selected(f.m[v6]);
	if (c->method == M_MANUAL) {
		snprintf(c->addr, sizeof(c->addr), "%s", txt(f.addr[v6]));
		snprintf(c->gw, sizeof(c->gw), "%s", txt(f.gw[v6]));
		if (!valid_cidr(v6, c->addr)) {
			snprintf(msg, sizeof(msg), "%s: enter the address with its prefix, e.g. %s", fam,
				 v6 ? "2001:db8::10/64" : "192.168.1.10/24");
			return form_error(msg);
		}
		if (c->gw[0] && !valid_ip(v6, c->gw)) {
			snprintf(msg, sizeof(msg), "%s: the gateway is not a valid address", fam);
			return form_error(msg);
		}
	}
	if (c->method != M_OFF) {
		snprintf(c->dns1, sizeof(c->dns1), "%s", txt(f.dns1[v6]));
		snprintf(c->dns2, sizeof(c->dns2), "%s", txt(f.dns2[v6]));
		if ((c->dns1[0] && !valid_ip(v6, c->dns1)) || (c->dns2[0] && !valid_ip(v6, c->dns2))) {
			snprintf(msg, sizeof(msg), "%s: a DNS server is not a valid address", fam);
			return form_error(msg);
		}
	}
	return 0;
}

static void btn_cb(lv_event_t *e)
{
	if (!lv_event_get_user_data(e)) {
		form_close();
		return;
	}
	if (form_read(0) || form_read(1))
		return;
	form_close();
	save_start();
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

static lv_obj_t *text_row(const char *label, const char *hint, const char *value)
{
	lv_obj_t *ta = form_row(label, lv_textarea_create);

	lv_textarea_set_one_line(ta, true);
	lv_textarea_set_accepted_chars(ta, "0123456789abcdefABCDEF.:/");
	lv_textarea_set_max_length(ta, 63);
	lv_textarea_set_placeholder_text(ta, hint);
	lv_textarea_set_text(ta, value);
	lv_obj_add_event_cb(ta, ta_cb, LV_EVENT_FOCUSED, NULL);
	return ta;
}

static void section(const char *title)
{
	lv_obj_t *l = lv_label_create(f.panel);

	lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_label_set_text(l, title);
}

static void family_rows(int v6)
{
	const struct ipcfg *c = &cfg[v6];
	int m = have_profile ? c->method : M_AUTO;

	section(v6 ? "IPv6" : "IPv4");
	f.m[v6] = form_row("Method", lv_dropdown_create);
	lv_dropdown_set_options(f.m[v6], v6 ? "Automatic\nManual\nDisabled"
					      : "Automatic (DHCP)\nManual\nDisabled");
	lv_dropdown_set_selected(f.m[v6], m);
	lv_obj_add_event_cb(f.m[v6], dd_cb, LV_EVENT_VALUE_CHANGED, NULL);
	f.addr[v6] = text_row("Address / prefix", v6 ? "2001:db8::10/64" : "192.168.1.10/24", c->addr);
	f.gw[v6] = text_row("Gateway (optional)", v6 ? "fe80::1" : "192.168.1.1", c->gw);
	f.dns1[v6] = text_row("DNS 1 (optional)", v6 ? "2606:4700:4700::1111" : "1.1.1.1", c->dns1);
	f.dns2[v6] = text_row("DNS 2 (optional)", v6 ? "2001:4860:4860::8888" : "8.8.8.8", c->dns2);
}

static void open_form(void)
{
	lv_obj_t *l, *row;

	memset(&f, 0, sizeof(f));
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
	lv_label_set_text(l, "Ethernet (" IFACE ")");

	family_rows(0);
	family_rows(1);

	l = lv_label_create(f.panel);
	lv_obj_set_width(l, lv_pct(100));
	lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
	lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(l, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
	lv_label_set_text(l, "Saving reconnects " IFACE ": SSH sessions over Ethernet are cut.");

	f.err = lv_label_create(f.panel);
	lv_obj_set_width(f.err, lv_pct(100));
	lv_label_set_long_mode(f.err, LV_LABEL_LONG_MODE_WRAP);
	lv_obj_set_style_text_font(f.err, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(f.err, lv_palette_main(LV_PALETTE_RED), 0);
	lv_obj_add_flag(f.err, LV_OBJ_FLAG_HIDDEN);

	row = new_row();
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	ui_button(row, "Cancel", btn_cb, NULL);
	ui_button(row, LV_SYMBOL_SAVE " Save", btn_cb, (void *)1);

	f.kb = kb_create(f.overlay);
	lv_obj_add_event_cb(f.kb, kb_cb, LV_EVENT_READY, NULL);

	kb_hide();
	form_update();
}

static void edit_cb(lv_event_t *e)
{
	(void)e;
	if (!busy)
		open_form();
}

/* ---- tab ---- */

static lv_obj_t *row(lv_obj_t *card, const char *key)
{
	lv_obj_t *v = ui_row(card, key);

	lv_obj_set_style_pad_column(lv_obj_get_parent(v), 24, 0);
	lv_obj_set_flex_grow(v, 1);
	lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_WRAP);
	lv_obj_set_style_text_font(v, &lv_font_montserrat_28, 0);
	return v;
}

void lan_create(lv_obj_t *tab)
{
	lv_obj_t *c;

	lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(tab, 30, 0);
	lv_obj_set_style_pad_hor(tab, 30, 0);

	c = ui_card(tab, LV_SYMBOL_SHUFFLE "  Ethernet (" IFACE ")");
	v_link = row(c, "Link");
	v_mac = row(c, "MAC");
	v_ip4 = row(c, "IPv4");
	v_ip6 = row(c, "IPv6");
	v_gw4 = row(c, "Gateway v4");
	v_gw6 = row(c, "Gateway v6");
	v_dns = row(c, "DNS");

	c = ui_card(tab, LV_SYMBOL_SETTINGS "  Settings");
	v_set4 = row(c, "IPv4");
	v_set6 = row(c, "IPv6");
	edit_btn = ui_button(c, LV_SYMBOL_EDIT " Edit", edit_cb, NULL);
	lv_obj_set_width(edit_btn, lv_pct(100));
	status_label = lv_label_create(c);
	lv_obj_set_width(status_label, lv_pct(100));
	lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
	lv_obj_set_style_text_font(status_label, &lv_font_montserrat_28, 0);
	lv_label_set_text(status_label, "");

	show_settings();
	lan_update(1);
}
