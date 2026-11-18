/* camellia-panel: on-screen UI for the Nura server phone.
 * Step 1 (hardware bring-up): DRM output, touch, backlight, power key blank/wake,
 * one test screen. Status page and setup screens follow. */
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

/* ---- test screen ---- */
static lv_obj_t *count_label;
static int taps;

static void tap_cb(lv_event_t *e)
{
	(void)e;
	lv_label_set_text_fmt(count_label, "Taps: %d", ++taps);
}

static void build_test_screen(void)
{
	char host[64] = "?";
	lv_obj_t *scr = lv_screen_active();
	lv_obj_t *l, *btn;

	gethostname(host, sizeof(host));
	lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(scr, 60, 0);
	lv_obj_set_style_pad_row(scr, 40, 0);

	l = lv_label_create(scr);
	lv_obj_set_style_text_font(l, &lv_font_montserrat_48, 0);
	lv_label_set_text(l, "Nura - camellia-panel");

	l = lv_label_create(scr);
	lv_label_set_text_fmt(l, "Host: %s", host);

	btn = lv_button_create(scr);
	lv_obj_set_size(btn, 600, 200);
	lv_obj_add_event_cb(btn, tap_cb, LV_EVENT_CLICKED, NULL);
	l = lv_label_create(btn);
	lv_label_set_text(l, "Tap me");
	lv_obj_center(l);

	count_label = lv_label_create(scr);
	lv_label_set_text(count_label, "Taps: 0");

	l = lv_label_create(scr);
	lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
	lv_label_set_text_fmt(l, "Screen turns off after %u s.\nPower key: off / on.", blank_ms / 1000);
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

	if (drm_out_open(&out, CARD))
		return 1;

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

	build_test_screen();

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
