#include "e.h"

# include <gbm/gbm_tbm.h>
# include <tdm.h>
# include <tdm_helper.h>
# include <tbm_surface.h>
# include <tbm_surface_internal.h>
# include <wayland-tbm-server.h>
# include <sys/eventfd.h>
# include <pixman.h>
# if HAVE_LIBGOMP
# include <omp.h>
# endif

# define E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED 7777

typedef enum _E_Plane_Renderer_Client_State
{
   E_PLANE_RENDERER_CLIENT_STATE_NONE,
   E_PLANE_RENDERER_CLIENT_STATE_CANDIDATED,
   E_PLANE_RENDERER_CLIENT_STATE_ACTIVATED,
   E_PLANE_RENDERER_CLIENT_STATE_PENDING_DEACTIVATED,
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

struct _E_Plane_Renderer_Buffer
{
   E_Plane_Renderer *renderer;
   tbm_surface_h tsurface;

   Ecore_Timer *release_timer;

   Eina_Bool dequeued;
   Eina_Bool exported;
   struct wl_resource *exported_wl_buffer;
   Eina_Bool usable;

   Eina_Bool alloced;

   Eina_Bool acquired;
};

/* E_Plane is a child object of E_Output. There is one Output per screen
 * E_plane represents hw overlay and a surface is assigned to disable composition
 * Each Output always has dedicated canvas and a zone
 */
///////////////////////////////////////////
static Eina_List *plane_hdlrs = NULL;
static Eina_Bool renderer_trace_debug = 0;

static int render_buffers_key;
#define RENDER_BUFFERS_KEY  (unsigned long)(&render_buffers_key)

static E_Comp_Wl_Buffer *
_get_comp_wl_buffer(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   if (!cdata) return NULL;

   E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata->buffer_ref;

   return buffer_ref->buffer;
}

static struct wl_resource *
_get_wl_buffer(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   if (!cdata) return NULL;

   E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata->buffer_ref;

   if (!buffer_ref->buffer) return NULL;

   return buffer_ref->buffer->resource;
}

static tbm_surface_queue_h
_get_tbm_surface_queue(Ecore_Evas *ee)
{
   return e_comp->e_comp_screen->tqueue;
}

static void
_e_plane_renderer_render_buffers_free(void *data)
{
   Eina_List *render_buffers = (Eina_List *)data;
   E_Comp_Wl_Buffer_Ref *buffer_ref;
   Eina_List *l, *ll;

   if (!render_buffers) return;

   EINA_LIST_FOREACH_SAFE(render_buffers, l, ll, buffer_ref)
     {
        e_comp_wl_buffer_reference(buffer_ref, NULL);
        render_buffers = eina_list_remove_list(render_buffers, l);
        E_FREE(buffer_ref);
     }
}

static void
_e_plane_renderer_buffer_remove(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s, *ll_s;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;

   EINA_LIST_FOREACH_SAFE(renderer->renderer_buffers, l_s, ll_s, renderer_buffer)
     {
        if (!renderer_buffer) continue;

        if (renderer_buffer->tsurface == tsurface)
          {
             if (renderer->ee)
               tbm_surface_internal_delete_user_data(tsurface, RENDER_BUFFERS_KEY);

             E_FREE_FUNC(renderer_buffer->release_timer, ecore_timer_del);
             renderer->renderer_buffers = eina_list_remove_list(renderer->renderer_buffers, l_s);
             E_FREE(renderer_buffer);
          }
     }
}

static Eina_Bool
_e_plane_renderer_buffer_add(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;

   EINA_LIST_FOREACH(renderer->renderer_buffers, l_s, renderer_buffer)
     {
        if (!renderer_buffer) continue;

        if (renderer_buffer->tsurface == tsurface)
          return EINA_TRUE;
     }

   renderer_buffer = E_NEW(E_Plane_Renderer_Buffer, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_buffer, EINA_FALSE);

   renderer_buffer->tsurface = tsurface;
   renderer_buffer->renderer = renderer;

   renderer->renderer_buffers = eina_list_append(renderer->renderer_buffers, renderer_buffer);

   if (renderer->ee)
     {
        tbm_surface_internal_add_user_data(tsurface, RENDER_BUFFERS_KEY,
                                           _e_plane_renderer_render_buffers_free);
     }

   return EINA_TRUE;
}

static E_Plane_Renderer_Buffer *
_e_plane_renderer_buffer_get(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   Eina_List *l_s;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;

   EINA_LIST_FOREACH(renderer->renderer_buffers, l_s, renderer_buffer)
     {
        if (!renderer_buffer) continue;

        if (renderer_buffer->tsurface == tsurface)
          return renderer_buffer;
     }

   return NULL;
}

static E_Plane_Renderer_Buffer *
_e_plane_renderer_client_renderer_buffer_get(E_Plane_Renderer_Client *renderer_client)
{
   E_Comp_Wl_Buffer *buffer = NULL;
   E_Client *ec = NULL;
   tbm_surface_h tsurface;
   E_Plane_Renderer *renderer = NULL;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;

   ec = renderer_client->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   renderer = renderer_client->renderer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   buffer = _get_comp_wl_buffer(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, EINA_FALSE);

   tsurface = wayland_tbm_server_get_surface(NULL, buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, EINA_FALSE);

   renderer_buffer = _e_plane_renderer_buffer_get(renderer, tsurface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_buffer, EINA_FALSE);

   return renderer_buffer;
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
_e_plane_renderer_client_copied_surface_create(E_Plane_Renderer_Client *renderer_client, Eina_Bool refresh)
{
   tbm_surface_h tsurface = NULL;
   tbm_surface_h new_tsurface = NULL;
   E_Comp_Wl_Buffer *buffer = NULL;
   tbm_surface_info_s src_info, dst_info;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   int ret = TBM_SURFACE_ERROR_NONE;
   E_Client *ec = renderer_client->ec;
   E_Plane_Renderer *renderer = renderer_client->renderer;
   E_Plane *plane = NULL;

   if (refresh)
     e_pixmap_image_refresh(ec->pixmap);

   buffer = _get_comp_wl_buffer(ec);
   if (buffer)
     {
        tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
        EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, NULL);
     }
   else
     {
        EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);
        plane = renderer->plane;

        EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);
        tsurface = plane->tsurface;

        EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, NULL);
     }

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
# define LIBGOMP_COPY_PAGE_SIZE getpagesize()
# define PAGE_ALIGN(addr) ((addr)&(~((LIBGOMP_COPY_PAGE_SIZE)-1)))
   if (src_info.planes[0].size > (LIBGOMP_COPY_THREAD_NUM * LIBGOMP_COPY_PAGE_SIZE))
     {
        size_t step[2];
        step[0] = PAGE_ALIGN(src_info.planes[0].size / LIBGOMP_COPY_THREAD_NUM);
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

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Create backup buffer   wl_buffer(%p) tsurface(%p) new_tsurface(%p)",
           ec, _get_wl_buffer(ec), tsurface, new_tsurface);

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
   E_Comp_Wl_Buffer *buffer, *backup_buffer = NULL;
   tbm_surface_h copied_tsurface = NULL;
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, EINA_FALSE);

   ec = renderer_client->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   copied_tsurface = _e_plane_renderer_client_copied_surface_create(renderer_client, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(copied_tsurface, EINA_FALSE);

   backup_buffer = e_comp_wl_tbm_buffer_get(copied_tsurface);
   EINA_SAFETY_ON_NULL_GOTO(backup_buffer, fail);

   buffer = _get_comp_wl_buffer(ec);
   if (buffer)
     backup_buffer->transform = buffer->transform;

   if (renderer_client->buffer)
     wl_list_remove(&renderer_client->buffer_destroy_listener.link);

   renderer_client->buffer = backup_buffer;
   wl_signal_add(&backup_buffer->destroy_signal, &renderer_client->buffer_destroy_listener);
   renderer_client->buffer_destroy_listener.notify = _e_plane_renderer_client_backup_buffer_cb_destroy;

   e_comp_wl_surface_attach(ec, backup_buffer);
   tbm_surface_internal_unref(copied_tsurface);

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
    int fd = (intptr_t)data;
    uint64_t value = 1;
    int ret;

    ret = write(fd, &value, sizeof(value));
    if (ret == -1)
      ERR("failed to send acquirable event:%m");
}

static void
_e_plane_renderer_cb_surface_queue_destroy(tbm_surface_queue_h surface_queue, void *data)
{
   E_Plane_Renderer *renderer = NULL;
   tbm_surface_h tsurface = NULL;

   renderer = (E_Plane_Renderer *)data;
   EINA_SAFETY_ON_NULL_RETURN(renderer);

   renderer->tqueue = NULL;
   renderer->tqueue_width = 0;
   renderer->tqueue_height = 0;

   EINA_LIST_FREE(renderer->disp_surfaces, tsurface)
     _e_plane_renderer_buffer_remove(renderer, tsurface);
}

static void
_e_plane_renderer_exported_surface_release(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   tbm_surface_h tmp_tsurface = NULL;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;
   Eina_List *l_s, *ll_s;

   EINA_SAFETY_ON_NULL_RETURN(tsurface);

   renderer_buffer = _e_plane_renderer_buffer_get(renderer, tsurface);
   EINA_SAFETY_ON_FALSE_RETURN(renderer_buffer);

   EINA_LIST_FOREACH_SAFE(renderer->exported_surfaces, l_s, ll_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;

        if (tmp_tsurface == tsurface)
          {
             if (!renderer_buffer->acquired && renderer_buffer->usable)
               e_plane_renderer_surface_queue_release(renderer, tsurface);

             renderer->exported_surfaces = eina_list_remove_list(renderer->exported_surfaces, l_s);
          }
     }

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Import  Renderer(%p)        tsurface(%p) tqueue(%p)",
           NULL, renderer, tsurface, renderer->tqueue);
}

