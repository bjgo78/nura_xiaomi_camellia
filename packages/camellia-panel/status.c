/* Status page: network, battery, system. Values are read from /proc, /sys and
 * NetworkManager (nmcli) and refreshed while the screen is on. */
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "ui.h"

#define BAT "/sys/class/power_supply/battery"

static lv_obj_t *v_host, *v_wifi, *v_wlan_ip, *v_usb_ip, *v_other_ip;
static lv_obj_t *v_bat, *v_bat_state, *v_bat_temp, *v_limit;
static lv_obj_t *v_uptime, *v_load, *v_mem, *v_swap, *v_disk;

static int read_line(const char *path, char *buf, size_t len)
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

static const char *conf_value(const char *path, const char *key, char *buf, size_t len)
{
	FILE *f = fopen(path, "r");
	char line[256];
	size_t kl = strlen(key);

	buf[0] = 0;
	if (!f)
		return buf;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, key, kl) && line[kl] == '=') {
			char *v = line + kl + 1;
			v[strcspn(v, "\n")] = 0;
			if (*v == '"') {
				v++;
				v[strcspn(v, "\"")] = 0;
			}
			snprintf(buf, len, "%s", v);
		}
	}
	fclose(f);
	return buf;
}

static void fmt_bytes(char *buf, size_t len, unsigned long long b)
{
	if (b >= 1ULL << 30)
		snprintf(buf, len, "%.1f GB", b / (double)(1ULL << 30));
	else
		snprintf(buf, len, "%llu MB", b >> 20);
}

