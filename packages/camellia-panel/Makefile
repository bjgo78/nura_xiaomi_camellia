# camellia-panel: status page and setup UI on the phone's screen (LVGL, DRM, evdev)
LVGL_DIR ?= lvgl
PREFIX ?= /usr

CFLAGS ?= -O2
CFLAGS += -Wall -I. -I$(LVGL_DIR) -DLV_CONF_INCLUDE_SIMPLE $(shell pkg-config --cflags libdrm)
LDLIBS += $(shell pkg-config --libs libdrm) -lm

SRCS := main.c drm_out.c status.c dialog.c exec.c wifi.c keyboard.c battery.c power.c $(shell find $(LVGL_DIR)/src -name '*.c')
OBJS := $(SRCS:%.c=build/%.o)

camellia-panel: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: %.c lv_conf.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

install: camellia-panel
	install -Dm755 camellia-panel $(DESTDIR)$(PREFIX)/bin/camellia-panel

clean:
	rm -rf build camellia-panel

.PHONY: install clean