static Eina_Bool
_e_plane_renderer_release_exported_renderer_buffer(E_Plane_Renderer *renderer, E_Plane_Renderer_Buffer *renderer_buffer)
{
   E_Plane_Renderer_Client *renderer_client = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_buffer, EINA_FALSE);

   E_FREE_FUNC(renderer_buffer->release_timer, ecore_timer_del);

   _e_plane_renderer_exported_surface_release(renderer, renderer_buffer->tsurface);

   renderer_buffer->exported = EINA_FALSE;
   renderer_buffer->exported_wl_buffer = NULL;
   renderer_buffer->usable = EINA_FALSE;
   renderer_buffer->dequeued = EINA_FALSE;

  if (!eina_list_count(renderer->exported_surfaces))
    {
       if ((renderer->state == E_PLANE_RENDERER_STATE_CANDIDATE) ||
           (renderer->state == E_PLANE_RENDERER_STATE_ACTIVATE))
         e_plane_renderer_reserved_deactivate(renderer);

       renderer_client = e_plane_renderer_client_get(renderer->ec);
       if (renderer_client)
         {
            renderer_client->state = E_PLANE_RENDERER_CLIENT_STATE_NONE;
            renderer_client->renderer = NULL;
         }

       renderer->state = E_PLANE_RENDERER_STATE_NONE;
       renderer->ec = NULL;
       renderer->pending_deactivate = EINA_FALSE;
    }

   return EINA_TRUE;
}

static Eina_Bool
_e_plane_renderer_receive_renderer_buffer(E_Plane_Renderer *renderer, E_Plane_Renderer_Buffer *renderer_buffer)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_buffer, EINA_FALSE);

   renderer_buffer->dequeued = EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_plane_renderer_buffer_release_timeout(void *data)
{
   E_Plane_Renderer_Buffer *renderer_buffer = data;
   E_Plane_Renderer *renderer = NULL;

   renderer = renderer_buffer->renderer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   renderer_buffer->release_timer = NULL;

   if (renderer_buffer->exported)
     _e_plane_renderer_release_exported_renderer_buffer(renderer, renderer_buffer);

   ERR("renderer buffer release timeout!! renderer(%p) tsurface(%p) tqueue(%p)", renderer,
                                                                                 renderer_buffer->tsurface,
                                                                                 renderer->tqueue);

   return ECORE_CALLBACK_CANCEL;
}

static void
_e_plane_renderer_exported_surfaces_timer_set(E_Plane_Renderer *renderer)
{
   Eina_List *l_s, *ll_s;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN(renderer);

   EINA_LIST_FOREACH_SAFE(renderer->exported_surfaces, l_s, ll_s, tsurface)
     {
        if (!tsurface) continue;

        renderer_buffer = _e_plane_renderer_buffer_get(renderer, tsurface);
        if (!renderer_buffer) continue;

        if (!renderer_buffer->usable)
          {
             E_FREE_FUNC(renderer_buffer->release_timer, ecore_timer_del);
             continue;
          }

        if (!renderer_buffer->release_timer)
          renderer_buffer->release_timer = ecore_timer_add(0.5,
                                                          _e_plane_renderer_buffer_release_timeout,
                                                          renderer_buffer);
     }
}


static void
_e_plane_renderer_exported_surfaces_release(E_Plane_Renderer *renderer, Eina_Bool sync)
{
   E_Plane *plane = NULL;
   Eina_List *l_s, *ll_s;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;
   tbm_surface_h tsurface = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;

   EINA_SAFETY_ON_NULL_RETURN(renderer);

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN(plane);

   EINA_LIST_FOREACH_SAFE(renderer->exported_surfaces, l_s, ll_s, tsurface)
     {
        if (!tsurface) continue;

        renderer_buffer = _e_plane_renderer_buffer_get(renderer, tsurface);
        if (!renderer_buffer) continue;

        if (renderer_buffer->acquired || !renderer_buffer->usable)
          {
             renderer_buffer->exported = EINA_FALSE;
             renderer_buffer->exported_wl_buffer = NULL;
             renderer_buffer->usable = EINA_FALSE;
             renderer->exported_surfaces = eina_list_remove_list(renderer->exported_surfaces, l_s);

             if (renderer_trace_debug)
               ELOGF("E_PLANE_RENDERER", "Import  Renderer(%p)        tsurface(%p) tqueue(%p)",
                     NULL, renderer, tsurface, renderer->tqueue);

             continue;
          }

        if (sync && renderer_buffer->dequeued)
          {
             if (!renderer_buffer->release_timer)
               renderer_buffer->release_timer = ecore_timer_add(0.5,
                                                                _e_plane_renderer_buffer_release_timeout,
                                                                renderer_buffer);
             continue;
          }

        renderer_buffer->exported = EINA_FALSE;
        renderer_buffer->exported_wl_buffer = NULL;
        renderer_buffer->usable = EINA_FALSE;

        E_FREE_FUNC(renderer_buffer->release_timer, ecore_timer_del);
        e_plane_renderer_surface_queue_release(renderer, tsurface);
        renderer->exported_surfaces = eina_list_remove_list(renderer->exported_surfaces, l_s);

        if (renderer_trace_debug)
          ELOGF("E_PLANE_RENDERER", "Import  Renderer(%p)        tsurface(%p) tqueue(%p)",
                NULL, renderer, tsurface, renderer->tqueue);
      }

   if (!eina_list_count(renderer->exported_surfaces))
     {
        renderer_client = e_plane_renderer_client_get(renderer->ec);
        if (renderer_client)
          {
             renderer_client->state = E_PLANE_RENDERER_CLIENT_STATE_NONE;
             renderer_client->renderer = NULL;
          }

        renderer->state = E_PLANE_RENDERER_STATE_NONE;
        renderer->ec = NULL;
        renderer->pending_deactivate = EINA_FALSE;
     }
}

static void
_e_plane_renderer_client_exported_surfaces_release(E_Plane_Renderer_Client *renderer_client, E_Plane_Renderer *renderer)
{
   Eina_List *l_s, *ll_s;
   tbm_surface_h tsurface = NULL;
   E_Plane *plane = renderer->plane;

   EINA_SAFETY_ON_NULL_RETURN(renderer_client);
   EINA_SAFETY_ON_NULL_RETURN(plane);

   if (renderer->state == E_PLANE_RENDERER_STATE_CANDIDATE)
     {
        renderer_client->exported_surfaces = eina_list_free(renderer_client->exported_surfaces);
     }
   else
     {
        if (eina_list_count(renderer_client->exported_surfaces) < 3)
          {
             renderer_client->exported_surfaces = eina_list_free(renderer_client->exported_surfaces);
          }
        else
          {
             EINA_LIST_FOREACH_SAFE(renderer_client->exported_surfaces, l_s, ll_s, tsurface)
               {
                  if (!tsurface) continue;

                  renderer_client->exported_surfaces = eina_list_remove_list(renderer_client->exported_surfaces, l_s);

                  if (eina_list_count(plane->commit_data_list)) continue;

                  if (tsurface == renderer->previous_tsurface)
                    _e_plane_renderer_exported_surface_release(renderer, tsurface);
               }
          }

        if (!eina_list_count(plane->commit_data_list) && !tbm_surface_queue_can_dequeue(renderer->tqueue, 0))
          {
             EINA_LIST_FOREACH_SAFE(renderer->disp_surfaces, l_s, ll_s, tsurface)
               {
                  if (!tsurface) continue;

                  if (tsurface != renderer->displaying_tsurface)
                    {
                       _e_plane_renderer_exported_surface_release(renderer, tsurface);
                       ERR("Force release plane:%p tsurface:%p to queue:%p\n", plane, tsurface, renderer->tqueue);
                    }
               }
          }
     }
}

static uint32_t
_e_plane_renderer_client_surface_flags_get(E_Plane_Renderer_Client *renderer_client)
{
   tbm_surface_h tsurface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Client *ec = renderer_client->ec;
   uint32_t flags = 0;
   E_Comp_Wl_Buffer *buffer = NULL;

   buffer = _get_comp_wl_buffer(ec);
   if (!buffer) return 0;
   if (!buffer->resource) return 0;

   switch (buffer->type)
     {
       case E_COMP_WL_BUFFER_TYPE_NATIVE:
       case E_COMP_WL_BUFFER_TYPE_VIDEO:
       case E_COMP_WL_BUFFER_TYPE_TBM:
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
_e_plane_renderer_exported_surface_dequeue_cb(struct wayland_tbm_client_queue *cqueue,
                                              tbm_surface_h tsurface,
                                              void *data)
{
   E_Plane *plane = NULL;
   E_Plane_Renderer *renderer = NULL;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;

   EINA_SAFETY_ON_NULL_RETURN(tsurface);
   EINA_SAFETY_ON_NULL_RETURN(data);

   plane = (E_Plane *)data;
   EINA_SAFETY_ON_NULL_RETURN(plane);

   renderer = plane->renderer;
   if (!renderer) return;

   renderer_buffer = _e_plane_renderer_buffer_get(renderer, tsurface);
   EINA_SAFETY_ON_NULL_RETURN(renderer_buffer);

   renderer_buffer->dequeued = EINA_TRUE;

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Client  Renderer(%p)        tsurface(%p) tqueue(%p) Dequeued",
           NULL, renderer, tsurface, renderer->tqueue);
}

static void
_e_plane_renderer_surface_exported_surface_detach_cb(struct wayland_tbm_client_queue *cqueue,
                                                     tbm_surface_h tsurface,
                                                     void *data)
{
   E_Plane *plane = NULL;
   E_Plane_Renderer *renderer = NULL;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;

   EINA_SAFETY_ON_NULL_RETURN(e_comp);
   EINA_SAFETY_ON_NULL_RETURN(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);
   EINA_SAFETY_ON_NULL_RETURN(data);

   if (!e_comp->hwc_use_detach) return;

   plane = (E_Plane *)data;
   EINA_SAFETY_ON_NULL_RETURN(plane);

   renderer = plane->renderer;
   if (!renderer) return;

   renderer_buffer = _e_plane_renderer_buffer_get(renderer, tsurface);
   EINA_SAFETY_ON_NULL_RETURN(renderer_buffer);

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Detach  Renderer(%p)        tsurface(%p) tqueue(%p)",
           NULL, renderer, tsurface, renderer->tqueue);

   if (renderer_buffer->exported)
     {
        if (!_e_plane_renderer_release_exported_renderer_buffer(renderer, renderer_buffer))
          ERR("failed to _e_plane_renderer_release_exported_renderer_buffer");
     }
}

