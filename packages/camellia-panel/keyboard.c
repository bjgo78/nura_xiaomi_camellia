/* On-screen keyboard of the setup screens, laid out like Gboard: letter rows
 * on an even grid, ?123 and =\< symbol pages, a language list behind the globe
 * key (English US QWERTY, Hungarian QWERTZ; the choice is saved as
 * KEYBOARD_LAYOUT in /etc/conf.d/camellia-panel), long press for accents and
 * for the digits of the top row (the default option opens above the finger;
 * slide to pick, release to type), one-shot
 * shift (off after the next character) and caps lock on a double tap.
 *
 * Montserrat has no accented letters, so DejaVu Sans (font-dejavu, loaded with
 * LVGL's Tiny TTF) is its fallback on the keys and in the text fields. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keyboard.h"

#define CONF       "/etc/conf.d/camellia-panel"
#define CONF_KEY   "KEYBOARD_LAYOUT="

static const char *const font_paths[] = {
	"/usr/share/fonts/dejavu/DejaVuSans.ttf",          /* Alpine font-dejavu */
	"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", /* Debian, Ubuntu */
};

/* geometry (1080 px wide panel); the bottom padding keeps the last row out
 * of the rounded corners */
#define PAD_SIDE    10
#define PAD_TOP     18
#define PAD_BOTTOM  90
#define ROW_H       150
#define GAP         12
#define KB_HEIGHT   (PAD_TOP + 4 * ROW_H + PAD_BOTTOM)
#define ALT_CELL    100
#define ALT_H       130
#define MAX_KEYS    48
#define MAX_ALTS    12
#define DBL_TAP_MS  400

/* the panel's colours: LVGL's dark theme (screen 15171A, card 282b30) and its
 * blue primary */
#define COL_BG       lv_color_hex(0x15171A)
#define COL_KEY      lv_color_hex(0x3a3d42)
#define COL_FN       lv_color_hex(0x282b30)
#define COL_PRESSED  lv_color_hex(0x55595f)
#define COL_TEXT     lv_palette_lighten(LV_PALETTE_GREY, 5)
#define COL_HINT     lv_palette_lighten(LV_PALETTE_GREY, 1)
#define COL_PRIMARY  lv_palette_main(LV_PALETTE_BLUE)

/* ---- layouts ---- */

struct layout {
	const char *code, *name;
	int grid;              /* key columns of the letter rows */
	const char *rows[3];   /* letters, space separated */
	const char *alts;      /* "base:alt alt ...|base:..." */
	const char *accent_keys; /* top-row keys whose pop-up opens on the accent */
};

static const struct layout layouts[] = {
	{ "en", "English (US)", 10,
	  { "q w e r t y u i o p", "a s d f g h j k l", "z x c v b n m" },
	  "a:à á â ä æ ã å ā|c:ç ć č|e:è é ê ë ē ę ė|i:î ï í ī į ì|l:ł|n:ñ ń|"
	  "o:ô ö ò ó œ ø ō õ|s:ß ś š|u:û ü ù ú ū|y:ÿ|z:ž ź ż", NULL },
	/* like Gboard: plain QWERTZ, every accent on long press, the Hungarian
	 * ones first */
	{ "hu", "Magyar", 10,
	  { "q w e r t z u i o p", "a s d f g h j k l", "y x c v b n m" },
	  "a:á à â ä æ ã å ā|c:ç ć č|e:é è ê ë ē ę ė|i:í î ï ī į ì|l:ł|n:ñ ń|"
	  "o:ó ö ő ô ò œ ø ō õ|s:ß ś š|u:ú ü ű û ù ū|y:ÿ|z:ž ź ż", "e u i o" },
};
#define N_LAYOUTS ((int)(sizeof(layouts) / sizeof(layouts[0])))

/* symbol pages (every printable ASCII character is on one of them) */
static const char *const sym_rows[2][3] = {
	{ "1 2 3 4 5 6 7 8 9 0", "@ # $ _ & - + ( ) /", "* \" ' : ; ! ?" },
	{ "~ ` | ^ = { } [ ] \\", "% < > € £ ¥ ¢ ° § ©", "× ÷ • ¶ ¿ ¡ ®" },
};
static const char *const sym_alts =
	"-:– — ·|$:€ £ ¥ ¢|!:¡|?:¿|\":« » „ “ ”|':‘ ’|.:… ? !";

