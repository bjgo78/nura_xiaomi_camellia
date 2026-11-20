/* camellia-panel: on-screen UI for the Nura server phone.
 * DRM output, touch, backlight, power key blank/wake; status page in the first
 * tab, setup screens (Wi-Fi, LAN, battery, power) in the others. */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "lvgl.h"
#include "drm_out.h"
#include "ui.h"

#define CARD        "/dev/dri/card0"
#define TOUCH_DEV   "/dev/input/event2"
#define KEYS_DEV    "/dev/input/event1"
#define BACKLIGHT   "/sys/class/leds/lcd-backlight"
#define DRAW_LINES  200

static struct drm_out out;
static int blanked;
static int brightness;
static unsigned blank_ms = 60000;
static volatile sig_atomic_t quit;

static uint32_t tick_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000u + ts.tv_nsec / 1000000u;
}

static int read_int(const char *path)
{
	FILE *f = fopen(path, "r");
	int v = -1;

	if (f) {
		if (fscanf(f, "%d", &v) != 1)
			v = -1;
		fclose(f);
	}
	return v;
}

static void write_int(const char *path, int v)
{
	FILE *f = fopen(path, "w");

	if (f) {
		fprintf(f, "%d\n", v);
		fclose(f);
	}
}

static void set_backlight(int v)
{
	write_int(BACKLIGHT "/brightness", v);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
	int w = lv_area_get_width(area);
	uint32_t src_stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_XRGB8888);

	if (!blanked) {
		for (int y = area->y1; y <= area->y2; y++) {
			memcpy(out.map + (size_t)y * out.pitch + area->x1 * 4, px, w * 4);
			px += src_stride;
		}
		drm_out_dirty(&out, area->x1, area->y1, area->x2, area->y2);
	}
	lv_display_flush_ready(disp);
}

static void blank(void)
{
	if (blanked)
		return;
	set_backlight(0);
	drm_out_power(&out, 0);
	blanked = 1;
	fprintf(stderr, "screen off\n");
}

static void wake(lv_display_t *disp)
{
	if (!blanked)
		return;
	drm_out_power(&out, 1);
	blanked = 0;
	lv_obj_invalidate(lv_screen_active());
	lv_refr_now(disp);
	set_backlight(brightness);
	lv_display_trigger_activity(disp);
	fprintf(stderr, "screen on\n");
}

/* ---- layout ---- */
static lv_obj_t *clock_label, *bat_label;
static int refresh_count;

static void top_bar_update(void)
{
	char buf[16];
	time_t now = time(NULL);
	struct tm tm;
	FILE *f;

	localtime_r(&now, &tm);
	strftime(buf, sizeof(buf), "%H:%M", &tm);
	lv_label_set_text(clock_label, buf);
	f = fopen("/sys/class/power_supply/battery/capacity", "r");
	if (f) {
		int cap;
		if (fscanf(f, "%d", &cap) == 1)
			lv_label_set_text_fmt(bat_label, "%d %%", cap);
		fclose(f);
	}
}

static void refresh_cb(lv_timer_t *t)
{
	(void)t;
	if (blanked)
		return;
	top_bar_update();
	status_update(refresh_count % 5 == 0);
	lan_update(refresh_count % 5 == 0);
	refresh_count++;
}

/* The top 103 px belong to the punch-hole camera: only the clock (left of it)
 * and the battery level (right of it) go there. */