static void
_e_plane_renderer_surface_exported_surface_destroy_cb(tbm_surface_h tsurface, void *data)
{
   E_Plane *plane = NULL;
   E_Plane_Renderer *renderer = NULL;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;

   EINA_SAFETY_ON_NULL_RETURN(e_comp);
   EINA_SAFETY_ON_NULL_RETURN(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);
   EINA_SAFETY_ON_NULL_RETURN(data);

   plane = (E_Plane *)data;
   EINA_SAFETY_ON_NULL_RETURN(plane);

   renderer = plane->renderer;
   if (!renderer) return;

   if (renderer->exported_wl_buffer_count > 0) renderer->exported_wl_buffer_count--;

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Destroy Renderer(%p)        tsurface(%p) tqueue(%p)",
           NULL, renderer, tsurface, renderer->tqueue);

   renderer_buffer = _e_plane_renderer_buffer_get(renderer, tsurface);
   if (renderer_buffer)
     {
        if (renderer->state == E_PLANE_RENDERER_STATE_PENDING_DEACTIVATE)
          {
             if (renderer_buffer->exported)
               {
                  if (!_e_plane_renderer_release_exported_renderer_buffer(renderer, renderer_buffer))
                    ERR("failed to _e_plane_renderer_release_exported_renderer_buffer");
               }
          }
     }

   if (!plane->is_fb && !renderer->exported_wl_buffer_count && !renderer->ec)
     e_plane_renderer_unset(plane);
}

static Eina_Bool
_e_plane_renderer_client_ec_buffer_change_cb(void *data, int type, void *event)
{
   E_Client *ec = NULL;
   E_Event_Client *ev = event;
   E_Plane_Renderer *renderer = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;

   if (e_object_is_del(E_OBJECT(ec))) return ECORE_CALLBACK_PASS_ON;

   renderer_client = e_plane_renderer_client_get(ec);
   if (!renderer_client) return ECORE_CALLBACK_PASS_ON;

   renderer = renderer_client->renderer;

   if (_e_plane_renderer_client_surface_flags_get(renderer_client) != E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED)
     {
        if (!renderer) return ECORE_CALLBACK_PASS_ON;
        if (renderer_client->state != E_PLANE_RENDERER_CLIENT_STATE_ACTIVATED) return ECORE_CALLBACK_PASS_ON;
        if (renderer->tqueue)
          {
             if (renderer->plane && (renderer->ec == ec))
               e_plane_ec_set(renderer->plane, NULL);
          }

        return ECORE_CALLBACK_PASS_ON;
     }

   renderer_buffer = _e_plane_renderer_client_renderer_buffer_get(renderer_client);
   if (!renderer_buffer)
     {
        WRN("fail to get renderer_bufrer");

        if (!_e_plane_renderer_client_backup_buffer_set(renderer_client))
          ERR("fail to _e_comp_hwc_set_backup_buffer");

        if (renderer_trace_debug)
          ELOGF("E_PLANE_RENDERER", "Set    backup buffer   wl_buffer(%p)::buffer_change_exception",
                ec, _get_wl_buffer(ec));

        return ECORE_CALLBACK_PASS_ON;
     }

   if ((renderer_client->state == E_PLANE_RENDERER_CLIENT_STATE_NONE) ||
       (renderer_client->state == E_PLANE_RENDERER_CLIENT_STATE_PENDING_DEACTIVATED))
     {
        if (!_e_plane_renderer_client_backup_buffer_set(renderer_client))
          ERR("fail to _e_comp_hwc_set_backup_buffer");

        if (renderer_trace_debug)
          ELOGF("E_PLANE_RENDERER", "Set    backup buffer   wl_buffer(%p)::buffer_change",
                ec, _get_wl_buffer(ec));

        if (!e_comp->hwc_use_detach &&
            renderer_client->state == E_PLANE_RENDERER_CLIENT_STATE_PENDING_DEACTIVATED)
          {
             if (renderer && !_e_plane_renderer_release_exported_renderer_buffer(renderer, renderer_buffer))
               ERR("fail to _e_plane_renderer_release_exported_renderer_buffer");
          }
     }
   else
     {
        if (!renderer)
          ERR("renderer_client doesn't have renderer");

        if (renderer && !_e_plane_renderer_receive_renderer_buffer(renderer, renderer_buffer))
          ERR("fail to _e_plane_renderer_receive_renderer_buffer");
     }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_plane_renderer_recover_ec(E_Plane_Renderer *renderer)
{
   E_Plane *plane = renderer->plane;
   E_Client *ec = renderer->ec;
   E_Comp_Wl_Client_Data *cdata = NULL;
   E_Comp_Wl_Buffer *buffer = NULL;
   tbm_surface_h tsurface =NULL;

   if (!plane->ec_redirected) return;

   if (!ec) return;

   cdata = ec->comp_data;
   if (!cdata) return;
   if (!cdata->mapped) return;

   buffer = cdata->buffer_ref.buffer;

   if (!buffer)
     {
        if (!plane) return;

        tsurface = plane->tsurface;
        if (!tsurface) return;

        buffer = e_comp_wl_tbm_buffer_get(tsurface);
        if (!buffer) return;
     }

   e_comp_wl_surface_attach(ec, buffer);

   /* force update */
   e_pixmap_image_refresh(ec->pixmap);
   e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
   e_comp_object_dirty(ec->frame);
   e_comp_object_render(ec->frame);

   return;
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

static void
_e_plane_renderer_render_flush_post_cb(void *data, Evas *e EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Plane_Renderer *renderer = (E_Plane_Renderer *)data;
   Eina_List *render_buffers = NULL;
   E_Comp_Wl_Buffer_Ref *buffer_ref = NULL;
   E_Comp_Wl_Buffer *buffer;
   Eina_List *l;
   E_Client *ec;

   if (!renderer->render_dequeued_tsurface) return;

   EINA_LIST_FOREACH(e_comp->render_list, l, ec)
     {
        buffer = e_pixmap_ref_resource_get(ec->pixmap);
        if (!buffer)
          buffer = _get_comp_wl_buffer(ec);

        if (!buffer) continue;

        /* if reference buffer created by server, server deadlock is occurred.
           beacause tbm_surface_internal_unref is called in user_data delete callback.
           tbm_surface doesn't allow it.
         */
        if (!buffer->resource) continue;

        buffer_ref = E_NEW(E_Comp_Wl_Buffer_Ref, 1);
        if (!buffer_ref) continue;

        e_comp_wl_buffer_reference(buffer_ref, buffer);
        render_buffers = eina_list_append(render_buffers, buffer_ref);
     }

   tbm_surface_internal_set_user_data(renderer->render_dequeued_tsurface,
                                      RENDER_BUFFERS_KEY,
                                      render_buffers);

   renderer->render_dequeued_tsurface = NULL;
}

static Eina_Bool
_e_plane_renderer_cb_queue_acquirable_event(void *data, Ecore_Fd_Handler *fd_handler)
{
   int len;
   int fd;
   char buffer[64];
   E_Plane_Renderer *renderer = (E_Plane_Renderer *)data;
   tbm_surface_h *tsurfaces = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   int num = 0, i = 0;

   fd = ecore_main_fd_handler_fd_get(fd_handler);
   if (fd < 0) return ECORE_CALLBACK_RENEW;

   len = read(fd, buffer, sizeof(buffer));
   if (len == -1)
     ERR("failed to read queue acquire event fd:%m");

   tsurfaces = E_NEW(tbm_surface_h, renderer->tqueue_size);
   if (!tsurfaces)
     {
        ERR("failed to alloc tsurfaces");
        return ECORE_CALLBACK_RENEW;
     }

   tsq_err = tbm_surface_queue_get_acquirable_surfaces(renderer->tqueue, tsurfaces, &num);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("failed to tbm_surface_queue_get_acquirable_surfaces");
        E_FREE(tsurfaces);
        return ECORE_CALLBACK_RENEW;
     }

   for (i = 0; i < num; i++)
     {
        tbm_surface_h tsurface = tsurfaces[i];
        if (!tsurface) continue;

        tbm_surface_internal_set_user_data(tsurface, RENDER_BUFFERS_KEY, NULL);
     }

   E_FREE(tsurfaces);

   return ECORE_CALLBACK_RENEW;
}

EINTERN E_Plane_Renderer *
e_plane_renderer_new(E_Plane *plane)
{
   E_Plane_Renderer *renderer = NULL;
   tbm_surface_queue_h tqueue = NULL;
   int ee_width = 0;
   int ee_height = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   /* create a renderer */
   renderer = E_NEW(E_Plane_Renderer, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);
   renderer->plane = plane;
   renderer->event_fd = -1;

   if (plane->is_external) return renderer;

   if (plane->is_fb)
     {
        renderer->ee = e_comp->ee;
        renderer->evas = ecore_evas_get(renderer->ee);
        renderer->event_fd = eventfd(0, EFD_NONBLOCK);
        renderer->event_hdlr = ecore_main_fd_handler_add(renderer->event_fd, ECORE_FD_READ,
                               _e_plane_renderer_cb_queue_acquirable_event, renderer, NULL, NULL);

        evas_event_callback_add(renderer->evas,
                                EVAS_CALLBACK_RENDER_FLUSH_POST,
                                _e_plane_renderer_render_flush_post_cb,
                                renderer);

        TRACE_DS_BEGIN(Plane Render:Manual Render);
        ecore_evas_geometry_get(renderer->ee, NULL, NULL, &ee_width, &ee_height);
        if (renderer->tqueue_width != ee_width || renderer->tqueue_height != ee_height)
          ecore_evas_manual_render(renderer->ee);
        TRACE_DS_END();

        tqueue = _get_tbm_surface_queue(renderer->ee);
        TRACE_DS_BEGIN(Plane Render:Set Queue);
        if (tqueue && !e_plane_renderer_surface_queue_set(renderer, tqueue))
          ERR("fail to e_plane_renderer_queue_set");
        TRACE_DS_END();
     }

   renderer->need_change_buffer_transform = EINA_TRUE;

   return renderer;
}

static void
_e_plane_renderer_cursor_image_draw(E_Comp_Wl_Buffer *buffer, tbm_surface_info_s *tsurface_info, int rotation)
{
   int src_width, src_height, src_stride;
   pixman_image_t *src_img = NULL, *dst_img = NULL;
   pixman_transform_t t;
   struct pixman_f_transform ft;
   void *src_ptr = NULL, *dst_ptr = NULL;
   int c = 0, s = 0, tx = 0, ty = 0;
   int i, rotate;

   src_width = wl_shm_buffer_get_width(buffer->shm_buffer);
   src_height = wl_shm_buffer_get_height(buffer->shm_buffer);
   src_stride = wl_shm_buffer_get_stride(buffer->shm_buffer);
   src_ptr = wl_shm_buffer_get_data(buffer->shm_buffer);

   dst_ptr = tsurface_info->planes[0].ptr;
   memset(dst_ptr, 0, tsurface_info->planes[0].stride * tsurface_info->height);

   if (rotation)
     {
        src_img = pixman_image_create_bits(PIXMAN_a8r8g8b8, src_width, src_height, (uint32_t*)src_ptr, src_stride);
        EINA_SAFETY_ON_NULL_GOTO(src_img, error);

        dst_img = pixman_image_create_bits(PIXMAN_a8r8g8b8, tsurface_info->width, tsurface_info->height,
                                          (uint32_t*)dst_ptr, tsurface_info->planes[0].stride);
        EINA_SAFETY_ON_NULL_GOTO(dst_img, error);

        if (rotation == 90)
           rotation = 270;
        else if (rotation == 270)
           rotation = 90;

        rotate = (rotation + 360) / 90 % 4;
        switch (rotate)
          {
           case 1:
              c = 0, s = -1, tx = -tsurface_info->width;
              break;
           case 2:
              c = -1, s = 0, tx = -tsurface_info->width, ty = -tsurface_info->height;
              break;
           case 3:
              c = 0, s = 1, ty = -tsurface_info->width;
              break;
          }

        pixman_f_transform_init_identity(&ft);
        pixman_f_transform_translate(&ft, NULL, tx, ty);
        pixman_f_transform_rotate(&ft, NULL, c, s);

        pixman_transform_from_pixman_f_transform(&t, &ft);
        pixman_image_set_transform(src_img, &t);

        pixman_image_composite(PIXMAN_OP_SRC, src_img, NULL, dst_img, 0, 0, 0, 0, 0, 0,
                               tsurface_info->width, tsurface_info->height);
     }
   else
     {
        for (i = 0 ; i < src_height ; i++)
          {
             memcpy(dst_ptr, src_ptr, src_stride);
             dst_ptr += tsurface_info->planes[0].stride;
             src_ptr += src_stride;
          }
     }

error:
   if (src_img) pixman_image_unref(src_img);
   if (dst_img) pixman_image_unref(dst_img);
}

EINTERN Eina_Bool
e_plane_renderer_cursor_surface_refresh(E_Plane_Renderer *renderer, E_Client *ec)
{
   E_Plane *plane = NULL;
   E_Output *output = NULL;
   int w, h, tw, th;
   int tsurface_w, tsurface_h;
   void *src_ptr = NULL;
   tbm_surface_h tsurface = NULL;
   E_Comp_Wl_Buffer *buffer = NULL;
   tbm_surface_error_e ret = TBM_SURFACE_ERROR_NONE;
   tbm_surface_info_s tsurface_info;
   E_Pointer *pointer = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   output = plane->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   buffer = ec->comp_data->buffer_ref.buffer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, EINA_FALSE);

   pointer = e_pointer_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pointer, EINA_FALSE);

   if ((plane->display_info.buffer_ref.buffer == buffer) &&
       (renderer->cursor_tsurface) &&
       (renderer->cursor_rotation == pointer->rotation))
     return EINA_TRUE;

   /* TODO: TBM TYPE, NATIVE_WL */
   if (buffer->type == E_COMP_WL_BUFFER_TYPE_SHM)
     {
        src_ptr = wl_shm_buffer_get_data(buffer->shm_buffer);
        if (!src_ptr)
          {
             ERR("Failed get data shm buffer");
             return EINA_FALSE;
          }
     }
   else
     {
        ERR("unkown buffer type:%d", ec->comp_data->buffer_ref.buffer->type);
        return EINA_FALSE;
     }

   if (pointer->rotation == 90)
      tw = ec->h, th = ec->w;
   else if (pointer->rotation == 180)
      tw = ec->w, th = ec->h;
   else if (pointer->rotation == 270)
      tw = ec->h, th = ec->w;
   else
      tw = ec->w, th = ec->h;

   renderer->cursor_rotation = pointer->rotation;

   w = (output->cursor_available.min_w > tw) ? output->cursor_available.min_w : tw;
   h = (output->cursor_available.min_h > th) ? output->cursor_available.min_h : th;

   if (e_comp->hwc_reuse_cursor_buffer)
     {
        if (renderer->cursor_tsurface)
          {
             tsurface_w = tbm_surface_get_width(renderer->cursor_tsurface);
             tsurface_h = tbm_surface_get_height(renderer->cursor_tsurface);

             if (w != tsurface_w || h != tsurface_h)
               {
                  tbm_surface_destroy(renderer->cursor_tsurface);
                  renderer->cursor_tsurface = NULL;
               }
          }
     }
   else
     {
        if (renderer->cursor_tsurface)
          {
             tbm_surface_destroy(renderer->cursor_tsurface);
             renderer->cursor_tsurface = NULL;
          }
     }

   if (!renderer->cursor_tsurface)
     {
        tsurface = tbm_surface_internal_create_with_flags(w, h, TBM_FORMAT_ARGB8888, plane->buffer_flags);
        if (!tsurface) return EINA_FALSE;
     }
   else
     {
        tsurface = renderer->cursor_tsurface;
     }

   ret = tbm_surface_map(tsurface, TBM_SURF_OPTION_WRITE, &tsurface_info);
   if (ret != TBM_SURFACE_ERROR_NONE)
     {
        ERR("Failed to map tsurface");
        tbm_surface_destroy(tsurface);
        return EINA_FALSE;
     }

   _e_plane_renderer_cursor_image_draw(buffer, &tsurface_info, pointer->rotation);

   tbm_surface_unmap(tsurface);

   renderer->cursor_tsurface = tsurface;

   return EINA_TRUE;
}

