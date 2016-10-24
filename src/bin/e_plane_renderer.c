#include "e.h"

# include <gbm/gbm_tbm.h>
# include <tdm.h>
# include <tdm_helper.h>
# include <tbm_surface.h>
# include <tbm_surface_internal.h>
# include <wayland-tbm-server.h>
# include <Evas_Engine_GL_Drm.h>
# include <sys/eventfd.h>
# if HAVE_LIBGOMP
# include <omp.h>
# endif

# define E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED 7777

typedef enum _E_Plane_Renderer_Client_State
{
   E_PLANE_RENDERER_CLIENT_STATE_NONE,
   E_PLANE_RENDERER_CLIENT_STATE_CANDIDATED,
   E_PLANE_RENDERER_CLIENT_STATE_ACTIVATED,
} E_Plane_Renderer_Client_State;

struct _E_Plane_Renderer_Client
{
   E_Client *ec;

   E_Plane_Renderer_Client_State state;
   E_Plane_Renderer *renderer;

   E_Comp_Wl_Buffer *buffer;
   struct wl_listener buffer_destroy_listener;

   Eina_List *exported_surfaces;
};

/* E_Plane is a child object of E_Output. There is one Output per screen
 * E_plane represents hw overlay and a surface is assigned to disable composition
 * Each Output always has dedicated canvas and a zone
 */
///////////////////////////////////////////
static Eina_List *plane_hdlrs = NULL;
static Eina_Bool renderer_trace_debug = 0;

static struct wl_resource *
_get_wl_buffer(E_Client *ec)
{
   E_Pixmap *pixmap = ec->pixmap;
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);

   if (!buffer) return NULL;

   return buffer->resource;
}

static struct wl_resource *
_get_wl_buffer_ref(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   if (!cdata) return NULL;

   E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata ->buffer_ref;

   if (!buffer_ref->buffer) return NULL;

   return buffer_ref->buffer->resource;
}

struct wayland_tbm_client_queue *
_e_plane_renderer_wayland_tbm_client_queue_get(E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_surface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Comp_Wl_Client_Data *cdata = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_comp_data, NULL);

   cdata = (E_Comp_Wl_Client_Data *)e_pixmap_cdata_get(ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cdata, NULL);

   wl_surface = cdata->wl_surface;
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_surface, NULL);

   cqueue = wayland_tbm_server_client_queue_get(wl_comp_data->tbm.server, wl_surface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, NULL);

   return cqueue;
}

static tbm_surface_h
_e_plane_renderer_client_copied_surface_create(E_Client *ec, Eina_Bool refresh)
{
   tbm_surface_h tsurface = NULL;
   tbm_surface_h new_tsurface = NULL;
   E_Pixmap *pixmap = NULL;
   E_Comp_Wl_Buffer *buffer = NULL;
   tbm_surface_info_s src_info, dst_info;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   int ret = TBM_SURFACE_ERROR_NONE;

   pixmap = ec->pixmap;

   if (refresh)
     e_pixmap_image_refresh(ec->pixmap);

   buffer = e_pixmap_resource_get(pixmap);
   if (!buffer) return NULL;

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, NULL);

   ret = tbm_surface_map(tsurface, TBM_SURF_OPTION_READ, &src_info);
   if (ret != TBM_SURFACE_ERROR_NONE)
     {
        ERR("fail to map the tsurface.");
        return NULL;
     }

   new_tsurface = tbm_surface_create(src_info.width, src_info.height, src_info.format);
   if (!new_tsurface)
     {
        ERR("fail to allocate the new_tsurface.");
        tbm_surface_unmap(tsurface);
        return NULL;
     }

   ret = tbm_surface_map(new_tsurface, TBM_SURF_OPTION_WRITE, &dst_info);
   if (ret != TBM_SURFACE_ERROR_NONE)
     {
        ERR("fail to map the new_tsurface.");
        tbm_surface_destroy(new_tsurface);
        tbm_surface_unmap(tsurface);
        return NULL;
     }

   /* copy from src to dst */
#if HAVE_LIBGOMP
# define LIBGOMP_COPY_THREAD_NUM 4
   if (src_info.planes[0].size > LIBGOMP_COPY_THREAD_NUM)
     {
        size_t step[2];
        step[0] = src_info.planes[0].size / LIBGOMP_COPY_THREAD_NUM;
        step[1] = src_info.planes[0].size - (step[0] * (LIBGOMP_COPY_THREAD_NUM - 1));

        omp_set_num_threads(LIBGOMP_COPY_THREAD_NUM);
        #pragma omp parallel
        #pragma omp sections
          {
             #pragma omp section
               {
                  memcpy(dst_info.planes[0].ptr,
                         src_info.planes[0].ptr,
                         step[0]);
               }
             #pragma omp section
               {
                  memcpy(dst_info.planes[0].ptr + step[0],
                         src_info.planes[0].ptr + step[0],
                         step[0]);
               }
             #pragma omp section
               {
                  memcpy(dst_info.planes[0].ptr + (step[0] * 2),
                         src_info.planes[0].ptr + (step[0] * 2),
                         step[0]);
               }
             #pragma omp section
               {
                  memcpy(dst_info.planes[0].ptr + (step[0] * 3),
                         src_info.planes[0].ptr + (step[0] * 3),
                         step[1]);
               }
          }
     }
   else
     {
        memcpy(dst_info.planes[0].ptr,
               src_info.planes[0].ptr,
               src_info.planes[0].size);
     }
