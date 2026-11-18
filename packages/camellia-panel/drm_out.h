/* Minimal DRM output: one dumb buffer, legacy modeset (the path modetest uses). */
#ifndef CAMELLIA_DRM_OUT_H
#define CAMELLIA_DRM_OUT_H

#include <stdint.h>

struct drm_out {
	int fd;
	uint32_t conn_id, crtc_id, fb_id, handle, dpms_prop;
	uint32_t width, height, pitch;
	uint64_t size;
	uint8_t *map;
	void *mode; /* drmModeModeInfo */
};

int drm_out_open(struct drm_out *o, const char *card);
int drm_out_power(struct drm_out *o, int on);
void drm_out_dirty(struct drm_out *o, int x1, int y1, int x2, int y2);
void drm_out_close(struct drm_out *o);

#endif