EINTERN tbm_surface_h
e_plane_renderer_cursor_surface_get(E_Plane_Renderer *renderer)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);

   return renderer->cursor_tsurface;
}

EINTERN Eina_Bool
e_plane_renderer_cursor_ec_set(E_Plane_Renderer *renderer, E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   E_Plane_Renderer_Client *renderer_client = NULL;
   E_Pointer *pointer = NULL;

   if (renderer->ec && renderer->ec != ec)
     {
        pointer = e_pointer_get(renderer->ec);
        if (pointer)
          e_pointer_hwc_set(pointer, EINA_FALSE);

        renderer_client = e_plane_renderer_client_get(renderer->ec);
        if (renderer_client)
          renderer_client->renderer = NULL;

        pointer = NULL;
        renderer_client = NULL;
     }

   pointer = e_pointer_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pointer, EINA_FALSE);

   renderer_client = e_plane_renderer_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, EINA_FALSE);

   renderer->ec = ec;
   renderer_client->renderer = renderer;

   e_pointer_hwc_set(pointer, EINA_TRUE);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_renderer_ec_set(E_Plane_Renderer *renderer, E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   E_Plane *plane = NULL;
   tbm_surface_queue_h tqueue = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;
   int w, h;
   int output_rotation;

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (e_object_is_del(E_OBJECT(ec)))
     {
        INF("Ignore deleted ec:%p", ec);
        return EINA_FALSE;
     }

   renderer_client = e_plane_renderer_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, EINA_FALSE);

   if (plane->reserved_memory)
     {
        if (!renderer->ee)
          {
             output_rotation = plane->output->config.rotation;
             if (output_rotation % 180)
               {
                  w = ec->h;
                  h = ec->w;
               }
             else
               {
                  w = ec->w;
                  h = ec->h;
               }

             if (!renderer->tqueue)
               {
                  tqueue = e_plane_renderer_surface_queue_create(renderer, w, h, plane->buffer_flags);
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
                  if (renderer->tqueue_width != w || renderer->tqueue_height != h)
                    {
                       if ((w * h) == (renderer->tqueue_width * renderer->tqueue_height))
                         tqueue = e_plane_renderer_surface_queue_recreate(renderer, w, h, plane->buffer_flags);
                       else
                         tqueue = e_plane_renderer_surface_queue_create(renderer, w, h, plane->buffer_flags);

                       EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

                       /* recreate tqueue */
                       e_plane_renderer_clean(plane);
                       e_plane_renderer_surface_queue_destroy(renderer);

                       if (!e_plane_renderer_surface_queue_set(renderer, tqueue))
                         {
                            ERR("Failed to set surface queue at renderer:%p", renderer);
                            tbm_surface_queue_destroy(tqueue);
                            return EINA_FALSE;
                         }
                    }
               }
          }

        if (renderer_client->renderer && renderer_client->renderer != renderer)
          e_plane_renderer_reserved_deactivate(renderer_client->renderer);

        if (!e_plane_renderer_reserved_activate(renderer, ec))
          return EINA_FALSE;
     }
   else
     {
        if (!e_plane_renderer_activate(renderer, ec))
          return EINA_FALSE;
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
        if (!e_plane_renderer_reserved_deactivate(renderer))
          {
             ERR("fail to e_plane_renderer_reserved_deactivate.");
             return EINA_FALSE;
          }
     }
   else
     {
        if (!e_plane_renderer_deactivate(renderer))
          {
             ERR("fail to e_plane_renderer_deactivate.");
             return EINA_FALSE;
          }
     }

   return EINA_TRUE;
}