#else /* HAVE_LIBGOMP */
   memcpy(dst_info.planes[0].ptr, src_info.planes[0].ptr, src_info.planes[0].size);
#endif /* end of HAVE_LIBGOMP */

   tbm_surface_unmap(new_tsurface);
   tbm_surface_unmap(tsurface);

   return new_tsurface;
}

static void
_e_plane_renderer_client_copied_surface_destroy(tbm_surface_h tbm_surface)
{
   EINA_SAFETY_ON_NULL_RETURN(tbm_surface);

   tbm_surface_internal_unref(tbm_surface);
}

static void
_e_plane_renderer_client_backup_buffer_cb_destroy(struct wl_listener *listener, void *data)
{
   E_Plane_Renderer_Client *renderer_client = NULL;
   E_Client *ec = NULL;

   renderer_client = container_of(listener, E_Plane_Renderer_Client, buffer_destroy_listener);
   EINA_SAFETY_ON_NULL_RETURN(renderer_client);

   if ((E_Comp_Wl_Buffer *)data != renderer_client->buffer) return;

   ec = renderer_client->ec;
   EINA_SAFETY_ON_NULL_RETURN(ec);

   renderer_client->buffer = NULL;
}

static Eina_Bool
_e_plane_renderer_client_backup_buffer_set(E_Plane_Renderer_Client *renderer_client)
{
   E_Comp_Wl_Buffer *backup_buffer = NULL;
   tbm_surface_h copied_tsurface = NULL;
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, EINA_FALSE);

   ec = renderer_client->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   copied_tsurface = _e_plane_renderer_client_copied_surface_create(ec, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(copied_tsurface, EINA_FALSE);

   backup_buffer = e_comp_wl_tbm_buffer_get(copied_tsurface);
   EINA_SAFETY_ON_NULL_GOTO(backup_buffer, fail);

   if (renderer_client->buffer)
      wl_list_remove(&renderer_client->buffer_destroy_listener.link);

   renderer_client->buffer = backup_buffer;
   wl_signal_add(&backup_buffer->destroy_signal, &renderer_client->buffer_destroy_listener);
   renderer_client->buffer_destroy_listener.notify = _e_plane_renderer_client_backup_buffer_cb_destroy;

   /* reference backup buffer to comp data */
   e_comp_wl_buffer_reference(&ec->comp_data->buffer_ref, backup_buffer);

   /* set the backup buffer resource to the pixmap */
   e_pixmap_resource_set(ec->pixmap, backup_buffer);
   e_pixmap_dirty(ec->pixmap);
   e_pixmap_refresh(ec->pixmap);

   return EINA_TRUE;

fail :
   if (copied_tsurface)
      _e_plane_renderer_client_copied_surface_destroy(copied_tsurface);

   return EINA_FALSE;
}