enum { PAGE_LETTERS, PAGE_SYM1, PAGE_SYM2 };
enum { KT_CHAR, KT_SHIFT, KT_BKSP, KT_PAGE, KT_GLOBE, KT_SPACE, KT_ENTER };
enum { SHIFT_OFF, SHIFT_ONCE, SHIFT_LOCK };

struct key {
	lv_obj_t *obj, *label;
	int type, page;
	int digit;          /* the pop-up starts with the top row's digit */
	char txt[8];
	char alts[128];
};

static struct {
	lv_obj_t *kb, *ta;
	struct key keys[MAX_KEYS];
	int n_keys;
	int page, shift;
	uint32_t shift_tick;
	int long_pressed;
	/* long-press popup */
	lv_obj_t *alt, *alt_cells[MAX_ALTS];
	char alt_txt[MAX_ALTS][8];
	int n_alts, alt_sel;
	/* language list */
	lv_obj_t *lang;
} s;

static int cur_layout = -1; /* kept across forms, loaded once from CONF */

/* ---- fonts ---- */

static lv_font_t font48, font36;
static int fonts_ready;

static void fonts_init(void)
{
	void *buf = NULL;
	long len = 0;

	if (fonts_ready)
		return;
	fonts_ready = 1;
	font48 = lv_font_montserrat_48;
	font36 = lv_font_montserrat_36;
	for (unsigned i = 0; i < sizeof(font_paths) / sizeof(font_paths[0]) && !buf; i++) {
		FILE *f = fopen(font_paths[i], "rb");

		if (!f)
			continue;
		if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0) {
			buf = malloc(len); /* Tiny TTF reads from it for as long as the font lives */
			if (buf && fread(buf, 1, len, f) != (size_t)len) {
				free(buf);
				buf = NULL;
			}
		}
		fclose(f);
	}
	if (!buf) {
		fprintf(stderr, "keyboard: DejaVu Sans not found, accented letters will not show\n");
		return;
	}
	font48.fallback = lv_tiny_ttf_create_data(buf, len, 48);
	font36.fallback = lv_tiny_ttf_create_data(buf, len, 36);
}

const lv_font_t *kb_text_font(void)
{
	fonts_init();
	return &font36;
}

/* ---- UTF-8 ---- */

static uint32_t utf8_next(const char **p)
{
	const unsigned char *s8 = (const unsigned char *)*p;
	uint32_t c = s8[0];
	int n = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;

	if (n)
		c &= 0x3F >> n;
	for (int i = 1; i <= n && s8[i]; i++)
		c = c << 6 | (s8[i] & 0x3F);
	*p += n + 1;
	return c;
}

static char *utf8_put(char *o, uint32_t c)
{
	if (c < 0x80) {
		*o++ = c;
	} else if (c < 0x800) {
		*o++ = 0xC0 | c >> 6;
		*o++ = 0x80 | (c & 0x3F);
	} else if (c < 0x10000) {
		*o++ = 0xE0 | c >> 12;
		*o++ = 0x80 | (c >> 6 & 0x3F);
		*o++ = 0x80 | (c & 0x3F);
	} else {
		*o++ = 0xF0 | c >> 18;
		*o++ = 0x80 | (c >> 12 & 0x3F);
		*o++ = 0x80 | (c >> 6 & 0x3F);
		*o++ = 0x80 | (c & 0x3F);
	}
	return o;
}

/* upper case for ASCII, Latin-1 and Latin Extended-A (every letter used here) */
static uint32_t cp_upper(uint32_t c)
{
	if (c >= 'a' && c <= 'z')
		return c - 32;
	if (c >= 0xE0 && c <= 0xFE && c != 0xF7)
		return c - 0x20;
	if (c == 0xFF)
		return 0x178;
	if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177))
		return c & 1 ? c - 1 : c;
	if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E))
		return c & 1 ? c : c - 1;
	return c;
}

static void utf8_upper(const char *in, char *out, size_t size)
{
	char *o = out;

	while (*in && (size_t)(o - out) + 5 < size)
		o = utf8_put(o, cp_upper(utf8_next(&in)));
	*o = 0;
}