static void
_e_plane_renderer_queue_trace_cb(tbm_surface_queue_h surface_queue,
        tbm_surface_h tsurface, tbm_surface_queue_trace trace, void *data)
{
   E_Plane_Renderer *renderer = (E_Plane_Renderer *)data;

   if (!renderer) return;

   if (trace != TBM_SURFACE_QUEUE_TRACE_DEQUEUE) return;

   if (renderer->send_dequeue) return;
   if (renderer->state == E_PLANE_RENDERER_STATE_ACTIVATE) return;

   renderer->render_dequeued_tsurface = tsurface;
}

EINTERN void
e_plane_renderer_del(E_Plane_Renderer *renderer)
{
   E_Plane *plane = NULL;
   E_Plane_Role role;
   E_Client *ec = NULL;
   E_Pointer *pointer = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;

   EINA_SAFETY_ON_NULL_RETURN(renderer);

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN(plane);

   ec = renderer->ec;

   if (renderer->ee)
     {
        WRN("Delete renderer for canvas");

        if (renderer->event_hdlr)
          {
             ecore_main_fd_handler_del(renderer->event_hdlr);
             renderer->event_hdlr = NULL;
          }

        if (renderer->tqueue)
          tbm_surface_queue_remove_trace_cb(renderer->tqueue,
                                            _e_plane_renderer_queue_trace_cb,
                                            renderer);

        evas_event_callback_del(renderer->evas,
                                EVAS_CALLBACK_RENDER_FLUSH_POST,
                                _e_plane_renderer_render_flush_post_cb);

        if (renderer->event_fd >= 0)
          {
             close(renderer->event_fd);
             renderer->event_fd = -1;
          }
     }

   if (renderer->tqueue)
     tbm_surface_queue_remove_destroy_cb(renderer->tqueue, _e_plane_renderer_cb_surface_queue_destroy, (void *)renderer);

   role = e_plane_role_get(plane);
   if (role == E_PLANE_ROLE_CURSOR)
     {
        if (ec)
          {
             pointer = e_pointer_get(ec);

             if (pointer)
               e_pointer_hwc_set(pointer, EINA_FALSE);
          }

        _e_plane_renderer_recover_ec(renderer);
        tbm_surface_destroy(renderer->cursor_tsurface);
     }
   else
     {
        if (plane->reserved_memory)
          {
             e_plane_renderer_reserved_deactivate(renderer);
             e_plane_renderer_surface_queue_destroy(renderer);
          }
        else
          e_plane_renderer_deactivate(renderer);
     }

   if (ec)
     {
        renderer_client = e_plane_renderer_client_get(ec);
        if (renderer_client)
          renderer_client->renderer = NULL;
     }

   free(renderer);
   plane->renderer = NULL;
}

EINTERN Eina_Bool
e_plane_renderer_ec_valid_check(E_Plane_Renderer *renderer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   cqueue = _e_plane_renderer_wayland_tbm_client_queue_get(ec);
   if (!cqueue) return EINA_FALSE;

   return EINA_TRUE;
}


static void
_e_plane_renderer_pending_deactivate_check(E_Plane_Renderer *renderer)
{
   E_Client *ec = NULL;
   struct wayland_tbm_client_queue *cqueue = NULL;

   if (!e_comp->hwc_sync_mode_change) return;
   if (e_comp->hwc_use_detach) return;
   if (renderer->state != E_PLANE_RENDERER_STATE_PENDING_DEACTIVATE) return;

   if (renderer->pending_deactivate)
     {
        renderer->pending_deactivate = EINA_FALSE;
        return;
     }

   ec = renderer->ec;

   if (ec)
     cqueue = _e_plane_renderer_wayland_tbm_client_queue_get(ec);

   if (cqueue)
     {
        _e_plane_renderer_exported_surfaces_release(renderer, EINA_TRUE);
        wayland_tbm_server_client_queue_set_dequeue_cb(cqueue, NULL, NULL);
     }
   else
     {
        _e_plane_renderer_exported_surfaces_release(renderer, EINA_FALSE);
     }
}

EINTERN Eina_Bool
e_plane_renderer_render(E_Plane_Renderer *renderer, Eina_Bool is_fb)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   _e_plane_renderer_pending_deactivate_check(renderer);

   if (is_fb)
     {
        if (e_comp_canvas_norender_get() > 0)
          {
             if (renderer_trace_debug)
               ELOGF("E_PLANE_RENDERER", "Canvas norender is set. No Display.", NULL);

             return EINA_TRUE;
          }

        /* render the ecore_evas and
           update_ee is to be true at post_render_cb when the render is successful. */
        TRACE_DS_BEGIN(MANUAL RENDER);

        if (e_plane_renderer_surface_queue_can_dequeue(renderer) || !renderer->tqueue)
          {
             ecore_evas_manual_render(renderer->ee);
             renderer->rendered = EINA_TRUE;
          }
        else
          renderer->rendered = EINA_FALSE;

        TRACE_DS_END();
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_renderer_norender(E_Plane_Renderer *renderer, Eina_Bool is_fb)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   if (is_fb)
     evas_norender(renderer->evas);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_renderer_activate(E_Plane_Renderer *renderer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;
   E_Plane *plane = NULL;
   int transform;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   cqueue = _e_plane_renderer_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   /* register the plane client */
   renderer_client = e_plane_renderer_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, EINA_FALSE);

   if ((renderer->state == E_PLANE_RENDERER_STATE_ACTIVATE) && (renderer->ec != ec))
     e_plane_renderer_deactivate(renderer);

   if (renderer->ec != ec)
     renderer->need_change_buffer_transform = EINA_TRUE;

   transform = e_comp_wl_output_buffer_transform_get(ec);

   if (plane->output->config.rotation)
     {
        if (!e_config->screen_rotation_client_ignore && renderer->need_change_buffer_transform)
          {
             e_comp_screen_rotation_ignore_output_transform_send(ec, EINA_FALSE);
             renderer->need_change_buffer_transform = EINA_FALSE;
             INF("ec:%p tansform:%d screen_roatation:%d", ec, transform, plane->output->config.rotation);
          }
     }

   if ((plane->output->config.rotation / 90) != transform)
     return EINA_FALSE;

   wayland_tbm_server_client_queue_activate(cqueue, 0, 0, 0);

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Activate Renderer(%p)", ec, renderer);

   renderer->need_change_buffer_transform = EINA_TRUE;

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
   E_Plane *plane = NULL;
   int transform;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   ec = renderer->ec;
   if (!ec) return EINA_TRUE;

   renderer_client = e_plane_renderer_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, EINA_FALSE);

   cqueue = _e_plane_renderer_wayland_tbm_client_queue_get(ec);

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Deactivate Renderer(%p)", ec, renderer);

   if (cqueue)
     wayland_tbm_server_client_queue_deactivate(cqueue);

   transform = e_comp_wl_output_buffer_transform_get(ec);
   if (plane->output->config.rotation != 0 && (plane->output->config.rotation / 90) == transform)
     e_comp_screen_rotation_ignore_output_transform_send(ec, EINA_TRUE);

   _e_plane_renderer_recover_ec(renderer);

   renderer->state = E_PLANE_RENDERER_STATE_NONE;
   renderer->ec = NULL;

   renderer_client->state = E_PLANE_RENDERER_CLIENT_STATE_NONE;
   renderer_client->renderer = NULL;

   return EINA_TRUE;
}