static Eina_Bool
_e_plane_renderer_client_exported_surface_find(E_Plane_Renderer_Client *renderer_client, tbm_surface_h tsurface)
{
   Eina_List *l_s;
   tbm_surface_h tmp_tsurface = NULL;

   /* destroy the renderer_client */
   EINA_LIST_FOREACH(renderer_client->exported_surfaces, l_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_plane_renderer_surface_find_disp_surface(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s;
   tbm_surface_h tmp_tsurface = NULL;

   EINA_LIST_FOREACH(renderer->disp_surfaces, l_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_plane_renderer_surface_find_released_surface(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s;
   tbm_surface_h tmp_tsurface = NULL;

   EINA_LIST_FOREACH(renderer->released_surfaces, l_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static void
_e_plane_renderer_cb_acquirable(tbm_surface_queue_h surface_queue, void *data)
{
    int fd = (int)data;
    uint64_t value = 1;
    int ret;

    ret = write(fd, &value, sizeof(value));
    if (ret == -1)
       ERR("failed to send acquirable event:%m");
}

static void
_e_plane_renderer_exported_surface_release(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   tbm_surface_h tmp_tsurface = NULL;
   Eina_List *l_s, *ll_s;

   EINA_SAFETY_ON_NULL_RETURN(tsurface);

   EINA_LIST_FOREACH_SAFE(renderer->exported_surfaces, l_s, ll_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;

        if (tmp_tsurface == tsurface)
          {
             e_plane_renderer_surface_queue_release(renderer, tsurface);

             renderer->exported_surfaces = eina_list_remove_list(renderer->exported_surfaces, l_s);
          }
     }

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Release exported Renderer(%p)  tsurface(%p) tqueue(%p)",
           NULL, NULL, renderer, tsurface, renderer->tqueue);
}

static void
_e_plane_renderer_client_exported_surfaces_release(E_Plane_Renderer_Client *renderer_client, E_Plane_Renderer *renderer)
{
   Eina_List *l_s, *ll_s;
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN(renderer_client);


   if (renderer_client->state == E_PLANE_RENDERER_CLIENT_STATE_CANDIDATED)
     {
        e_plane_renderer_surface_queue_release(renderer, renderer->displaying_tsurface);
     }
   else
     {
        EINA_LIST_FOREACH_SAFE(renderer_client->exported_surfaces, l_s, ll_s, tsurface)
          {
             if (!tsurface) continue;

             if (tsurface == renderer->previous_tsurface)
               {
                  _e_plane_renderer_exported_surface_release(renderer, tsurface);
                  renderer_client->exported_surfaces = eina_list_remove_list(renderer_client->exported_surfaces, l_s);
                  break;
               }
          }

        EINA_LIST_FOREACH_SAFE(renderer_client->exported_surfaces, l_s, ll_s, tsurface)
          {
             if (!tsurface) continue;

             if (tsurface == renderer->displaying_tsurface)
               {
                  _e_plane_renderer_exported_surface_release(renderer, tsurface);
                  renderer_client->exported_surfaces = eina_list_remove_list(renderer_client->exported_surfaces, l_s);
                  break;
               }
          }
     }

   EINA_LIST_FOREACH_SAFE(renderer_client->exported_surfaces, l_s, ll_s, tsurface)
     {
        if (!tsurface) continue;

        _e_plane_renderer_exported_surface_release(renderer, tsurface);
        renderer_client->exported_surfaces = eina_list_remove_list(renderer_client->exported_surfaces, l_s);
     }
}

static uint32_t
_e_plane_renderer_client_surface_flags_get(E_Plane_Renderer_Client *renderer_client)
{
   tbm_surface_h tsurface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Client *ec = renderer_client->ec;
   E_Pixmap *pixmap = ec->pixmap;
   uint32_t flags = 0;
   E_Comp_Wl_Buffer *buffer = NULL;

   buffer = e_pixmap_resource_get(pixmap);
   if (!buffer) return 0;

   switch (buffer->type)
     {
       case E_COMP_WL_BUFFER_TYPE_NATIVE:
       case E_COMP_WL_BUFFER_TYPE_VIDEO:
         tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
         EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, 0);

         flags = wayland_tbm_server_get_buffer_flags(wl_comp_data->tbm.server, buffer->resource);
         break;
       default:
         flags = 0;
         break;
     }

   return flags;
}

static Eina_Bool
_e_plane_renderer_surface_find_sent_surface(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s;
   tbm_surface_h tmp_tsurface = NULL;

   EINA_LIST_FOREACH(renderer->sent_surfaces, l_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_plane_renderer_surface_find_exported_surface(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s;
   tbm_surface_h tmp_tsurface = NULL;

   EINA_LIST_FOREACH(renderer->exported_surfaces, l_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;
        if (tmp_tsurface == tsurface) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static void
_e_plane_renderer_surface_exported_surface_destroy_cb(tbm_surface_h tsurface, void *data)
{
   E_Plane_Renderer *renderer = NULL;

   EINA_SAFETY_ON_NULL_RETURN(e_comp);
   EINA_SAFETY_ON_NULL_RETURN(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);
   EINA_SAFETY_ON_NULL_RETURN(data);

   renderer = (E_Plane_Renderer *)data;

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Destroy Renderer(%p)  tsurface(%p) tqueue(%p)",
           NULL, NULL, renderer, tsurface, renderer->tqueue);
}

static void
_e_plane_renderer_surface_release_all_disp_surfaces(E_Plane_Renderer *renderer)
{
   Eina_List *l_s;
   tbm_surface_h tsurface = NULL;

   EINA_LIST_FOREACH(renderer->disp_surfaces, l_s, tsurface)
     {
        if (!tsurface) continue;

        e_plane_renderer_surface_queue_release(renderer, tsurface);

        if (_e_plane_renderer_surface_find_exported_surface(renderer, tsurface))
           renderer->exported_surfaces = eina_list_remove(renderer->exported_surfaces, tsurface);
     }
}

static Eina_Bool
_e_plane_renderer_client_ec_buffer_change_cb(void *data, int type, void *event)
{
   E_Client *ec = NULL;
   E_Event_Client *ev = event;
   E_Plane_Renderer_Client *renderer_client = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;

   if (e_object_is_del(E_OBJECT(ec))) return ECORE_CALLBACK_PASS_ON;

   renderer_client = e_plane_renderer_client_get(ec);
   if (!renderer_client) return ECORE_CALLBACK_PASS_ON;

   if (renderer_client->state != E_PLANE_RENDERER_CLIENT_STATE_NONE) return ECORE_CALLBACK_PASS_ON;

   if (_e_plane_renderer_client_surface_flags_get(renderer_client) != E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED)
      return ECORE_CALLBACK_PASS_ON;

   if (renderer_trace_debug)
      ELOGF("E_PLANE_RENDERER", "Set Backup Buffer     wl_buffer(%p):buffer_change", ec->pixmap, ec, _get_wl_buffer(ec));

   if (!_e_plane_renderer_client_backup_buffer_set(renderer_client))
      ERR("fail to _e_comp_hwc_set_backup_buffer");

   return ECORE_CALLBACK_PASS_ON;
}

EINTERN Eina_Bool
e_plane_renderer_init(void)
{
#ifdef HAVE_HWC
   E_LIST_HANDLER_APPEND(plane_hdlrs, E_EVENT_CLIENT_BUFFER_CHANGE,
                         _e_plane_renderer_client_ec_buffer_change_cb, NULL);
#endif
   return EINA_TRUE;
}

EINTERN void
e_plane_renderer_shutdown(void)
{
  ;
}

static Eina_Bool
_e_plane_renderer_cb_queue_acquirable_event(void *data, Ecore_Fd_Handler *fd_handler)
{
   int len;
   int fd;
   char buffer[64];

   fd = ecore_main_fd_handler_fd_get(fd_handler);
   len = read(fd, buffer, sizeof(buffer));
   if (len == -1)
      ERR("failed to read queue acquire event fd:%m");

   return ECORE_CALLBACK_RENEW;
}

EINTERN E_Plane_Renderer *
e_plane_renderer_new(E_Plane *plane)
{
   E_Plane_Renderer *renderer = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   /* create a renderer */
   renderer = E_NEW(E_Plane_Renderer, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);
   renderer->plane = plane;
   renderer->event_fd = -1;

   if (plane->is_fb)
     {
        renderer->ee = e_comp->ee;
        renderer->evas = ecore_evas_get(renderer->ee);
        ecore_evas_manual_render_set(renderer->ee, 1);
        renderer->event_fd = eventfd(0, EFD_NONBLOCK);
        renderer->event_hdlr = ecore_main_fd_handler_add(renderer->event_fd, ECORE_FD_READ,
                               _e_plane_renderer_cb_queue_acquirable_event, NULL, NULL, NULL);
     }

   return renderer;
}

EINTERN Eina_Bool
e_plane_renderer_ec_set(E_Plane_Renderer *renderer, E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   E_Plane *plane = NULL;
   tbm_surface_queue_h tqueue = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   renderer_client = e_plane_renderer_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, EINA_FALSE);

   if (plane->reserved_memory)
     {
        if (!renderer->ee)
          {
             if (!renderer->tqueue)
               {
                  tqueue = e_plane_renderer_surface_queue_create(renderer, ec->w, ec->h, plane->buffer_flags);
                  EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

                  if (!e_plane_renderer_surface_queue_set(renderer, tqueue))
                    {
                       ERR("Failed to set surface queue at renderer:%p", renderer);
                       tbm_surface_queue_destroy(tqueue);
                       return EINA_FALSE;
                    }
               }
             else
               {
                  if (renderer->tqueue_width != ec->w || renderer->tqueue_height != ec->h)
                    {
                       /* recreate tqueue */
                       e_plane_renderer_surface_queue_destroy(renderer);

                       tqueue = e_plane_renderer_surface_queue_create(renderer, ec->w, ec->h, plane->buffer_flags);
                       EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

                       if (!e_plane_renderer_surface_queue_set(renderer, tqueue))
                         {
                            ERR("Failed to set surface queue at renderer:%p", renderer);
                            e_plane_renderer_surface_queue_destroy(renderer);
                            return EINA_FALSE;
                         }
                    }
               }
          }

        if (renderer_client->renderer && renderer_client->renderer != renderer)
           e_plane_renderer_deactivate(renderer_client->renderer);

        if (!e_plane_renderer_activate(renderer, ec))
          {
             INF("can't activate ec:%p.", ec);
             return EINA_FALSE;
          }
     }
   else
     {
        renderer->ec = ec;
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_renderer_ecore_evas_use(E_Plane_Renderer *renderer)
{
   E_Plane *plane = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   if (!renderer->ee)
     {
        ERR("Renderer:%p can't use cavans", renderer);
        return EINA_FALSE;
     }

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (plane->reserved_memory)
     {
        if (!e_plane_renderer_deactivate(renderer))
          {
             ERR("fail to e_plane_renderer_deactivate.");
             return EINA_FALSE;
          }
     }

   return EINA_TRUE;
}

EINTERN void
e_plane_renderer_del(E_Plane_Renderer *renderer)
{
   E_Plane *plane = NULL;

   EINA_SAFETY_ON_NULL_RETURN(renderer);

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN(plane);

   if (renderer->ee)
     {
        WRN("Delete renderer for canvas");

        if (renderer->event_hdlr)
          {
             ecore_main_fd_handler_del(renderer->event_hdlr);
             renderer->event_hdlr = NULL;
          }

        if (renderer->event_fd)
          {
             close(renderer->event_fd);
             renderer->event_fd = -1;
          }
     }

   if (plane->reserved_memory)
     {
        e_plane_renderer_deactivate(renderer);
        e_plane_renderer_surface_queue_destroy(renderer);
     }

   free(renderer);
   plane->renderer = NULL;
}

EINTERN Eina_Bool
e_plane_renderer_render(E_Plane_Renderer *renderer, Eina_Bool is_fb)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   if (is_fb)
     {
        if (e_comp_canvas_norender_get() > 0)
          {
             if (renderer_trace_debug)
               ELOGF("E_PLANE_RENDERER", "Canvas norender is set. No Display.", NULL, NULL);

             return EINA_TRUE;
          }

        /* render the ecore_evas and
           update_ee is to be true at post_render_cb when the render is successful. */
        TRACE_DS_BEGIN(MANUAL RENDER);

        if (e_plane_renderer_surface_queue_can_dequeue(renderer) || !renderer->tqueue)
           ecore_evas_manual_render(renderer->ee);

        TRACE_DS_END();
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_renderer_activate(E_Plane_Renderer *renderer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   tbm_surface_h tsurface = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   cqueue = _e_plane_renderer_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   /* register the plane client */
   renderer_client = e_plane_renderer_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, EINA_FALSE);

   if (_e_plane_renderer_client_surface_flags_get(renderer_client) != E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED)
     {
        if (renderer->state == E_PLANE_RENDERER_STATE_NONE)
           {
              /* check dequeuable */
              if (!e_plane_renderer_surface_queue_can_dequeue(renderer))
                {
                  INF("There is any dequeuable surface.");
                  return EINA_FALSE;
                }

              wayland_tbm_server_client_queue_activate(cqueue, 0);
           }
        else if ((renderer->state == E_PLANE_RENDERER_STATE_CANDIDATE) && (renderer->ec != ec))
           {
              /* deactive the candidate_ec */
              e_plane_renderer_deactivate(renderer);

              /* check dequeuable */
              if (!e_plane_renderer_surface_queue_can_dequeue(renderer))
                {
                  INF("There is any dequeuable surface.");
                  return EINA_FALSE;
                }

              /* activate the client queue */
              wayland_tbm_server_client_queue_activate(cqueue, 0);
           }
        else if ((renderer->state == E_PLANE_RENDERER_STATE_CANDIDATE) && (renderer->ec == ec))
           {
              INF("ec does not have the scanout surface yet.");
              return EINA_FALSE;
           }
        else
           {
              ERR("NEVER HERE.");
              return EINA_FALSE;
           }

        /* dequeue */
        tsurface = e_plane_renderer_surface_queue_dequeue(renderer);
        if (!tsurface)
          {
             ERR("fail to dequeue surface");
             return EINA_FALSE;
          }

        /* export */
        e_plane_renderer_surface_send(renderer, ec, tsurface);

        if (renderer_trace_debug)
           ELOGF("E_PLANE_RENDERER", "Candidate Renderer(%p)", ec->pixmap, ec, renderer);

        renderer->state = E_PLANE_RENDERER_STATE_CANDIDATE;
        renderer->ec = ec;

        renderer_client->state = E_PLANE_RENDERER_CLIENT_STATE_CANDIDATED;
        renderer_client->renderer = renderer;

        INF("ec does not have the scanout surface.");

        return EINA_FALSE;
     }
   else
     {
        if(renderer->state == E_PLANE_RENDERER_STATE_NONE)
          {
             ERR("renderer state is E_PLANE_RENDERER_STATE_NONE but client has scanout surface");
             return EINA_FALSE;
          }
     }

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Activate Renderer(%p)", ec->pixmap, ec, renderer);

   renderer->ec = ec;
   renderer->state = E_PLANE_RENDERER_STATE_ACTIVATE;

   renderer_client->renderer = renderer;
   renderer_client->state = E_PLANE_RENDERER_CLIENT_STATE_ACTIVATED;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_renderer_deactivate(E_Plane_Renderer *renderer)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   E_Client *ec = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   ec = renderer->ec;
   if (!ec) return EINA_TRUE;

   renderer_client = e_plane_renderer_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, EINA_FALSE);

   cqueue = _e_plane_renderer_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_GOTO(cqueue, done);

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Deactivate Renderer(%p)", ec->pixmap, ec, renderer);

   /* deactive */
   wayland_tbm_server_client_queue_deactivate(cqueue);

   if (_e_plane_renderer_client_surface_flags_get(renderer_client) == E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED)
     {
        if (renderer_trace_debug)
            ELOGF("E_PLANE_RENDERER", "Set Backup Buffer     wl_buffer(%p):Deactivate", ec->pixmap, ec, _get_wl_buffer(ec));

        if (!_e_plane_renderer_client_backup_buffer_set(renderer_client))
           ERR("fail to _e_comp_hwc_set_backup_buffer");

        /* force update */
        e_pixmap_image_refresh(ec->pixmap);
        e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
        e_comp_object_dirty(ec->frame);
        e_comp_object_render(ec->frame);
     }

done:
   _e_plane_renderer_client_exported_surfaces_release(renderer_client, renderer);

   renderer->state = E_PLANE_RENDERER_STATE_NONE;
   renderer->ec = NULL;

   renderer_client->state = E_PLANE_RENDERER_CLIENT_STATE_NONE;
   renderer_client->renderer = NULL;

   return EINA_TRUE;
}

EINTERN E_Plane_Renderer_State
e_plane_renderer_state_get(E_Plane_Renderer *renderer)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, E_PLANE_RENDERER_STATE_NONE);

   return renderer->state;
}

EINTERN void
e_plane_renderer_update_exist_set(E_Plane_Renderer *renderer, Eina_Bool update_exist)
{
   EINA_SAFETY_ON_NULL_RETURN(renderer);

   if (renderer->update_exist != update_exist)
     renderer->update_exist = update_exist;
}

EINTERN Eina_Bool
e_plane_renderer_update_exist_check(E_Plane_Renderer *renderer)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   return renderer->update_exist;
}

EINTERN void
e_plane_renderer_pending_set(E_Plane_Renderer *renderer, Eina_Bool pending)
{
   EINA_SAFETY_ON_NULL_RETURN(renderer);

   if (renderer->pending != pending)
     renderer->pending = pending;
}

EINTERN Eina_Bool
e_plane_renderer_pending_check(E_Plane_Renderer *renderer)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   return renderer->pending;
}

EINTERN E_Plane *
e_plane_renderer_plane_get(E_Plane_Renderer *renderer)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);

   return renderer->plane;
}

EINTERN void
e_plane_renderer_displaying_surface_set(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   EINA_SAFETY_ON_NULL_RETURN(renderer);

   renderer->displaying_tsurface = tsurface;
}

EINTERN tbm_surface_h
e_plane_renderer_displaying_surface_get(E_Plane_Renderer *renderer)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);

   return renderer->displaying_tsurface;
}