/* the text a key types now (upper case with shift or caps lock) */
static void cased(const char *txt, char *out, size_t size)
{
	if (s.shift != SHIFT_OFF && s.page == PAGE_LETTERS)
		utf8_upper(txt, out, size);
	else
		snprintf(out, size, "%s", txt);
}

/* ---- saved layout ---- */

static void layout_load(void)
{
	FILE *f = fopen(CONF, "r");
	char line[256];

	cur_layout = 0;
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *v = line + strlen(CONF_KEY);

		if (strncmp(line, CONF_KEY, strlen(CONF_KEY)))
			continue;
		v += *v == '"';
		v[strcspn(v, "\"\n")] = 0;
		for (int i = 0; i < N_LAYOUTS; i++)
			if (!strcmp(v, layouts[i].code))
				cur_layout = i;
	}
	fclose(f);
}

/* rewrite only that line, keep everything else */
static void layout_save(void)
{
	FILE *in = fopen(CONF, "r"), *out = fopen(CONF ".tmp", "w");
	char line[256];
	int done = 0;

	if (!out) {
		if (in)
			fclose(in);
		fprintf(stderr, "keyboard: cannot write %s\n", CONF);
		return;
	}
	while (in && fgets(line, sizeof(line), in)) {
		if (!strncmp(line, CONF_KEY, strlen(CONF_KEY))) {
			fprintf(out, CONF_KEY "%s\n", layouts[cur_layout].code);
			done = 1;
		} else {
			fputs(line, out);
		}
	}
	if (!done)
		fprintf(out, CONF_KEY "%s\n", layouts[cur_layout].code);
	if (in)
		fclose(in);
	if (fclose(out) || rename(CONF ".tmp", CONF))
		fprintf(stderr, "keyboard: cannot write %s\n", CONF);
}

/* ---- styles ---- */

static lv_style_t st_key, st_fn, st_pressed, st_primary, st_primary_pressed, st_popup;

static void styles_init(void)
{
	static int done;

	if (done)
		return;
	done = 1;
	lv_style_init(&st_key);
	lv_style_set_bg_opa(&st_key, LV_OPA_COVER);
	lv_style_set_bg_color(&st_key, COL_KEY);
	lv_style_set_radius(&st_key, 18);
	lv_style_set_text_color(&st_key, COL_TEXT);

	lv_style_init(&st_fn);
	lv_style_set_bg_color(&st_fn, COL_FN);

	lv_style_init(&st_pressed);
	lv_style_set_bg_color(&st_pressed, COL_PRESSED);

	lv_style_init(&st_primary);
	lv_style_set_bg_color(&st_primary, COL_PRIMARY);
	lv_style_set_text_color(&st_primary, lv_color_white());

	lv_style_init(&st_primary_pressed);
	lv_style_set_bg_color(&st_primary_pressed, lv_palette_darken(LV_PALETTE_BLUE, 2));

	lv_style_init(&st_popup);
	lv_style_set_bg_opa(&st_popup, LV_OPA_COVER);
	lv_style_set_bg_color(&st_popup, COL_FN);
	lv_style_set_border_width(&st_popup, 2);
	lv_style_set_border_color(&st_popup, COL_PRESSED);
	lv_style_set_radius(&st_popup, 20);
	lv_style_set_shadow_width(&st_popup, 40);
	lv_style_set_shadow_color(&st_popup, lv_color_black());
	lv_style_set_shadow_opa(&st_popup, LV_OPA_70);
	lv_style_set_text_color(&st_popup, COL_TEXT);
}

/* a plain object that never takes the focus away from the text field and
 * never starts a scroll (a slide over a long-press popup must stay a press) */
static lv_obj_t *plain(lv_obj_t *parent)
{
	lv_obj_t *o = lv_obj_create(parent);

	lv_obj_remove_style_all(o);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE |
			      LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE);
	return o;
}

/* ---- popups ---- */

static void alt_close(void)
{
	if (s.alt) {
		lv_obj_delete(s.alt);
		s.alt = NULL;
	}
	s.n_alts = 0;
}

static void lang_close(void)
{
	if (s.lang) {
		lv_obj_delete_async(s.lang);
		s.lang = NULL;
	}
}