lv_obj_t *ui_card(lv_obj_t *parent, const char *title)
{
	lv_obj_t *card = lv_obj_create(parent);
	lv_obj_t *t;

	lv_obj_set_width(card, lv_pct(100));
	lv_obj_set_height(card, LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(card, 36, 0);
	lv_obj_set_style_pad_row(card, 18, 0);
	lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

	t = lv_label_create(card);
	lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(t, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_label_set_text(t, title);
	return card;
}

lv_obj_t *ui_row(lv_obj_t *card, const char *key)
{
	lv_obj_t *row = lv_obj_create(card);
	lv_obj_t *k, *v;

	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);

	k = lv_label_create(row);
	lv_obj_set_style_text_font(k, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(k, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
	lv_label_set_text(k, key);

	v = lv_label_create(row);
	lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(v, "-");
	return v;
}

void status_create(lv_obj_t *tab)
{
	lv_obj_t *c;

	lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(tab, 30, 0);
	lv_obj_set_style_pad_hor(tab, 30, 0);

	c = ui_card(tab, LV_SYMBOL_WIFI "  Network");
	v_host = ui_row(c, "Hostname");
	v_wifi = ui_row(c, "Wi-Fi");
	v_wlan_ip = ui_row(c, "Wi-Fi IP");
	v_usb_ip = ui_row(c, "USB IP");
	v_other_ip = ui_row(c, "Other");

	c = ui_card(tab, LV_SYMBOL_CHARGE "  Battery");
	v_bat = ui_row(c, "Level");
	v_bat_state = ui_row(c, "State");
	v_bat_temp = ui_row(c, "Temperature");
	v_limit = ui_row(c, "Charge limit");

	c = ui_card(tab, LV_SYMBOL_SETTINGS "  System");
	v_uptime = ui_row(c, "Uptime");
	v_load = ui_row(c, "Load");
	v_mem = ui_row(c, "Memory");
	v_swap = ui_row(c, "Swap (zram)");
	v_disk = ui_row(c, "Disk /");

	status_update(1);
}

static void update_network(int slow)
{
	struct ifaddrs *ifa, *i;
	char wlan[64] = "-", usb[64] = "-", other[256] = "";
	char host[64] = "?";

	gethostname(host, sizeof(host));
	lv_label_set_text(v_host, host);

	if (getifaddrs(&ifa) == 0) {
		for (i = ifa; i; i = i->ifa_next) {
			char ip[INET_ADDRSTRLEN];

			if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET)
				continue;
			inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, ip, sizeof(ip));
			if (!strcmp(i->ifa_name, "lo") || !strncmp(i->ifa_name, "docker", 6) ||
			    !strncmp(i->ifa_name, "br-", 3) || !strncmp(i->ifa_name, "veth", 4))
				continue;
			if (!strcmp(i->ifa_name, "wlan0"))
				snprintf(wlan, sizeof(wlan), "%s", ip);
			else if (!strcmp(i->ifa_name, "usb0"))
				snprintf(usb, sizeof(usb), "%s", ip);
			else
				snprintf(other + strlen(other), sizeof(other) - strlen(other), "%s%s %s",
					 other[0] ? "\n" : "", i->ifa_name, ip);
		}
		freeifaddrs(ifa);
	}
	lv_label_set_text(v_wlan_ip, wlan);
	lv_label_set_text(v_usb_ip, usb);
	lv_label_set_text(v_other_ip, other[0] ? other : "-");

	if (slow) {
		/* "yes:SSID:70" for the network wlan0 is connected to */
		FILE *p = popen("nmcli -t -f ACTIVE,SSID,SIGNAL device wifi list ifname wlan0 --rescan no 2>/dev/null", "r");
		char line[256], ssid[300] = "not connected";

		if (p) {
			while (fgets(line, sizeof(line), p)) {
				if (!strncmp(line, "yes:", 4)) {
					char *s = line + 4, *sig = strrchr(s, ':');
					if (sig) {
						*sig++ = 0;
						sig[strcspn(sig, "\n")] = 0;
						snprintf(ssid, sizeof(ssid), "%s  %s%%", s, sig);
					}
				}
			}
			pclose(p);
		}
		lv_label_set_text(v_wifi, ssid);
	}
}

static void update_battery(void)
{
	char cap[16], st[32], temp[16], a[16], b[16];

	if (read_line(BAT "/capacity", cap, sizeof(cap)) == 0)
		lv_label_set_text_fmt(v_bat, "%s %%", cap);
	if (read_line(BAT "/status", st, sizeof(st)) == 0)
		lv_label_set_text(v_bat_state, st);
	if (read_line(BAT "/temp", temp, sizeof(temp)) == 0) {
		int t = atoi(temp); /* tenths of a degree */
		lv_label_set_text_fmt(v_bat_temp, "%d.%d \xC2\xB0" "C", t / 10, abs(t % 10));
	}
	conf_value("/etc/conf.d/camellia-battery", "CHARGE_START", a, sizeof(a));
	conf_value("/etc/conf.d/camellia-battery", "CHARGE_STOP", b, sizeof(b));
	if (a[0] && b[0])
		lv_label_set_text_fmt(v_limit, "%s - %s %%", a, b);
}

static void update_system(void)
{
	char buf[128], s1[32], s2[32];
	FILE *f;
	unsigned long long total = 0, avail = 0, stotal = 0, sfree = 0;
	struct statvfs vfs;

	if (read_line("/proc/uptime", buf, sizeof(buf)) == 0) {
		long up = atol(buf);
		lv_label_set_text_fmt(v_uptime, "%ldd %02ld:%02ld", up / 86400, up / 3600 % 24,
				      up / 60 % 60);
	}
	if (read_line("/proc/loadavg", buf, sizeof(buf)) == 0) {
		float l1, l5, l15;
		if (sscanf(buf, "%f %f %f", &l1, &l5, &l15) == 3)
			lv_label_set_text_fmt(v_load, "%.2f  %.2f  %.2f", l1, l5, l15);
	}
	f = fopen("/proc/meminfo", "r");
	if (f) {
		unsigned long long v;
		while (fgets(buf, sizeof(buf), f)) {
			if (sscanf(buf, "MemTotal: %llu", &v) == 1) total = v << 10;
			else if (sscanf(buf, "MemAvailable: %llu", &v) == 1) avail = v << 10;
			else if (sscanf(buf, "SwapTotal: %llu", &v) == 1) stotal = v << 10;
			else if (sscanf(buf, "SwapFree: %llu", &v) == 1) sfree = v << 10;
		}
		fclose(f);
		fmt_bytes(s1, sizeof(s1), total - avail);
		fmt_bytes(s2, sizeof(s2), total);
		lv_label_set_text_fmt(v_mem, "%s / %s", s1, s2);
		fmt_bytes(s1, sizeof(s1), stotal - sfree);
		fmt_bytes(s2, sizeof(s2), stotal);
		lv_label_set_text_fmt(v_swap, "%s / %s", s1, s2);
	}
	if (statvfs("/", &vfs) == 0) {
		unsigned long long size = (unsigned long long)vfs.f_blocks * vfs.f_frsize;
		unsigned long long used = size - (unsigned long long)vfs.f_bavail * vfs.f_frsize;
		fmt_bytes(s1, sizeof(s1), used);
		fmt_bytes(s2, sizeof(s2), size);
		lv_label_set_text_fmt(v_disk, "%s / %s", s1, s2);
	}
}

/* slow: also the values that need a subprocess (Wi-Fi name and signal) */
void status_update(int slow)
{
	update_network(slow);
	update_battery();
	update_system();
}