EINTERN void
e_plane_renderer_previous_surface_set(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   EINA_SAFETY_ON_NULL_RETURN(renderer);

   renderer->previous_tsurface = tsurface;
}

EINTERN E_Plane_Renderer_Client *
e_plane_renderer_client_new(E_Client *ec)
{
   E_Plane_Renderer_Client *renderer_client = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);

   renderer_client = e_plane_renderer_client_get(ec);
   if (renderer_client) return renderer_client;

   renderer_client = E_NEW(E_Plane_Renderer_Client, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, NULL);

   renderer_client->ec = ec;

   return renderer_client;
}

EINTERN void
e_plane_renderer_client_free(E_Plane_Renderer_Client *renderer_client)
{
   if (!renderer_client) return;

   if (renderer_client->buffer)
      wl_list_remove(&renderer_client->buffer_destroy_listener.link);

   free(renderer_client);
}

EINTERN E_Plane_Renderer_Client *
e_plane_renderer_client_get(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);

   return ec->renderer_client;
}

EINTERN tbm_surface_h
e_plane_renderer_client_surface_recieve(E_Plane_Renderer_Client *renderer_client)
{
   tbm_surface_h tsurface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Client *ec = NULL;
   E_Pixmap *pixmap = NULL;
   uint32_t flags = 0;
   E_Comp_Wl_Buffer *buffer = NULL;
   E_Plane_Renderer *renderer = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client->ec, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client->renderer, NULL);

   ec = renderer_client->ec;
   renderer = renderer_client->renderer;
   pixmap = ec->pixmap;

   buffer = e_pixmap_resource_get(pixmap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, NULL);

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, NULL);

   flags = wayland_tbm_server_get_buffer_flags(wl_comp_data->tbm.server, buffer->resource);

   if (renderer_trace_debug)
     {
        E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
        E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata ->buffer_ref;

        ELOGF("E_PLANE_RENDERER", "Receive Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p) flags(%d)",
              ec->pixmap, ec, renderer, buffer->resource, tsurface, renderer->tqueue, buffer_ref->buffer->resource, flags);
     }
   if (flags != E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED)
     {
        ERR("the flags of the enqueuing surface is %d. need flags(%d).", flags, E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED);
        return NULL;
     }

   /* remove a recieved surface from the sent list in renderer */
   renderer->sent_surfaces = eina_list_remove(renderer->sent_surfaces, (const void *)tsurface);

   return tsurface;
}