static void alt_highlight(int sel)
{
	for (int i = 0; i < s.n_alts; i++) {
		lv_obj_set_style_bg_opa(s.alt_cells[i], i == sel ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
		lv_obj_set_style_text_color(s.alt_cells[i], i == sel ? lv_color_white() : COL_TEXT, 0);
	}
	s.alt_sel = sel;
}

/* is the token in the space separated list */
static int key_listed(const char *list, const char *tok)
{
	size_t n = strlen(tok);

	for (const char *p = list; (p = strstr(p, tok)); p += n)
		if ((p == list || p[-1] == ' ') && (p[n] == ' ' || !p[n]))
			return 1;
	return 0;
}

/* Like Gboard: the default option opens right above the finger (released
 * without sliding, it is typed); the others spread away from the nearer edge
 * of the screen. The default is the first option (the top row's digit), on
 * the layout's accent keys the first accent, with the digit next to it on the
 * other side. */
static void alt_open(struct key *k)
{
	const struct layout *L = &layouts[cur_layout];
	char buf[sizeof(k->alts)], *tok, *save = NULL, opt[MAX_ALTS][8];
	int n = 0, def = 0, side = -1, order[MAX_ALTS], m = 0, dpos, right;
	int32_t hres = lv_display_get_horizontal_resolution(NULL), kx, cell = ALT_CELL, room, x, y;
	lv_area_t a;

	alt_close();
	snprintf(buf, sizeof(buf), "%s", k->alts);
	for (tok = strtok_r(buf, " ", &save); tok && n < MAX_ALTS; tok = strtok_r(NULL, " ", &save))
		cased(tok, opt[n++], sizeof(opt[0]));
	if (!n)
		return;
	if (k->digit && n > 1 && L->accent_keys && key_listed(L->accent_keys, k->txt)) {
		def = 1;
		side = 0;
	}

	lv_obj_get_coords(k->obj, &a);
	kx = (a.x1 + a.x2) / 2;
	right = kx < hres / 2;
	if (right) {
		if (side >= 0)
			order[m++] = side;
		order[m++] = def;
		for (int i = 0; i < n; i++)
			if (i != def && i != side)
				order[m++] = i;
	} else {
		for (int i = n - 1; i >= 0; i--)
			if (i != def && i != side)
				order[m++] = i;
		order[m++] = def;
		if (side >= 0)
			order[m++] = side;
	}
	for (dpos = 0; order[dpos] != def; dpos++)
		;

	/* narrower cells rather than moving the default away from the finger */
	room = kx - PAD_SIDE - 10;
	if (dpos && room * 2 / (2 * dpos + 1) < cell)
		cell = room * 2 / (2 * dpos + 1);
	room = hres - PAD_SIDE - 10 - kx;
	if (m - 1 - dpos && room * 2 / (2 * (m - 1 - dpos) + 1) < cell)
		cell = room * 2 / (2 * (m - 1 - dpos) + 1);
	x = kx - cell / 2 - dpos * cell - 10;
	y = a.y1 - ALT_H - 20 - 12;

	s.alt = plain(lv_layer_top());
	lv_obj_remove_flag(s.alt, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_style(s.alt, &st_popup, 0);
	lv_obj_set_pos(s.alt, x, y);
	lv_obj_set_size(s.alt, cell * m + 20, ALT_H + 20);
	for (int i = 0; i < m; i++) {
		lv_obj_t *c = lv_label_create(s.alt);

		memcpy(s.alt_txt[i], opt[order[i]], sizeof(s.alt_txt[0]));
		lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_style_text_font(c, &font48, 0);
		lv_obj_set_style_text_align(c, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_style_pad_top(c, (ALT_H - lv_font_get_line_height(&font48)) / 2, 0);
		lv_obj_set_style_bg_color(c, COL_PRIMARY, 0);
		lv_obj_set_style_radius(c, 14, 0);
		lv_obj_set_pos(c, 10 + i * cell, 10);
		lv_obj_set_size(c, cell, ALT_H);
		lv_label_set_text(c, s.alt_txt[i]);
		s.alt_cells[i] = c;
	}
	s.n_alts = m;
	alt_highlight(dpos);
}

/* the option under the finger is selected; only its x position counts */
static void alt_track(void)
{
	lv_point_t p;
	lv_area_t a;
	int i;

	lv_indev_get_point(lv_indev_active(), &p);
	lv_obj_get_coords(s.alt, &a);
	i = p.x < a.x1 + 10 ? 0 : (p.x - a.x1 - 10) / lv_obj_get_width(s.alt_cells[0]);
	if (i >= s.n_alts)
		i = s.n_alts - 1;
	if (i != s.alt_sel)
		alt_highlight(i);
}

static void rebuild_async(void *unused);

static void lang_pick_cb(lv_event_t *e)
{
	int i = (int)(intptr_t)lv_event_get_user_data(e);

	lang_close();
	if (i >= 0 && i != cur_layout) {
		cur_layout = i;
		layout_save();
		s.page = PAGE_LETTERS;
		lv_async_call(rebuild_async, NULL);
	}
}

static void lang_open(struct key *k)
{
	lv_obj_t *list;
	lv_area_t a;
	int32_t h = N_LAYOUTS * 130 + 20, x;

	lang_close();
	lv_obj_get_coords(k->obj, &a);

	/* full-screen catcher: a tap outside the list closes it */
	s.lang = plain(lv_layer_top());
	lv_obj_set_size(s.lang, lv_pct(100), lv_pct(100));
	lv_obj_add_flag(s.lang, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(s.lang, lang_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)-1);

	list = plain(s.lang);
	lv_obj_add_style(list, &st_popup, 0);
	lv_obj_set_size(list, 560, h);
	x = a.x1 < PAD_SIDE ? PAD_SIDE : a.x1;
	if (x + 560 > lv_display_get_horizontal_resolution(NULL) - PAD_SIDE)
		x = lv_display_get_horizontal_resolution(NULL) - PAD_SIDE - 560;
	lv_obj_set_pos(list, x, a.y1 - h - 20);
	lv_obj_add_flag(list, LV_OBJ_FLAG_CLICKABLE); /* taps between the items stay in the list */

	for (int i = 0; i < N_LAYOUTS; i++) {
		lv_obj_t *item = plain(list), *l;

		lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_pos(item, 10, 10 + i * 130);
		lv_obj_set_size(item, 540 - 4, 130);
		lv_obj_set_style_radius(item, 14, 0);
		lv_obj_set_style_bg_color(item, COL_PRESSED, LV_STATE_PRESSED);
		lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
		lv_obj_add_event_cb(item, lang_pick_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
		l = lv_label_create(item);
		lv_obj_set_style_text_font(l, &font36, 0);
		lv_label_set_text_fmt(l, "%s   %s", i == cur_layout ? LV_SYMBOL_OK : "     ",
				      layouts[i].name);
		lv_obj_set_style_text_color(l, i == cur_layout ? COL_PRIMARY : COL_TEXT, 0);
		lv_obj_align(l, LV_ALIGN_LEFT_MID, 30, 0);
	}
}

/* ---- keys ---- */

static void relabel(void)
{
	for (int i = 0; i < s.n_keys; i++) {
		struct key *k = &s.keys[i];
		char buf[16];

		if (k->type == KT_CHAR) {
			cased(k->txt, buf, sizeof(buf));
			lv_label_set_text(k->label, buf);
		} else if (k->type == KT_SHIFT) {
			/* off: outline arrow; once: filled arrow; caps lock: arrow
			 * on a bar, key in the primary colour */
			lv_label_set_text(k->label, s.shift == SHIFT_OFF ? "⇧" : s.shift == SHIFT_ONCE ? "⬆" : "⇪");
			lv_obj_set_style_text_color(k->label, s.shift == SHIFT_ONCE ? COL_PRIMARY :
							      s.shift == SHIFT_LOCK ? lv_color_white() : COL_TEXT, 0);
			lv_obj_remove_style(k->obj, &st_primary, 0);
			lv_obj_remove_style(k->obj, &st_primary_pressed, LV_STATE_PRESSED);
			if (s.shift == SHIFT_LOCK) {
				lv_obj_add_style(k->obj, &st_primary, 0);
				lv_obj_add_style(k->obj, &st_primary_pressed, LV_STATE_PRESSED);
			}
		}
	}
}

static void type_text(const char *txt)
{
	if (s.ta)
		lv_textarea_add_text(s.ta, txt);
	if (s.shift == SHIFT_ONCE) {
		s.shift = SHIFT_OFF;
		relabel();
	}
}

static void backspace(void)
{
	if (s.ta)
		lv_textarea_delete_char(s.ta);
}

static void shift_tap(void)
{
	uint32_t now = lv_tick_get();

	if (s.shift == SHIFT_ONCE && lv_tick_diff(now, s.shift_tick) < DBL_TAP_MS)
		s.shift = SHIFT_LOCK;
	else
		s.shift = s.shift == SHIFT_OFF ? SHIFT_ONCE : SHIFT_OFF;
	s.shift_tick = now;
	relabel();
}

static void key_action(struct key *k)
{
	char buf[16];

	switch (k->type) {
	case KT_CHAR:
		cased(k->txt, buf, sizeof(buf));
		type_text(buf);
		break;
	case KT_SPACE:
		type_text(" ");
		break;
	case KT_SHIFT:
		shift_tap();
		break;
	case KT_PAGE:
		s.page = k->page;
		if (s.shift == SHIFT_ONCE)
			s.shift = SHIFT_OFF;
		lv_async_call(rebuild_async, NULL); /* not while this key's event runs */
		break;
	case KT_GLOBE:
		lang_open(k);
		break;
	case KT_ENTER:
		lv_obj_send_event(s.kb, LV_EVENT_READY, NULL);
		break;
	}
}

/* globe icon: outline, two meridian arcs, axis and equator */
static void draw_globe(lv_event_t *e, struct key *k)
{
	lv_layer_t *layer = lv_event_get_layer(e);
	lv_draw_arc_dsc_t arc;
	lv_draw_line_dsc_t line;
	lv_area_t a;
	int32_t cx, cy, r = 27, d = r * 9 / 10, big = 36; /* big = sqrt(r*r + d*d) */

	lv_obj_get_coords(k->obj, &a);
	cx = (a.x1 + a.x2) / 2;
	cy = (a.y1 + a.y2) / 2;

	lv_draw_arc_dsc_init(&arc);
	arc.color = COL_TEXT;
	arc.width = 4;
	arc.center.x = cx;
	arc.center.y = cy;
	arc.radius = r;
	arc.start_angle = 0;
	arc.end_angle = 360;
	lv_draw_arc(layer, &arc);

	/* meridians: arcs of larger circles through the poles */
	arc.radius = big + 2;
	arc.center.x = cx + d;
	arc.start_angle = 132;
	arc.end_angle = 228;
	lv_draw_arc(layer, &arc);
	arc.center.x = cx - d;
	arc.start_angle = 312;
	arc.end_angle = 48;
	lv_draw_arc(layer, &arc);

	lv_draw_line_dsc_init(&line);
	line.color = COL_TEXT;
	line.width = 4;
	line.p1.x = cx - r;
	line.p1.y = cy;
	line.p2.x = cx + r;
	line.p2.y = cy;
	lv_draw_line(layer, &line);
	line.p1.x = cx;
	line.p1.y = cy - r;
	line.p2.x = cx;
	line.p2.y = cy + r;
	lv_draw_line(layer, &line);
}

static void key_cb(lv_event_t *e)
{
	struct key *k = lv_event_get_user_data(e);

	switch (lv_event_get_code(e)) {
	case LV_EVENT_PRESSED:
		s.long_pressed = 0;
		if (k->type == KT_BKSP)
			backspace(); /* on press, then repeated while held */
		break;
	case LV_EVENT_LONG_PRESSED:
		s.long_pressed = 1;
		if (k->type == KT_CHAR && k->alts[0])
			alt_open(k);
		else if (k->type == KT_SPACE)
			lang_open(k);
		break;
	case LV_EVENT_LONG_PRESSED_REPEAT:
		if (k->type == KT_BKSP)
			backspace();
		break;
	case LV_EVENT_PRESSING:
		if (s.alt)
			alt_track();
		break;
	case LV_EVENT_RELEASED:
		if (s.alt) {
			char txt[8];

			memcpy(txt, s.alt_txt[s.alt_sel], sizeof(txt));
			alt_close();
			type_text(txt);
		} else if (k->type != KT_BKSP && !(s.long_pressed && k->type == KT_SPACE)) {
			key_action(k);
		}
		break;
	case LV_EVENT_PRESS_LOST:
		alt_close();
		break;
	case LV_EVENT_DRAW_MAIN_END:
		if (k->type == KT_GLOBE)
			draw_globe(e, k);
		break;
	default:
		break;
	}
}

/* ---- building the pages ---- */

struct rowkey {
	int type, page;
	float w;          /* in key widths */
	const char *txt;
};

/* alternates of base from a "base:alt alt|..." table, appended to out */
static void find_alts(const char *table, const char *base, char *out, size_t size)
{
	size_t bl = strlen(base);

	for (const char *p = table; p && *p; p = strchr(p, '|') ? strchr(p, '|') + 1 : NULL) {
		if (!strncmp(p, base, bl) && p[bl] == ':') {
			size_t len = strcspn(p + bl + 1, "|"), used = strlen(out);

			if (used + len + 2 < size)
				snprintf(out + used, size - used, "%s%.*s", used ? " " : "", (int)len, p + bl + 1);
			return;
		}
	}
}

static void add_key(int32_t x, int32_t y, int32_t w, const struct rowkey *rk, int row, int col)
{
	struct key *k;
	lv_obj_t *o;
	const struct layout *L = &layouts[cur_layout];

	if (s.n_keys == MAX_KEYS)
		return;
	k = &s.keys[s.n_keys++];
	memset(k, 0, sizeof(*k));
	k->type = rk->type;
	k->page = rk->page;
	snprintf(k->txt, sizeof(k->txt), "%s", rk->txt ? rk->txt : "");

	o = k->obj = plain(s.kb);
	lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
	lv_obj_add_style(o, &st_key, 0);
	if ((k->type != KT_CHAR && k->type != KT_SPACE) || row == 3) /* , and . too */
		lv_obj_add_style(o, &st_fn, 0);
	lv_obj_add_style(o, &st_pressed, LV_STATE_PRESSED);
	if (k->type == KT_ENTER) {
		lv_obj_add_style(o, &st_primary, 0);
		lv_obj_add_style(o, &st_primary_pressed, LV_STATE_PRESSED);
	}
	lv_obj_set_pos(o, x, y);
	lv_obj_set_size(o, w, ROW_H - GAP);
	lv_obj_add_event_cb(o, key_cb, LV_EVENT_ALL, k);

	k->label = lv_label_create(o);
	lv_obj_set_style_text_font(k->label, &font48, 0);
	lv_obj_center(k->label);

	switch (k->type) {
	case KT_CHAR:
		/* top letter row: the digit on long press (typed when released
		 * without sliding), shown in the corner */
		if (s.page == PAGE_LETTERS && row == 0 && col < 10) {
			lv_obj_t *hint = lv_label_create(o);

			k->alts[0] = "1234567890"[col];
			k->digit = 1;
			lv_obj_set_style_text_font(hint, &lv_font_montserrat_28, 0);
			lv_obj_set_style_text_color(hint, COL_HINT, 0);
			lv_label_set_text_fmt(hint, "%c", k->alts[0]);
			lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -10, 4);
		}
		if (s.page == PAGE_LETTERS)
			find_alts(L->alts, k->txt, k->alts, sizeof(k->alts));
		find_alts(sym_alts, k->txt, k->alts, sizeof(k->alts));
		break;
	case KT_BKSP:
		lv_label_set_text(k->label, LV_SYMBOL_BACKSPACE);
		break;
	case KT_PAGE:
		lv_obj_set_style_text_font(k->label, &font36, 0);
		lv_label_set_text(k->label, rk->txt);
		break;
	case KT_GLOBE:
		lv_label_set_text(k->label, "");
		break;
	case KT_SPACE:
		lv_obj_set_style_text_font(k->label, &lv_font_montserrat_28, 0);
		lv_obj_set_style_text_color(k->label, COL_HINT, 0);
		lv_label_set_text(k->label, L->name);
		break;
	case KT_ENTER:
		lv_label_set_text(k->label, LV_SYMBOL_OK);
		break;
	}
}

static void add_row(int row, float grid, const struct rowkey *rk, int n)
{
	float total = 0, unit, x;
	int32_t y = PAD_TOP + row * ROW_H + GAP / 2;

	for (int i = 0; i < n; i++)
		total += rk[i].w;
	unit = (float)(lv_display_get_horizontal_resolution(NULL) - 2 * PAD_SIDE) / grid;
	x = PAD_SIDE + (grid - total) / 2 * unit; /* shorter rows are centred */
	for (int i = 0; i < n; i++) {
		add_key((int32_t)(x + GAP / 2.0f + 0.5f), y, (int32_t)(rk[i].w * unit - GAP + 0.5f), &rk[i], row, i);
		x += rk[i].w * unit;
	}
}

/* splits a space separated row into character keys; returns the count */
static int split_row(const char *spec, char *buf, size_t size, struct rowkey *rk, int max)
{
	char *tok, *save = NULL;
	int n = 0;

	snprintf(buf, size, "%s", spec);
	for (tok = strtok_r(buf, " ", &save); tok && n < max; tok = strtok_r(NULL, " ", &save))
		rk[n++] = (struct rowkey){ KT_CHAR, 0, 1, tok };
	return n;
}

static void build(void)
{
	const struct layout *L = &layouts[cur_layout];
	const char *const *rows = s.page == PAGE_LETTERS ? L->rows : sym_rows[s.page - 1];
	int grid = s.page == PAGE_LETTERS ? L->grid : 10;
	struct rowkey rk[16];
	char buf[128];
	float side;
	int n;

	alt_close();
	lang_close();
	lv_obj_clean(s.kb);
	s.n_keys = 0;

	for (int r = 0; r < 2; r++) {
		n = split_row(rows[r], buf, sizeof(buf), rk, 14);
		add_row(r, grid, rk, n);
	}

	/* third row: shift (or the other symbol page) and backspace at the ends */
	n = split_row(rows[2], buf, sizeof(buf), rk + 1, 13);
	side = (grid - n) / 2.0f;
	if (side > 1.5f)
		side = 1.5f;
	if (side < 1)
		side = 1;
	if (s.page == PAGE_LETTERS)
		rk[0] = (struct rowkey){ KT_SHIFT, 0, side, NULL };
	else if (s.page == PAGE_SYM1)
		rk[0] = (struct rowkey){ KT_PAGE, PAGE_SYM2, side, "=\\<" };
	else
		rk[0] = (struct rowkey){ KT_PAGE, PAGE_SYM1, side, "?123" };
	rk[n + 1] = (struct rowkey){ KT_BKSP, 0, side, NULL };
	add_row(2, grid, rk, n + 2);

	/* bottom row, the same on every page */
	rk[0] = (struct rowkey){ KT_PAGE, s.page == PAGE_LETTERS ? PAGE_SYM1 : PAGE_LETTERS, 1.5f,
				 s.page == PAGE_LETTERS ? "?123" : "ABC" };
	rk[1] = (struct rowkey){ KT_CHAR, 0, 1, "," };
	rk[2] = (struct rowkey){ KT_GLOBE, 0, 1, NULL };
	rk[3] = (struct rowkey){ KT_SPACE, 0, 4, NULL };
	rk[4] = (struct rowkey){ KT_CHAR, 0, 1, "." };
	rk[5] = (struct rowkey){ KT_ENTER, 0, 1.5f, NULL };
	add_row(3, 10, rk, 6);

	relabel();
}

static void rebuild_async(void *unused)
{
	(void)unused;
	if (s.kb)
		build();
}

static void kb_delete_cb(lv_event_t *e)
{
	(void)e;
	alt_close();
	if (s.lang) {
		lv_obj_delete(s.lang);
		s.lang = NULL;
	}
	lv_async_call_cancel(rebuild_async, NULL);
	s.kb = NULL;
	s.ta = NULL;
	s.n_keys = 0;
}

lv_obj_t *kb_create(lv_obj_t *parent)
{
	fonts_init();
	styles_init();
	if (cur_layout < 0)
		layout_load();

	s.kb = plain(parent);
	lv_obj_add_flag(s.kb, LV_OBJ_FLAG_CLICKABLE); /* taps between the keys stay here */
	lv_obj_set_size(s.kb, lv_pct(100), KB_HEIGHT);
	lv_obj_align(s.kb, LV_ALIGN_BOTTOM_MID, 0, 0);
	lv_obj_set_style_bg_opa(s.kb, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(s.kb, COL_BG, 0);
	lv_obj_add_event_cb(s.kb, kb_delete_cb, LV_EVENT_DELETE, NULL);
	s.ta = NULL;
	s.page = PAGE_LETTERS;
	s.shift = SHIFT_OFF;
	build();
	return s.kb;
}

void kb_set_textarea(lv_obj_t *kb, lv_obj_t *ta)
{
	(void)kb;
	s.ta = ta;
}
