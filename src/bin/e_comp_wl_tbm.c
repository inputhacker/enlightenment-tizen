#include "e.h"
#include <wayland-tbm-server.h>
#include <tbm_bufmgr.h>
#include <tbm_surface_internal.h>

static int
_e_comp_wl_tbm_bind_wl_display(struct wayland_tbm_server *tbm_server, struct wl_display *display)
{
   tbm_bufmgr bufmgr = NULL;

   bufmgr = wayland_tbm_server_get_bufmgr(tbm_server);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(bufmgr, EINA_FALSE);

   if (!tbm_bufmgr_bind_native_display(bufmgr, (void *)display))
     {
        e_error_message_show(_("Enlightenment cannot bind native Display TBM!\n"));
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_tbm_init(void)
{
   struct wayland_tbm_server *tbm_server = NULL;

   if (!e_comp)
     {
        e_error_message_show(_("Enlightenment cannot has no e_comp at Wayland TBM!\n"));
        return EINA_FALSE;
     }

   EINA_SAFETY_ON_FALSE_RETURN_VAL(e_comp->wl_comp_data->wl.disp, EINA_FALSE);

   if (e_comp->wl_comp_data->tbm.server)
      return EINA_TRUE;

   tbm_server = wayland_tbm_server_init(e_comp->wl_comp_data->wl.disp, NULL, -1, 0);
   if (!tbm_server)
     {
        e_error_message_show(_("Enlightenment cannot initialize a Wayland TBM!\n"));
        return EINA_FALSE;
     }

   e_comp->wl_comp_data->tbm.server = (void *)tbm_server;

   _e_comp_wl_tbm_bind_wl_display(tbm_server, e_comp->wl_comp_data->wl.disp);

   if (e_comp_socket_init("tbm-drm-auth"))
     PRCTL("[Winsys] change permission and create sym link for %s", "tbm-drm-auth");

   return EINA_TRUE;
}

EINTERN void
e_comp_wl_tbm_shutdown(void)
{
   if (!e_comp)
      return;

   if (!e_comp->wl_comp_data)
      return;

   if (!e_comp->wl_comp_data->tbm.server)
      return;

   wayland_tbm_server_deinit((struct wayland_tbm_server *)e_comp->wl_comp_data->tbm.server);

   e_comp->wl_comp_data->tbm.server = NULL;
}

EINTERN void
e_comp_wl_tbm_buffer_destroy(E_Comp_Wl_Buffer *buffer)
{
   if (!buffer) return;

   if (buffer->tbm_surface)
     {
        tbm_surface_internal_unref(buffer->tbm_surface);
        buffer->tbm_surface = NULL;
     }

   wl_signal_emit(&buffer->destroy_signal, buffer);
   free(buffer);
}

E_API E_Comp_Wl_Buffer *
e_comp_wl_tbm_buffer_get(tbm_surface_h tsurface)
{
   E_Comp_Wl_Buffer *buffer = NULL;

   if (!tsurface) return NULL;

   if (!(buffer = E_NEW(E_Comp_Wl_Buffer, 1)))
      return NULL;

   buffer->type = E_COMP_WL_BUFFER_TYPE_TBM;
   buffer->w = tbm_surface_get_width(tsurface);
   buffer->h = tbm_surface_get_height(tsurface);
   buffer->tbm_surface = tsurface;
   buffer->resource = NULL;
   buffer->shm_buffer = NULL;
   wl_signal_init(&buffer->destroy_signal);

   tbm_surface_internal_ref(tsurface);

   return buffer;
}

EINTERN Eina_Bool
e_comp_wl_tbm_buffer_sync_timeline_used(E_Comp_Wl_Buffer *buffer)
{
   if (!buffer) return EINA_FALSE;

   if (!wayland_tbm_server_buffer_has_sync_timeline(NULL, buffer->resource))
      return EINA_FALSE;

   return EINA_TRUE;
}

EINTERN struct wl_resource *
e_comp_wl_tbm_remote_buffer_get(struct wl_resource *wl_tbm, struct wl_resource *wl_buffer)
{
   struct wl_resource *remote_buffer;
   tbm_surface_h tbm_surface;

   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_tbm, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_buffer,  NULL);

   tbm_surface = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server,
                                                wl_buffer);
   remote_buffer = wayland_tbm_server_export_buffer(e_comp->wl_comp_data->tbm.server,
                                                    wl_tbm, tbm_surface);

   return remote_buffer;
}

EINTERN struct wl_resource *
e_comp_wl_tbm_remote_buffer_get_with_tbm(struct wl_resource *wl_tbm, tbm_surface_h tbm_surface)
{
   struct wl_resource *remote_buffer;

   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_tbm, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surface,  NULL);

   remote_buffer = wayland_tbm_server_export_buffer(e_comp->wl_comp_data->tbm.server,
                                                    wl_tbm, tbm_surface);

   return remote_buffer;
}