EINTERN E_Plane_Renderer *
e_plane_renderer_client_renderer_get(E_Plane_Renderer_Client *renderer_client)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, NULL);

   return renderer_client->renderer;
}

EINTERN tbm_surface_queue_h
e_plane_renderer_surface_queue_create(E_Plane_Renderer *renderer, int width, int height, unsigned int buffer_flags)
{
   tbm_surface_queue_h tqueue = NULL;
   int format = TBM_FORMAT_ARGB8888;
   int queue_size = 3; /* query tdm ????? */

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);

   tqueue = tbm_surface_queue_create(queue_size, width, height, format, buffer_flags);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tqueue, NULL);

   return tqueue;
}

EINTERN Eina_Bool
e_plane_renderer_surface_queue_set(E_Plane_Renderer *renderer, tbm_surface_queue_h tqueue)
{
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   renderer->tqueue = tqueue;
   renderer->tqueue_width = tbm_surface_queue_get_width(tqueue);
   renderer->tqueue_height = tbm_surface_queue_get_height(tqueue);

   if (renderer->disp_surfaces)
      renderer->disp_surfaces = eina_list_free(renderer->disp_surfaces);

   /* dequeue the surfaces if the qeueue is available */
   /* add the surface to the disp_surfaces list, if it is not in the disp_surfaces */
   while (tbm_surface_queue_can_dequeue(renderer->tqueue, 0))
      {
         /* dequeue */
         tsurface = e_plane_renderer_surface_queue_dequeue(renderer);
         if (!tsurface)
            {
               ERR("fail to dequeue surface");
               continue;
            }

        /* if not exist, add the surface to the renderer */
        if (!_e_plane_renderer_surface_find_disp_surface(renderer, tsurface))
           renderer->disp_surfaces = eina_list_append(renderer->disp_surfaces, tsurface);
      }

   _e_plane_renderer_surface_release_all_disp_surfaces(renderer);

   if (renderer->ee)
     {
        tsq_err = tbm_surface_queue_add_acquirable_cb(renderer->tqueue, _e_plane_renderer_cb_acquirable, (void *)renderer->event_fd);
        if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
          {
             ERR("fail to add dequeuable cb");
             return EINA_FALSE;
          }
     }

   return EINA_TRUE;
}