static Eina_Bool
_e_plane_renderer_all_disp_surfaces_send(E_Plane_Renderer *renderer, E_Client* ec)
{
   tbm_surface_h tsurface = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(renderer->disp_surfaces, l, tsurface)
     {
        if (!tsurface) continue;
        e_plane_renderer_surface_send(renderer, ec, tsurface);
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_renderer_reserved_activate(E_Plane_Renderer *renderer, E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   tbm_surface_h tsurface = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   E_Plane *plane = NULL;
   int transform;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   cqueue = _e_plane_renderer_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   /* register the plane client */
   renderer_client = e_plane_renderer_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, EINA_FALSE);

   if ((renderer->state == E_PLANE_RENDERER_STATE_ACTIVATE) && (renderer->ec != ec))
     e_plane_renderer_reserved_deactivate(renderer);

   if (_e_plane_renderer_client_surface_flags_get(renderer_client) != E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED)
     {
        if (renderer->state == E_PLANE_RENDERER_STATE_NONE)
          {
             if (eina_list_count(renderer->exported_surfaces))
               {
                  INF("Renderer has exported surfaces.");
                  return EINA_FALSE;
               }

             /* check dequeuable */
             if (!e_plane_renderer_surface_queue_can_dequeue(renderer))
               {
                  INF("There is no dequeuable surface.");
                  return EINA_FALSE;
               }
          }
        else if ((renderer->state == E_PLANE_RENDERER_STATE_CANDIDATE) && (renderer->ec != ec))
          {
             /* deactive the candidate_ec */
             e_plane_renderer_reserved_deactivate(renderer);

             if (eina_list_count(renderer->exported_surfaces))
               {
                  INF("Renderer has exported surfaces.");
                  return EINA_FALSE;
               }

             /* check dequeuable */
             if (!e_plane_renderer_surface_queue_can_dequeue(renderer))
               {
                  INF("There is no dequeuable surface.");
                  return EINA_FALSE;
               }
           }
        else if ((renderer->state == E_PLANE_RENDERER_STATE_CANDIDATE) && (renderer->ec == ec))
           {
              return EINA_FALSE;
           }
        else if (renderer->state == E_PLANE_RENDERER_STATE_PENDING_DEACTIVATE)
           {
              INF("renderer state is pending deactivate.");
              return EINA_FALSE;
           }
        else
          {
             ERR("NEVER HERE.");
             return EINA_FALSE;
          }

        if (!_e_plane_renderer_all_disp_surfaces_send(renderer, ec))
          {
             ERR("Failed to _e_plane_renderer_all_disp_surfaces_send ec:%p", ec);
             return EINA_FALSE;
          }

        renderer->send_dequeue = EINA_TRUE;

        /* dequeue */
        tsurface = e_plane_renderer_surface_queue_dequeue(renderer);
        if (!tsurface)
          {
             renderer->send_dequeue = EINA_FALSE;
             ERR("fail to dequeue surface");
             return EINA_FALSE;
          }

        renderer->send_dequeue = EINA_FALSE;

        /* export */
        e_plane_renderer_surface_usable_send(renderer, ec, tsurface);

        if (renderer->ec != ec)
          renderer->need_change_buffer_transform = EINA_TRUE;

        if (plane->output->config.rotation)
          {
             if (!e_config->screen_rotation_client_ignore && renderer->need_change_buffer_transform)
               {
                  e_comp_screen_rotation_ignore_output_transform_send(ec, EINA_FALSE);
                  renderer->need_change_buffer_transform = EINA_FALSE;
               }
          }

        wayland_tbm_server_client_queue_activate(cqueue, 0, renderer->tqueue_size, 1);

        if (e_comp->hwc_sync_mode_change && !e_comp->hwc_use_detach)
          wayland_tbm_server_client_queue_set_dequeue_cb(cqueue, _e_plane_renderer_exported_surface_dequeue_cb, plane);

        tsq_err = tbm_surface_queue_notify_reset(renderer->tqueue);
        if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
          ERR("fail to tbm_surface_queue_notify_reset");

        ELOGF("E_PLANE_RENDERER", "Candidate Renderer(%p) Plane(%p) ec(%p, %s)",
              ec, renderer, renderer->plane,  ec, e_client_util_name_get(ec));

        renderer->state = E_PLANE_RENDERER_STATE_CANDIDATE;
        renderer->ec = ec;

        renderer_client->state = E_PLANE_RENDERER_CLIENT_STATE_CANDIDATED;
        renderer_client->renderer = renderer;

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

   transform = e_comp_wl_output_buffer_transform_get(ec);
   if ((plane->output->config.rotation / 90) != transform)
     {
        INF("ec:%p tansform:%d screen_roatation:%d", ec, transform, plane->output->config.rotation);
        return EINA_FALSE;
     }

   ELOGF("E_PLANE_RENDERER", "Activate Renderer(%p) Plane(%p) ec(%p, %s)",
         ec, renderer, renderer->plane, ec, e_client_util_name_get(ec));

   renderer->need_change_buffer_transform = EINA_TRUE;

   renderer->ec = ec;
   renderer->state = E_PLANE_RENDERER_STATE_ACTIVATE;

   renderer_client->renderer = renderer;
   renderer_client->state = E_PLANE_RENDERER_CLIENT_STATE_ACTIVATED;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_renderer_reserved_deactivate(E_Plane_Renderer *renderer)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   E_Client *ec = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   E_Plane *plane = NULL;
   Eina_Bool usable = EINA_FALSE;
   int transform;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   ec = renderer->ec;
   if (!ec) return EINA_TRUE;

   renderer_client = e_plane_renderer_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, EINA_FALSE);

   if (e_object_is_del(E_OBJECT(ec))) goto done;

   if (renderer->state == E_PLANE_RENDERER_STATE_PENDING_DEACTIVATE) return EINA_TRUE;

   cqueue = _e_plane_renderer_wayland_tbm_client_queue_get(ec);
   if (cqueue)
      wayland_tbm_server_client_queue_deactivate(cqueue);

   transform = e_comp_wl_output_buffer_transform_get(ec);
   if (plane->output->config.rotation != 0 && (plane->output->config.rotation / 90) == transform)
     e_comp_screen_rotation_ignore_output_transform_send(ec, EINA_TRUE);

   if ((_get_comp_wl_buffer(ec)) &&
       (_e_plane_renderer_client_surface_flags_get(renderer_client) != E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED))
     goto done;

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Set    backup buffer   wl_buffer(%p)::Deactivate",
           ec, _get_wl_buffer(ec));

   usable = e_pixmap_usable_get(ec->pixmap);

   if (!_e_plane_renderer_client_backup_buffer_set(renderer_client))
     ERR("fail to _e_comp_hwc_set_backup_buffer");

   if (!usable)
     e_pixmap_usable_set(ec->pixmap, EINA_FALSE);

   if (renderer->state == E_PLANE_RENDERER_STATE_CANDIDATE)
     {
        if (!ec->redirected)
          goto done;
     }
   else
     {
        if (!plane->ec_redirected)
          goto done;
     }

   /* force update */
   e_pixmap_image_refresh(ec->pixmap);
   e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
   e_comp_object_dirty(ec->frame);
   e_comp_object_render(ec->frame);

done:
   ELOGF("E_PLANE_RENDERER", "Deactivate Renderer(%p) Plane(%p) ec(%p, %s)",
         ec, renderer, renderer->plane, ec, e_client_util_name_get(ec));

   if (e_comp->hwc_sync_mode_change)
     {
        if (cqueue)
          {
             renderer->state = E_PLANE_RENDERER_STATE_PENDING_DEACTIVATE;
             renderer->pending_deactivate = EINA_TRUE;

             renderer_client->state = E_PLANE_RENDERER_CLIENT_STATE_PENDING_DEACTIVATED;

             if (e_comp->hwc_use_detach)
               _e_plane_renderer_exported_surfaces_timer_set(renderer);
          }
        else
          {
             _e_plane_renderer_exported_surfaces_release(renderer, EINA_FALSE);
          }

        renderer_client->exported_surfaces = eina_list_free(renderer_client->exported_surfaces);
     }
   else
     {
        _e_plane_renderer_client_exported_surfaces_release(renderer_client, renderer);

        renderer->state = E_PLANE_RENDERER_STATE_NONE;
        renderer->ec = NULL;

        renderer->mode_change_age = 0;
        renderer_client->state = E_PLANE_RENDERER_CLIENT_STATE_NONE;
        renderer_client->renderer = NULL;
     }

   tsq_err = tbm_surface_queue_notify_reset(renderer->tqueue);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     ERR("fail to tbm_surface_queue_notify_reset");

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
   uint32_t flags = 0;
   E_Comp_Wl_Buffer *buffer = NULL;
   E_Plane_Renderer *renderer = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client->ec, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client->renderer, NULL);

   ec = renderer_client->ec;
   renderer = renderer_client->renderer;

   buffer = _get_comp_wl_buffer(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, NULL);

   switch (buffer->type)
     {
       case E_COMP_WL_BUFFER_TYPE_NATIVE:
         tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
         break;
       case E_COMP_WL_BUFFER_TYPE_TBM:
         tsurface = buffer->tbm_surface;
         break;
       default:
         ERR("not supported buffer type:%d", buffer->type);
         break;
     }

   if (!tsurface)
     {
        ERR("fail to get tsurface buffer type:%d", buffer->type);
        return NULL;
     }

   if (buffer->resource)
     flags = wayland_tbm_server_get_buffer_flags(wl_comp_data->tbm.server, buffer->resource);

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Receive Renderer(%p)        tsurface(%p) tqueue(%p) wl_buffer(%p) flags(%d)",
           ec, renderer, tsurface, renderer->tqueue, _get_wl_buffer(ec), flags);

   if (flags != E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED)
     {
        ERR("the flags of the enqueuing surface is %d. need flags(%d).", flags, E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED);
        return NULL;
     }

   return tsurface;
}

EINTERN E_Plane_Renderer *
e_plane_renderer_client_renderer_get(E_Plane_Renderer_Client *renderer_client)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, NULL);

   return renderer_client->renderer;
}

static void
_e_plane_renderer_buffer_alloced_reset(E_Plane_Renderer *renderer)
{
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(renderer->renderer_buffers, l, renderer_buffer)
     renderer_buffer->alloced = EINA_FALSE;
}
static void
_e_plane_renderer_surface_queue_recreate_free_cb(tbm_surface_queue_h surface_queue,
                                                 void *data,
                                                 tbm_surface_h tsurface)
{
   tbm_surface_internal_unref(tsurface);
}

static tbm_surface_h
_e_plane_renderer_surface_queue_recreate_alloc_cb(tbm_surface_queue_h surface_queue,
                                                  void *data)
{
   E_Plane_Renderer *renderer = (E_Plane_Renderer *)data;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_bo bo = NULL;
   Eina_List *l;
   int width = 0, height = 0, format = 0, flags = 0;
   tbm_surface_info_s info;
   tbm_surface_info_s new_info;

   width = renderer->recreate_info.width;
   height = renderer->recreate_info.height;
   format = TBM_FORMAT_ARGB8888;

   if (renderer->plane)
     flags = renderer->plane->buffer_flags;

   EINA_LIST_FOREACH(renderer->renderer_buffers, l, renderer_buffer)
     {
        if (!renderer_buffer->tsurface) continue;
        if (renderer_buffer->alloced) continue;

        bo = tbm_surface_internal_get_bo(renderer_buffer->tsurface, 0);
        if (!bo)
          {
             ERR("fail to get bo");
             continue;
          }

        tbm_surface_get_info(renderer_buffer->tsurface, &info);

        memcpy(&new_info, &info, sizeof(tbm_surface_info_s));
        new_info.width = width;
        new_info.height = height;
        new_info.format = format;
        new_info.planes[0].stride = new_info.size / height;

        tsurface = tbm_surface_internal_create_with_bos(&new_info, &bo, 1);
        if (!tsurface)
          {
             ERR("fail to alloc tsurface with bo");
             continue;
          }

        if (renderer_trace_debug)
          ELOGF("E_PLANE_RENDERER", "Renderer(%p) Reset tsurface:%p new_tsurface:%p tqueue:%p",
                NULL, renderer, renderer_buffer->tsurface, tsurface, surface_queue);

        renderer_buffer->alloced = EINA_TRUE;

        return tsurface;
     }

   ERR("Renderer(%p) tqueue:%p reset alloc fail", renderer, surface_queue);
   tsurface = tbm_surface_internal_create_with_flags(width, height, format, flags);

   return tsurface;
}

