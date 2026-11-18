/* Minimal DRM output for the MediaTek DSI panel: the first connected connector,
 * its preferred mode, one XRGB8888 dumb buffer scanned out with drmModeSetCrtc.
 * The legacy API is used on purpose: it is what modetest uses and it works on
 * this 4.14 MTK driver. */
#include "drm_out.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static uint32_t find_prop(int fd, uint32_t obj, uint32_t type, const char *name)
{
	drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj, type);
	uint32_t id = 0;

	if (!props)
		return 0;
	for (uint32_t i = 0; i < props->count_props && !id; i++) {
		drmModePropertyRes *p = drmModeGetProperty(fd, props->props[i]);
		if (p && !strcmp(p->name, name))
			id = p->prop_id;
		drmModeFreeProperty(p);
	}
	drmModeFreeObjectProperties(props);
	return id;
}

static int find_crtc(int fd, drmModeRes *res, drmModeConnector *conn)
{
	for (int e = 0; e < conn->count_encoders; e++) {
		drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[e]);
		if (!enc)
			continue;
		if (enc->crtc_id) {
			int id = enc->crtc_id;
			drmModeFreeEncoder(enc);
			return id;
		}
		for (int c = 0; c < res->count_crtcs; c++) {
			if (enc->possible_crtcs & (1u << c)) {
				int id = res->crtcs[c];
				drmModeFreeEncoder(enc);
				return id;
			}
		}
		drmModeFreeEncoder(enc);
	}
	return -1;
}

static int set_crtc(struct drm_out *o)
{
	return drmModeSetCrtc(o->fd, o->crtc_id, o->fb_id, 0, 0, &o->conn_id, 1,
			      (drmModeModeInfo *)o->mode);
}

int drm_out_open(struct drm_out *o, const char *card)
{
	drmModeRes *res;
	drmModeConnector *conn = NULL;
	struct drm_mode_create_dumb creq = { 0 };
	struct drm_mode_map_dumb mreq = { 0 };
	int crtc;

	memset(o, 0, sizeof(*o));
	o->fd = open(card, O_RDWR | O_CLOEXEC);
	if (o->fd < 0) {
		perror(card);
		return -1;
	}
	res = drmModeGetResources(o->fd);
	if (!res) {
		perror("drmModeGetResources");
		return -1;
	}
	/* first connected connector with modes that is not a writeback connector */
	for (int i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(o->fd, res->connectors[i]);
		if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0 &&
		    conn->connector_type != DRM_MODE_CONNECTOR_WRITEBACK)
			break;
		drmModeFreeConnector(conn);
		conn = NULL;
	}
	if (!conn) {
		fprintf(stderr, "drm: no connected panel\n");
		return -1;
	}
	crtc = find_crtc(o->fd, res, conn);
	if (crtc < 0) {
		fprintf(stderr, "drm: no crtc for connector %u\n", conn->connector_id);
		return -1;
	}
	o->conn_id = conn->connector_id;
	o->crtc_id = crtc;
	o->mode = malloc(sizeof(drmModeModeInfo));
	memcpy(o->mode, &conn->modes[0], sizeof(drmModeModeInfo)); /* preferred mode first */
	for (int i = 0; i < conn->count_modes; i++)
		if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
			memcpy(o->mode, &conn->modes[i], sizeof(drmModeModeInfo));
			break;
		}
	o->width = ((drmModeModeInfo *)o->mode)->hdisplay;
	o->height = ((drmModeModeInfo *)o->mode)->vdisplay;
	o->dpms_prop = find_prop(o->fd, o->conn_id, DRM_MODE_OBJECT_CONNECTOR, "DPMS");
	drmModeFreeConnector(conn);
	drmModeFreeResources(res);

	creq.width = o->width;
	creq.height = o->height;
	creq.bpp = 32;
	if (drmIoctl(o->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		perror("CREATE_DUMB");
		return -1;
	}
	o->handle = creq.handle;
	o->pitch = creq.pitch;
	o->size = creq.size;
	if (drmModeAddFB(o->fd, o->width, o->height, 24, 32, o->pitch, o->handle, &o->fb_id)) {
		perror("drmModeAddFB");
		return -1;
	}
	mreq.handle = o->handle;
	if (drmIoctl(o->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		perror("MAP_DUMB");
		return -1;
	}
	o->map = mmap(NULL, o->size, PROT_READ | PROT_WRITE, MAP_SHARED, o->fd, mreq.offset);
	if (o->map == MAP_FAILED) {
		perror("mmap");
		return -1;
	}
	memset(o->map, 0, o->size);
	if (set_crtc(o)) {
		perror("drmModeSetCrtc");
		return -1;
	}
	fprintf(stderr, "drm: connector %u crtc %u %ux%u pitch %u\n", o->conn_id, o->crtc_id,
		o->width, o->height, o->pitch);
	return 0;
}

/* Panel on/off through the connector's DPMS property; on again, the mode is set again. */
int drm_out_power(struct drm_out *o, int on)
{
	int ret = 0;

	if (o->dpms_prop)
		ret = drmModeConnectorSetProperty(o->fd, o->conn_id, o->dpms_prop,
						  on ? DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF);
	if (on)
		ret |= set_crtc(o);
	if (ret)
		fprintf(stderr, "drm: power %s failed: %s\n", on ? "on" : "off", strerror(errno));
	return ret;
}

void drm_out_dirty(struct drm_out *o, int x1, int y1, int x2, int y2)
{
	drmModeClip clip = { x1, y1, x2 + 1, y2 + 1 };

	drmModeDirtyFB(o->fd, o->fb_id, &clip, 1); /* not supported everywhere; harmless */
}

void drm_out_close(struct drm_out *o)
{
	if (o->map && o->map != MAP_FAILED)
		munmap(o->map, o->size);
	if (o->fb_id)
		drmModeRmFB(o->fd, o->fb_id);
	if (o->fd >= 0)
		close(o->fd);
	free(o->mode);
}