EINTERN void
e_plane_renderer_surface_queue_destroy(E_Plane_Renderer *renderer)
{
   if (!renderer) return;

   if (renderer->tqueue)
     {
        tbm_surface_queue_destroy(renderer->tqueue);
        renderer->tqueue = NULL;
        renderer->tqueue_width = 0;
        renderer->tqueue_height = 0;
     }

   renderer->disp_surfaces = eina_list_free(renderer->disp_surfaces);
}

EINTERN tbm_surface_h
e_plane_renderer_surface_queue_acquire(E_Plane_Renderer *renderer)
{
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, NULL);

   if (tbm_surface_queue_can_acquire(tqueue, 0))
     {
        tsq_err = tbm_surface_queue_acquire(tqueue, &tsurface);
        if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
          {
             ERR("Failed to acquire tbm_surface from tbm_surface_queue(%p): tsq_err = %d", tqueue, tsq_err);
             return NULL;
          }
     }
   else
     {
        return NULL;
     }

   if (_e_plane_renderer_surface_find_released_surface(renderer, tsurface))
      renderer->released_surfaces = eina_list_remove(renderer->released_surfaces, tsurface);

   /* if not exist, add the surface to the renderer */
   if (!_e_plane_renderer_surface_find_disp_surface(renderer, tsurface))
      renderer->disp_surfaces = eina_list_append(renderer->disp_surfaces, tsurface);

   /* debug */
   if (renderer_trace_debug)
     {
        E_Client *ec = renderer->ec;
        if (ec)
          ELOGF("E_PLANE_RENDERER", "Acquire Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
                ec->pixmap, ec, renderer, _get_wl_buffer(ec), tsurface, tqueue, _get_wl_buffer_ref(ec));
        else
          ELOGF("E_PLANE_RENDERER", "Acquire Renderer(%p)  tsurface(%p) tqueue(%p)",
                NULL, NULL, renderer, tsurface, tqueue);
     }

   return tsurface;
}