static void build_layout(void)
{
	lv_obj_t *scr = lv_screen_active();
	lv_obj_t *bar, *tv, *tab;
	static const char *const names[] = {
		LV_SYMBOL_HOME " Status", LV_SYMBOL_WIFI " Wi-Fi", LV_SYMBOL_SHUFFLE " LAN",
		LV_SYMBOL_CHARGE " Battery", LV_SYMBOL_POWER " Power",
	};

	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	bar = lv_obj_create(scr);
	lv_obj_remove_style_all(bar);
	lv_obj_set_size(bar, out.width, UI_TOP_BAR);
	lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);

	clock_label = lv_label_create(bar);
	lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_36, 0);
	lv_obj_set_style_text_color(clock_label, lv_color_white(), 0);
	lv_obj_set_width(clock_label, UI_CUTOUT_X1 - 2 * UI_SIDE_PAD);
	lv_obj_align(clock_label, LV_ALIGN_LEFT_MID, UI_SIDE_PAD + 40, 6);

	bat_label = lv_label_create(bar);
	lv_obj_set_style_text_font(bat_label, &lv_font_montserrat_36, 0);
	lv_obj_set_style_text_color(bat_label, lv_color_white(), 0);
	lv_obj_set_style_text_align(bat_label, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(bat_label, "");
	lv_obj_set_width(bat_label, out.width - UI_CUTOUT_X2 - 2 * UI_SIDE_PAD);
	lv_obj_align(bat_label, LV_ALIGN_RIGHT_MID, -(UI_SIDE_PAD + 40), 6);

	tv = lv_tabview_create(scr);
	lv_obj_set_size(tv, out.width, out.height - UI_TOP_BAR);
	lv_obj_align(tv, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_tabview_set_tab_bar_position(tv, LV_DIR_BOTTOM);
	lv_tabview_set_tab_bar_size(tv, UI_TAB_BAR);
	/* keep the tab buttons out of the rounded bottom corners */
	lv_obj_set_style_pad_hor(lv_tabview_get_tab_bar(tv), UI_SIDE_PAD, 0);
	lv_obj_set_style_pad_bottom(lv_tabview_get_tab_bar(tv), 30, 0);
	lv_obj_set_style_text_font(lv_tabview_get_tab_bar(tv), &lv_font_montserrat_28, 0);

	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		tab = lv_tabview_add_tab(tv, names[i]);
		if (i == 0)
			status_create(tab);
		else if (i == 1)
			wifi_create(tab);
		else if (i == 2)
			lan_create(tab);
		else if (i == 3)
			battery_create(tab);
		else
			power_create(tab);
	}

	top_bar_update();
	lv_timer_create(refresh_cb, 2000, NULL);
}

static void on_signal(int sig)
{
	(void)sig;
	quit = 1;
}

int main(int argc, char **argv)
{
	lv_display_t *disp;
	lv_indev_t *touch;
	int keys;
	void *buf1, *buf2;
	size_t bufsize;

	if (argc > 1)
		blank_ms = atoi(argv[1]) * 1000u;
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);

	/* The boot splash (Plymouth) holds the display until the end of the boot;
	 * its OpenRC plugin quits it once the default runlevel is up. */
	for (int i = 0; i < 900 && !quit && system("plymouth --ping 2>/dev/null") == 0; i++) {
		if (i == 0)
			fprintf(stderr, "waiting for plymouth to quit\n");
		usleep(200000);
	}
	/* another program may still hold the display for a moment */
	for (int i = 0; drm_out_open(&out, CARD); i++) {
		drm_out_close(&out);
		if (i == 50 || quit)
			return 1;
		usleep(200000);
	}

	brightness = read_int(BACKLIGHT "/brightness");
	if (brightness <= 0) {
		int max = read_int(BACKLIGHT "/max_brightness");
		brightness = max > 0 ? max / 2 : 1023;
	}
	set_backlight(brightness);

	lv_init();
	lv_tick_set_cb(tick_ms);

	disp = lv_display_create(out.width, out.height);
	lv_display_set_color_format(disp, LV_COLOR_FORMAT_XRGB8888);
	lv_display_set_flush_cb(disp, flush_cb);
	bufsize = lv_draw_buf_width_to_stride(out.width, LV_COLOR_FORMAT_XRGB8888) * DRAW_LINES;
	buf1 = malloc(bufsize);
	buf2 = malloc(bufsize);
	lv_display_set_buffers(disp, buf1, buf2, bufsize, LV_DISPLAY_RENDER_MODE_PARTIAL);

	touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, TOUCH_DEV);
	if (!touch)
		fprintf(stderr, "touch: cannot open %s\n", TOUCH_DEV);

	/* grab the keys so nothing else acts on the power key */
	keys = open(KEYS_DEV, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (keys < 0 || ioctl(keys, EVIOCGRAB, 1) < 0)
		fprintf(stderr, "keys: %s: %s\n", KEYS_DEV, strerror(errno));

	build_layout();

	while (!quit) {
		uint32_t next = lv_timer_handler();
		struct pollfd pfd = { .fd = keys, .events = POLLIN };
		struct input_event ev;

		if (next > 100)
			next = 100;
		if (keys >= 0 && poll(&pfd, 1, next) > 0) {
			while (read(keys, &ev, sizeof(ev)) == sizeof(ev)) {
				if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 1) {
					if (blanked)
						wake(disp);
					else
						blank();
				}
			}
		} else if (keys < 0) {
			usleep(next * 1000);
		}
		if (!blanked && blank_ms && lv_display_get_inactive_time(disp) > blank_ms)
			blank();
	}

	set_backlight(brightness);
	drm_out_close(&out);
	return 0;
}