EINTERN tbm_surface_queue_h
e_plane_renderer_surface_queue_recreate(E_Plane_Renderer *renderer, int width, int height, unsigned int buffer_flags)
{
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_queue_h renderer_tqueue = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   int queue_size = 0;
   int buffer_count = 0;
   int format = TBM_FORMAT_ARGB8888;
   Eina_List *dequeued_tsurface = NULL, *l = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);

   renderer_tqueue = renderer->tqueue;
   EINA_SAFETY_ON_FALSE_RETURN_VAL(renderer->tqueue, NULL);

   queue_size = tbm_surface_queue_get_size(renderer_tqueue);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(queue_size > 1, NULL);

   buffer_count = eina_list_count(renderer->renderer_buffers);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(buffer_count > 1, NULL);

   EINA_SAFETY_ON_FALSE_RETURN_VAL(buffer_count == queue_size, NULL);

   tqueue = tbm_surface_queue_create(queue_size, width, height, format, buffer_flags);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tqueue, NULL);

   renderer->recreate_info.width = width;
   renderer->recreate_info.height = height;

   tbm_surface_queue_set_alloc_cb(tqueue, _e_plane_renderer_surface_queue_recreate_alloc_cb,
                                  _e_plane_renderer_surface_queue_recreate_free_cb, renderer);
   _e_plane_renderer_buffer_alloced_reset(renderer);

   while (tbm_surface_queue_can_dequeue(tqueue, 0))
     {
        /* dequeue */
        tsq_err = tbm_surface_queue_dequeue(tqueue, &tsurface);
        EINA_SAFETY_ON_FALSE_GOTO (tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE, error);

        dequeued_tsurface = eina_list_append(dequeued_tsurface, tsurface);
     }

   EINA_LIST_FOREACH(dequeued_tsurface, l, tsurface)
     {
        tsq_err = tbm_surface_queue_release(tqueue, tsurface);
        EINA_SAFETY_ON_FALSE_GOTO (tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE, error);
     }

   return tqueue;

error:
   _e_plane_renderer_buffer_alloced_reset(renderer);
   tbm_surface_queue_destroy(tqueue);

   return NULL;
}

EINTERN tbm_surface_queue_h
e_plane_renderer_surface_queue_create(E_Plane_Renderer *renderer, int width, int height, unsigned int buffer_flags)
{
   E_Plane *plane = NULL;
   tbm_surface_queue_h tqueue = NULL;
   int format = TBM_FORMAT_ARGB8888;
   tdm_error error;
   const tdm_prop *props;
   int i, count;
   int buffer_count = 3;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   error = tdm_layer_get_available_properties(plane->tlayer, &props, &count);
   if (error == TDM_ERROR_NONE)
     {
        for (i = 0; i < count; i++)
          {
             tdm_value value;

             if (strncmp(props[i].name, "reserved-buffer-count", TDM_NAME_LEN))
               continue;

             error = tdm_layer_get_property(plane->tlayer, props[i].id, &value);
             if (error == TDM_ERROR_NONE)
               buffer_count = value.u32;

             break;
          }
     }

   tqueue = tbm_surface_queue_create(buffer_count, width, height, format, buffer_flags);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tqueue, NULL);

   return tqueue;
}

EINTERN Eina_Bool
e_plane_renderer_surface_queue_set(E_Plane_Renderer *renderer, tbm_surface_queue_h tqueue)
{
   tbm_surface_h tsurface = NULL;
   tbm_surface_h *tsurfaces = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   Eina_List *dequeued_tsurface = NULL;
   int get_size = 0, i = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   renderer->tqueue = tqueue;
   renderer->tqueue_width = tbm_surface_queue_get_width(tqueue);
   renderer->tqueue_height = tbm_surface_queue_get_height(tqueue);
   renderer->tqueue_size = tbm_surface_queue_get_size(tqueue);

   EINA_LIST_FREE(renderer->disp_surfaces, tsurface)
     _e_plane_renderer_buffer_remove(renderer, tsurface);

   while (tbm_surface_queue_can_dequeue(tqueue, 0))
     {
        /* dequeue */
        tsq_err = tbm_surface_queue_dequeue(tqueue, &tsurface);
        EINA_SAFETY_ON_FALSE_GOTO(tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE, fail);

        dequeued_tsurface = eina_list_append(dequeued_tsurface, tsurface);
     }

   EINA_LIST_FREE(dequeued_tsurface, tsurface)
     tbm_surface_queue_release(tqueue, tsurface);

   tsurfaces = E_NEW(tbm_surface_h, renderer->tqueue_size);
   EINA_SAFETY_ON_NULL_GOTO(tsurfaces, fail);

   tsq_err = tbm_surface_queue_get_surfaces(tqueue, tsurfaces, &get_size);
   EINA_SAFETY_ON_FALSE_GOTO(tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE, fail);

   for (i = 0; i < get_size; i++)
     {
        /* if not exist, add the surface to the renderer */
        if (!_e_plane_renderer_surface_find_disp_surface(renderer, tsurfaces[i]))
          renderer->disp_surfaces = eina_list_append(renderer->disp_surfaces, tsurfaces[i]);

        if (!_e_plane_renderer_buffer_add(renderer, tsurfaces[i]))
          ERR("failed to _e_plane_renderer_buffer_add");
     }

   tsq_err = tbm_surface_queue_add_destroy_cb(tqueue, _e_plane_renderer_cb_surface_queue_destroy, (void *)renderer);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail to add destroy cb");
        goto fail_add_destroy_cb;
     }

   if (renderer->ee)
     {
        tsq_err = tbm_surface_queue_add_acquirable_cb(tqueue, _e_plane_renderer_cb_acquirable, (void *)(intptr_t)renderer->event_fd);
        if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
          {
             ERR("fail to add acquirable cb");
             goto fail;
          }

        tsq_err = tbm_surface_queue_add_trace_cb(tqueue, _e_plane_renderer_queue_trace_cb, renderer);
        if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
          {
             ERR("fail to add trace cb");
             goto fail;
          }
     }
   else
     {
        if (eina_list_count(renderer->disp_surfaces) < 2)
          {
             ERR("fail to get disp_surface count:%d", eina_list_count(renderer->disp_surfaces));
             goto fail;
          }
     }

   return EINA_TRUE;

fail:
   tbm_surface_queue_remove_destroy_cb(tqueue, _e_plane_renderer_cb_surface_queue_destroy, (void *)renderer);

fail_add_destroy_cb:
   EINA_LIST_FREE(renderer->disp_surfaces, tsurface)
     _e_plane_renderer_buffer_remove(renderer, tsurface);

   renderer->tqueue = NULL;
   renderer->tqueue_width = 0;
   renderer->tqueue_height = 0;
   renderer->tqueue_size = 0;

   return EINA_FALSE;
}

EINTERN void
e_plane_renderer_surface_queue_destroy(E_Plane_Renderer *renderer)
{
   tbm_surface_h tsurface;

   if (!renderer) return;

   if (renderer->tqueue)
     {
        tbm_surface_queue_destroy(renderer->tqueue);
        renderer->tqueue = NULL;
        renderer->tqueue_width = 0;
        renderer->tqueue_height = 0;
     }

   EINA_LIST_FREE(renderer->disp_surfaces, tsurface)
     _e_plane_renderer_buffer_remove(renderer, tsurface);
}

EINTERN Eina_Bool
e_plane_renderer_surface_queue_can_acquire(E_Plane_Renderer *renderer)
{
   tbm_surface_queue_h tqueue = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, 0);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, 0);

   if (!tbm_surface_queue_can_acquire(tqueue, 0))
     return EINA_FALSE;

   return EINA_TRUE;
}

EINTERN tbm_surface_h
e_plane_renderer_surface_queue_acquire(E_Plane_Renderer *renderer)
{
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;

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

   if (!_e_plane_renderer_buffer_add(renderer, tsurface))
     ERR("failed to _e_plane_renderer_buffer_add");

   renderer_buffer = _e_plane_renderer_buffer_get(renderer, tsurface);
   if (renderer_buffer)
     renderer_buffer->acquired = EINA_TRUE;

   if (renderer->ee)
     tbm_surface_internal_set_user_data(tsurface, RENDER_BUFFERS_KEY, NULL);

   /* debug */
   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Acquire Renderer(%p)        tsurface(%p) tqueue(%p)",
           NULL, renderer, tsurface, tqueue);

   return tsurface;
}