EINTERN void
e_plane_renderer_surface_queue_release(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   tbm_surface_queue_h tqueue = NULL;

   EINA_SAFETY_ON_NULL_RETURN(renderer);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN(tqueue);

   if (_e_plane_renderer_surface_find_released_surface(renderer, tsurface)) return;

   /* debug */
   if (renderer_trace_debug)
     {
        E_Client *ec = renderer->ec;
        if (ec)
          ELOGF("E_PLANE_RENDERER", "Release Renderer(%p)     wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
                ec->pixmap, ec, renderer, _get_wl_buffer(ec), tsurface, renderer->tqueue, _get_wl_buffer_ref(ec));
        else
          ELOGF("E_PLANE_RENDERER", "Release Renderer(%p)  tsurface(%p) tqueue(%p)",
                NULL, NULL, renderer, tsurface, renderer->tqueue);
     }

   tsq_err = tbm_surface_queue_release(tqueue, tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("Failed to release tbm_surface(%p) from tbm_surface_queue(%p): tsq_err = %d", tsurface, tqueue, tsq_err);
        return;
     }

   renderer->released_surfaces = eina_list_append(renderer->released_surfaces, tsurface);
}

EINTERN Eina_Bool
e_plane_renderer_surface_queue_enqueue(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, EINA_FALSE);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   /* debug */
   if (renderer_trace_debug)
    {
        E_Client *ec = renderer->ec;
        ELOGF("E_PLANE_RENDERER", "Enqueue Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
              ec->pixmap, ec, renderer, _get_wl_buffer(ec), tsurface, renderer->tqueue, _get_wl_buffer_ref(ec));
    }

   tsq_err = tbm_surface_queue_enqueue(tqueue, tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("tbm_surface_queue_enqueue failed. tbm_surface_queue(%p) tbm_surface(%p)", tqueue, tsurface);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_renderer_surface_queue_can_dequeue(E_Plane_Renderer *renderer)
{
   tbm_surface_queue_h tqueue = NULL;
   int num_free = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   num_free = tbm_surface_queue_can_dequeue(tqueue, 0);

   if (num_free <= 0) return EINA_FALSE;

   return EINA_TRUE;
}

EINTERN tbm_surface_h
e_plane_renderer_surface_queue_dequeue(E_Plane_Renderer *renderer)
{
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, NULL);

   tsq_err = tbm_surface_queue_dequeue(tqueue, &tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail to tbm_surface_queue_dequeue");
        return NULL;
     }

   if (_e_plane_renderer_surface_find_released_surface(renderer, tsurface))
      renderer->released_surfaces = eina_list_remove(renderer->released_surfaces, tsurface);

   /* debug */
   if (renderer_trace_debug)
     {
         E_Client *ec = renderer->ec;
         if (ec)
           ELOGF("E_PLANE_RENDERER", "Dequeue Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p) wl_buffer_ref(%p)",
                 ec->pixmap, ec, renderer, _get_wl_buffer(ec), tsurface, renderer->tqueue, _get_wl_buffer_ref(ec));
         else
           ELOGF("E_PLANE_RENDERER", "Dequeue Renderer(%p)  tsurface(%p) tqueue(%p)",
                 NULL, NULL, renderer, tsurface, renderer->tqueue);
     }
   return tsurface;
}

