#include <stdlib.h>
#include <stdint.h>
#include "wayland-util.h"

extern const struct wl_interface tizen_surface_shm_flusher_interface;
extern const struct wl_interface wl_surface_interface;

static const struct wl_interface *types[] = {
	&tizen_surface_shm_flusher_interface,
	&wl_surface_interface,
};

static const struct wl_message tizen_surface_shm_requests[] = {
	{ "get_flusher", "no", types + 0 },
};

WL_EXPORT const struct wl_interface tizen_surface_shm_interface = {
	"tizen_surface_shm", 2,
	1, tizen_surface_shm_requests,
	0, NULL,
};

static const struct wl_message tizen_surface_shm_flusher_requests[] = {
	{ "destroy", "", types + 0 },
};

static const struct wl_message tizen_surface_shm_flusher_events[] = {
	{ "flush", "", types + 0 },
	{ "free_flush", "2", types + 0 },
};

WL_EXPORT const struct wl_interface tizen_surface_shm_flusher_interface = {
	"tizen_surface_shm_flusher", 2,
	1, tizen_surface_shm_flusher_requests,
	2, tizen_surface_shm_flusher_events,
};