EINTERN void
e_plane_renderer_surface_queue_release(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   tbm_surface_queue_h tqueue = NULL;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;

   EINA_SAFETY_ON_NULL_RETURN(renderer);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN(tqueue);

   renderer_buffer = _e_plane_renderer_buffer_get(renderer, tsurface);
   EINA_SAFETY_ON_NULL_RETURN(renderer_buffer);

   renderer_buffer->acquired = EINA_FALSE;

   if (_e_plane_renderer_surface_find_released_surface(renderer, tsurface)) return;

   if (renderer_buffer->release_timer) return;

   /* debug */
   if (renderer_trace_debug)
     {
        E_Client *ec = renderer->ec;

        ELOGF("E_PLANE_RENDERER", "Release Renderer(%p)        tsurface(%p) tqueue(%p)",
              ec, renderer, tsurface, renderer->tqueue);
     }

   tsq_err = tbm_surface_queue_release(tqueue, tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("Failed to release tbm_surface(%p) from tbm_surface_queue(%p): tsq_err = %d", tsurface, tqueue, tsq_err);
        return;
     }

   renderer->released_surfaces = eina_list_append(renderer->released_surfaces, tsurface);

   if (renderer->ee)
     tbm_surface_internal_set_user_data(tsurface, RENDER_BUFFERS_KEY, NULL);
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
        ELOGF("E_PLANE_RENDERER", "Enqueue Renderer(%p)        tsurface(%p) tqueue(%p) wl_buffer(%p) ",
              ec, renderer, tsurface, renderer->tqueue, _get_wl_buffer(ec));
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
e_plane_renderer_surface_queue_cancel_acquire(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   tbm_surface_queue_h tqueue = NULL;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, EINA_FALSE);

   tqueue = renderer->tqueue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   renderer_buffer = _e_plane_renderer_buffer_get(renderer, tsurface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_buffer, EINA_FALSE);

   renderer_buffer->acquired = EINA_FALSE;

   /* debug */
   if (renderer_trace_debug)
    {
        E_Client *ec = renderer->ec;
        ELOGF("E_PLANE_RENDERER", "Cancel Acquire Renderer(%p)  tsurface(%p) tqueue(%p) wl_buffer(%p) ",
              ec, renderer, tsurface, renderer->tqueue, _get_wl_buffer(ec));
    }

   tsq_err = tbm_surface_queue_cancel_acquire(tqueue, tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("tbm_surface_queue_cancel_acquire failed. tbm_surface_queue(%p) tbm_surface(%p)", tqueue, tsurface);
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
   if (!tqueue)
     {
        WRN("tbm_surface_queue is NULL");
        return EINA_FALSE;
     }

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

   /* if not exist, add the surface to the renderer */
   if (!_e_plane_renderer_surface_find_disp_surface(renderer, tsurface))
     renderer->disp_surfaces = eina_list_append(renderer->disp_surfaces, tsurface);

   if (!_e_plane_renderer_buffer_add(renderer, tsurface))
     ERR("failed to _e_plane_renderer_buffer_add");

   /* debug */
   if (renderer_trace_debug)
     {
        E_Client *ec = renderer->ec;

        ELOGF("E_PLANE_RENDERER", "Dequeue Renderer(%p)        tsurface(%p) tqueue(%p)",
              ec, renderer, tsurface, renderer->tqueue);
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
e_plane_renderer_sent_surface_recevie(E_Plane_Renderer *renderer, tbm_surface_h tsurface)
{
   tbm_surface_h tmp_tsurface = NULL;
   Eina_List *l_s, *ll_s;

   EINA_SAFETY_ON_NULL_RETURN(renderer);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);

   if (renderer->state != E_PLANE_RENDERER_STATE_NONE) return;

   if (renderer->mode_change_age < 2)
     renderer->mode_change_age++;

   if (_e_plane_renderer_surface_find_exported_surface(renderer, tsurface))
     renderer->exported_surfaces = eina_list_remove(renderer->exported_surfaces, tsurface);

   if (renderer->mode_change_age < 2) return;

   EINA_LIST_FOREACH_SAFE(renderer->exported_surfaces, l_s, ll_s, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;

        renderer->exported_surfaces = eina_list_remove_list(renderer->exported_surfaces, l_s);

        if (tmp_tsurface == tsurface) continue;
        if (tmp_tsurface == renderer->displaying_tsurface) continue;

        e_plane_renderer_surface_queue_release(renderer, tmp_tsurface);
     }
}

EINTERN Eina_Bool
e_plane_renderer_surface_usable_send(E_Plane_Renderer *renderer, E_Client *ec, tbm_surface_h tsurface)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, EINA_FALSE);

   cqueue = _e_plane_renderer_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   renderer_buffer = _e_plane_renderer_buffer_get(renderer, tsurface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_buffer, EINA_FALSE);

   if (!renderer_buffer->exported || !renderer_buffer->exported_wl_buffer)
     {
        ERR("Not exported tsurface:%p", tsurface);
        return EINA_FALSE;
     }

   if (renderer_buffer->usable) return EINA_TRUE;

   wayland_tbm_server_client_queue_send_buffer_usable(cqueue, renderer_buffer->exported_wl_buffer);
   renderer_buffer->usable = EINA_TRUE;

   if (renderer_trace_debug)
   ELOGF("E_PLANE_RENDERER", "Usable  Renderer(%p)        tsurface(%p) tqueue(%p) wl_buffer(%p)",
         ec, renderer, tsurface, renderer->tqueue, renderer_buffer->exported_wl_buffer);

   return EINA_TRUE;
}

EINTERN void
e_plane_renderer_surface_send(E_Plane_Renderer *renderer, E_Client *ec, tbm_surface_h tsurface)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_buffer = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;
   E_Plane_Renderer_Buffer *renderer_buffer = NULL;
   E_Plane *plane = NULL;

   EINA_SAFETY_ON_NULL_RETURN(renderer);
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);

   plane = renderer->plane;
   EINA_SAFETY_ON_NULL_RETURN(plane);

   renderer_client = e_plane_renderer_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(renderer_client);

   cqueue = _e_plane_renderer_wayland_tbm_client_queue_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(cqueue);

   if (!_e_plane_renderer_surface_find_exported_surface(renderer, tsurface))
     {
        renderer_buffer = _e_plane_renderer_buffer_get(renderer, tsurface);
        EINA_SAFETY_ON_NULL_RETURN(renderer_buffer);

        /* export the tbm_surface(wl_buffer) to the client_queue */
        wl_buffer = wayland_tbm_server_client_queue_export_buffer2(cqueue, tsurface,
                E_PLANE_RENDERER_CLIENT_SURFACE_FLAGS_RESERVED,
                _e_plane_renderer_surface_exported_surface_detach_cb,
                _e_plane_renderer_surface_exported_surface_destroy_cb,
                (void *)plane);

        renderer->exported_wl_buffer_count++;
        renderer->exported_surfaces = eina_list_append(renderer->exported_surfaces, tsurface);

        if (!_e_plane_renderer_client_exported_surface_find(renderer_client, tsurface))
          renderer_client->exported_surfaces = eina_list_append(renderer_client->exported_surfaces, tsurface);

        renderer_buffer->exported = EINA_TRUE;
        renderer_buffer->exported_wl_buffer = wl_buffer;

        if (wl_buffer && renderer_trace_debug)
          ELOGF("E_PLANE_RENDERER", "Export  Renderer(%p)        tsurface(%p) tqueue(%p) wl_buffer(%p)",
                ec, renderer, tsurface, renderer->tqueue, wl_buffer);
     }
}

EINTERN void
e_plane_renderer_hwc_trace_debug(Eina_Bool onoff)
{
   if (onoff == renderer_trace_debug) return;
   renderer_trace_debug = onoff;
   INF("Renderer: hwc trace_debug is %s", onoff?"ON":"OFF");
}

EINTERN void
e_plane_renderer_show_state(E_Plane_Renderer *renderer)
{
   tbm_surface_h tmp_tsurface = NULL;
   Eina_List *l = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;

   EINA_SAFETY_ON_NULL_RETURN(renderer);

   if (renderer->ec)
     ELOGF("E_PLANE_RENDERER", "Renderer(%p) Plane(%p) ec(%p) state(%d) mode_chage_age(%d)",
           NULL, renderer, renderer->plane, renderer->ec, renderer->state, renderer->mode_change_age);
   else
     ELOGF("E_PLANE_RENDERER", "Renderer(%p) Plane(%p) ee_engine:%s state(%d) mode_chage_age(%d)",
           NULL, renderer, renderer->plane, ecore_evas_engine_name_get(renderer->ee), renderer->state, renderer->mode_change_age);

   EINA_LIST_FOREACH(renderer->disp_surfaces, l, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;

        ELOGF("E_PLANE_RENDERER", "Dispay Surfaces tsurface(%p)", NULL, tmp_tsurface);
     }

   ELOGF("E_PLANE_RENDERER", "Displaying tsurface(%p)", NULL, renderer->displaying_tsurface);
   ELOGF("E_PLANE_RENDERER", "Previous  tsurface(%p)", NULL, renderer->previous_tsurface);

   EINA_LIST_FOREACH(renderer->exported_surfaces, l, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;

        ELOGF("E_PLANE_RENDERER", "Exported tsurface(%p)", NULL, tmp_tsurface);
     }

   EINA_LIST_FOREACH(renderer->released_surfaces, l, tmp_tsurface)
     {
        if (!tmp_tsurface) continue;

        ELOGF("E_PLANE_RENDERER", "Released tsurface(%p)", NULL, tmp_tsurface);
     }

   if (renderer->ec)
     {
        renderer_client = e_plane_renderer_client_get(renderer->ec);
        EINA_SAFETY_ON_NULL_RETURN(renderer_client);

        EINA_LIST_FOREACH(renderer_client->exported_surfaces, l, tmp_tsurface)
          {
             if (!tmp_tsurface) continue;

             ELOGF("E_PLANE_RENDERER", "Client ec(%p) exported tsurface(%p)", NULL, renderer->ec, tmp_tsurface);
          }
     }
}


EINTERN int
e_plane_renderer_render_count_get(E_Plane_Renderer *renderer)
{
   int dequeue_num = 0;
   int enqueue_num = 0;
   int export_num = 0;
   int count = 0;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, 0);

   if (!renderer->tqueue) return 0;

   tsq_err = tbm_surface_queue_get_trace_surface_num(renderer->tqueue, TBM_SURFACE_QUEUE_TRACE_DEQUEUE, &dequeue_num);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail to tbm_surface_queue_get_trace_surface_num");
        return 0;
     }

   tsq_err = tbm_surface_queue_get_trace_surface_num(renderer->tqueue, TBM_SURFACE_QUEUE_TRACE_ENQUEUE, &enqueue_num);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail to tbm_surface_queue_get_trace_surface_num");
        return 0;
     }

   export_num = eina_list_count(renderer->exported_surfaces);
   if (export_num < 0)
     {
        ERR("invalid export_num");
        return 0;
     }

   count = dequeue_num + enqueue_num - export_num;
   if (count < 0)
     {
        ERR("invalid render_count dequeue:%d enqueue:%d export:%d",
            dequeue_num, enqueue_num, export_num);
        return 0;
     }

   return count;
}

EINTERN void
e_plane_renderer_surface_queue_sync_count_set(E_Plane_Renderer *renderer, unsigned int sync_count)
{
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN(renderer);
   EINA_SAFETY_ON_NULL_RETURN(renderer->tqueue);

   tsq_err = tbm_surface_queue_set_sync_count(renderer->tqueue, sync_count);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     ERR("fail to tbm_surface_queue_set_sync_count");

   if (renderer_trace_debug)
     ELOGF("E_PLANE_RENDERER", "Set      Renderer(%p) sync_count(%d)",
           NULL, renderer, sync_count);
}

EINTERN void
e_plane_renderer_ecore_evas_force_render(E_Plane_Renderer *renderer)
{
   int w, h;

   EINA_SAFETY_ON_NULL_RETURN(renderer);

   if (!renderer->ee) return;
   if (!renderer->evas) return;

   ecore_evas_geometry_get(renderer->ee, 0, 0, &w, &h);
   evas_damage_rectangle_add(renderer->evas, 0, 0, w,  h);
}