EINTERN Eina_Bool
e_plane_renderer_surface_queue_clear(E_Plane_Renderer *renderer)
{
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   while ((tsurface = e_plane_renderer_surface_queue_acquire(renderer)))
      e_plane_renderer_surface_queue_release(renderer, tsurface);

  return EINA_TRUE;
}

EINTERN void
e_plane_renderer_surface_send(E_Plane_Renderer *renderer, E_Client *ec, tbm_surface_h tsurface)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_buffer = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;

   EINA_SAFETY_ON_NULL_RETURN(renderer);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);

   renderer_client = e_plane_renderer_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(renderer_client);

   cqueue = _e_plane_renderer_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(cqueue);

   if (!_e_plane_renderer_surface_find_exported_surface(renderer, tsurface))
     {
        /* export the tbm_surface(wl_buffer) to the client_queue */
        wl_buffer = wayland_tbm_server_client_queue_export_buffer(cqueue, tsurface,
                E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED, _e_plane_renderer_surface_exported_surface_destroy_cb,
                (void *)renderer);

        renderer->exported_surfaces = eina_list_append(renderer->exported_surfaces, tsurface);

        if (!_e_plane_renderer_client_exported_surface_find(renderer_client, tsurface))
           renderer_client->exported_surfaces = eina_list_append(renderer_client->exported_surfaces, tsurface);

        if (wl_buffer && renderer_trace_debug)
           ELOGF("E_PLANE_RENDERER", "Export  Renderer(%p)  wl_buffer(%p) tsurface(%p) tqueue(%p)",
                 ec->pixmap, ec, renderer, wl_buffer, tsurface, renderer->tqueue);
     }

   /* add a sent surface to the sent list in renderer if it is not in the list */
   if (!_e_plane_renderer_surface_find_sent_surface(renderer, tsurface))
     renderer->sent_surfaces = eina_list_append(renderer->sent_surfaces, tsurface);
}

EINTERN void
e_plane_renderer_hwc_trace_debug(Eina_Bool onoff)
{
   if (onoff == renderer_trace_debug) return;
   renderer_trace_debug = onoff;
   INF("Renderer: hwc trace_debug is %s", onoff?"ON":"OFF");
}
