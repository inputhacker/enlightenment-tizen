#include "e.h"

# include <gbm/gbm_tbm.h>
# include <tdm.h>
# include <tdm_helper.h>
# include <tbm_surface.h>
# include <tbm_surface_internal.h>
# include <wayland-tbm-server.h>
# include <Evas_Engine_GL_Drm.h>
# include <Evas_Engine_Software_Tbm.h>

# ifndef CLEAR
# define CLEAR(x) memset(&(x), 0, sizeof (x))
# endif

/* E_Plane is a child object of E_Output. There is one Output per screen
 * E_plane represents hw overlay and a surface is assigned to disable composition
 * Each Output always has dedicated canvas and a zone
 */

///////////////////////////////////////////
static E_Client_Hook *client_hook_new = NULL;
static E_Client_Hook *client_hook_del = NULL;
static const char *_e_plane_ec_last_err = NULL;
static Eina_Bool plane_trace_debug = 0;

E_API int E_EVENT_PLANE_WIN_CHANGE = -1;

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

static tbm_surface_queue_h
_get_tbm_surface_queue(E_Comp *e_comp)
{
   const char* name;
   tbm_surface_queue_h tbm_queue = NULL;

   name = ecore_evas_engine_name_get(e_comp->ee);
   if (!strcmp(name, "gl_drm"))
     {
        Evas_Engine_Info_GL_Drm *info;
        info = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
        if (info->info.surface)
           tbm_queue = gbm_tbm_get_surface_queue(info->info.surface);
     }
   else if(!strcmp(name, "drm_tbm"))
     {
        Evas_Engine_Info_Software_Tbm *info;
        info = (Evas_Engine_Info_Software_Tbm *)evas_engine_info_get(e_comp->evas);
        tbm_queue = (tbm_surface_queue_h)info->info.tbm_queue;
     }

   return tbm_queue;
}

static Eina_Bool
_e_plane_surface_unset(E_Plane *plane)
{
   tdm_layer *tlayer = plane->tlayer;
   tdm_error error;

   if (plane_trace_debug)
       ELOGF("E_PLANE", "Unset  Plane(%p)", NULL, NULL, plane);

   error = tdm_layer_unset_buffer(tlayer);
   if (error != TDM_ERROR_NONE)
     {
         ERR("fail to tdm_layer_unset_buffer");
         return EINA_FALSE;
     }

   plane->tsurface = NULL;
   plane->need_to_commit = EINA_TRUE;

   if (plane->renderer)
     {
        /* set the displaying buffer to be null */
        e_plane_renderer_displaying_surface_set(plane->renderer, NULL);
        /* set the update_exist to be false */
        e_plane_renderer_update_exist_set(plane->renderer, EINA_FALSE);

        /* set the display_buffer_ref to be null */
        e_comp_wl_buffer_reference(&plane->displaying_buffer_ref, NULL);
     }

   return EINA_TRUE;
}

static void
_e_plane_ev_free(void *d EINA_UNUSED, E_Event_Plane_Win_Change *ev)
{
   if (ev->ec) e_object_unref(E_OBJECT(ev->ec));
   E_FREE(ev);
}

static void
_e_plane_ev(E_Plane *ep, int type)
{
   E_Event_Plane_Win_Change *ev;

   if (!ep->need_ev) return;

   ev = E_NEW(E_Event_Plane_Win_Change, 1);
   EINA_SAFETY_ON_NULL_RETURN(ev);

   ev->ep = ep;
   ev->ec = ep->ec;

   if ((ep->ec) && (!e_object_is_del(E_OBJECT(ep->ec))))
     e_object_ref(E_OBJECT(ep->ec));

   ecore_event_add(type, ev, (Ecore_End_Cb)_e_plane_ev_free, NULL);

   ep->need_ev = EINA_FALSE;
}

static Eina_Bool
_e_plane_surface_set(E_Plane *plane, tbm_surface_h tsurface)
{
   tbm_surface_info_s surf_info;
   tdm_error error;
   tdm_layer *tlayer = plane->tlayer;
   E_Output *output = plane->output;
   E_Client *ec = plane->ec;
   int aligned_width;

   /* set layer when the layer infomation is different from the previous one */
   tbm_surface_get_info(tsurface, &surf_info);

   switch (surf_info.format)
     {
      case TBM_FORMAT_YUV420:
      case TBM_FORMAT_YVU420:
      case TBM_FORMAT_YUV422:
      case TBM_FORMAT_YVU422:
      case TBM_FORMAT_NV12:
      case TBM_FORMAT_NV21:
        aligned_width = surf_info.planes[0].stride;
        break;
      case TBM_FORMAT_YUYV:
      case TBM_FORMAT_UYVY:
        aligned_width = surf_info.planes[0].stride >> 1;
        break;
      case TBM_FORMAT_ARGB8888:
      case TBM_FORMAT_XRGB8888:
        aligned_width = surf_info.planes[0].stride >> 2;
        break;
      default:
        ERR("not supported format: %x", surf_info.format);
        return EINA_FALSE;
     }

   if (ec)
     {
        if (plane->info.src_config.size.h != aligned_width ||
            plane->info.src_config.size.v != surf_info.height ||
            plane->info.src_config.pos.x != 0 ||
            plane->info.src_config.pos.y != 0 ||
            plane->info.src_config.pos.w != surf_info.width ||
            plane->info.src_config.pos.h != surf_info.height ||
            plane->info.dst_pos.x != ec->x ||
            plane->info.dst_pos.y != ec->y ||
            plane->info.dst_pos.w != surf_info.width ||
            plane->info.dst_pos.h != surf_info.height ||
            plane->info.transform != TDM_TRANSFORM_NORMAL)
          {
              plane->info.src_config.size.h = aligned_width;
              plane->info.src_config.size.v = surf_info.height;
              plane->info.src_config.pos.x = 0;
              plane->info.src_config.pos.y = 0;
              plane->info.src_config.pos.w = surf_info.width;
              plane->info.src_config.pos.h = surf_info.height;
              plane->info.dst_pos.x = ec->x;
              plane->info.dst_pos.y = ec->y;
              plane->info.dst_pos.w = surf_info.width;
              plane->info.dst_pos.h = surf_info.height;
              plane->info.transform = TDM_TRANSFORM_NORMAL;

              error = tdm_layer_set_info(tlayer, &plane->info);
              if (error != TDM_ERROR_NONE)
                {
                  ERR("fail to tdm_layer_set_info");
                  return EINA_FALSE;
                }
          }
     }
   else
     {
        if (plane->info.src_config.size.h != aligned_width ||
            plane->info.src_config.size.v != surf_info.height ||
            plane->info.src_config.pos.x != 0 ||
            plane->info.src_config.pos.y != 0 ||
            plane->info.src_config.pos.w != surf_info.width ||
            plane->info.src_config.pos.h != surf_info.height ||
            plane->info.dst_pos.x != output->config.geom.x ||
            plane->info.dst_pos.y != output->config.geom.y ||
            plane->info.dst_pos.w != output->config.geom.w ||
            plane->info.dst_pos.h != output->config.geom.h ||
            plane->info.transform != TDM_TRANSFORM_NORMAL)
          {
              plane->info.src_config.size.h = aligned_width;
              plane->info.src_config.size.v = surf_info.height;
              plane->info.src_config.pos.x = 0;
              plane->info.src_config.pos.y = 0;
              plane->info.src_config.pos.w = surf_info.width;
              plane->info.src_config.pos.h = surf_info.height;
              plane->info.dst_pos.x = output->config.geom.x;
              plane->info.dst_pos.y = output->config.geom.y;
              plane->info.dst_pos.w = output->config.geom.w;
              plane->info.dst_pos.h = output->config.geom.h;
              plane->info.transform = TDM_TRANSFORM_NORMAL;

              error = tdm_layer_set_info(tlayer, &plane->info);
              if (error != TDM_ERROR_NONE)
                {
                  ERR("fail to tdm_layer_set_info");
                  return EINA_FALSE;
                }
          }
     }

   if (plane_trace_debug)
     {
        ELOGF("E_PLANE", "Set  Plane(%p)  tsurface(%p) (%dx%d,[%d,%d,%d,%d]=>[%d,%d,%d,%d])",
              NULL, NULL, plane, tsurface,
              plane->info.src_config.size.h, plane->info.src_config.size.h,
              plane->info.src_config.pos.x, plane->info.src_config.pos.y,
              plane->info.src_config.pos.w, plane->info.src_config.pos.h,
              plane->info.dst_pos.x, plane->info.dst_pos.y,
              plane->info.dst_pos.w, plane->info.dst_pos.h);
     }

   error = tdm_layer_set_buffer(tlayer, tsurface);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_set_buffer");
        return EINA_FALSE;
     }

   _e_plane_ev(plane, E_EVENT_PLANE_WIN_CHANGE);

   return EINA_TRUE;
}

static void
_e_plane_surface_on_client_reserved_release(E_Plane *plane, tbm_surface_h tsurface)
{
   E_Plane_Renderer *renderer = plane->renderer;
   E_Client *ec = plane->ec;

   if (!ec)
     {
        ERR("no ec at plane.");
        return;
     }

   /* release the tsurface */
   e_plane_renderer_surface_send(renderer, ec, tsurface);
}

static tbm_surface_h
_e_plane_surface_from_client_acquire_reserved(E_Plane *plane)
{
   E_Client *ec = plane->ec;
   tbm_surface_h tsurface = NULL;
   E_Plane_Renderer *renderer = plane->renderer;
   E_Plane_Renderer_Client *renderer_client = NULL;

   /* check the ec is set to the renderer */
   renderer_client = e_plane_renderer_client_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer_client, NULL);

   if (e_comp_object_hwc_update_exists(ec->frame))
     {
        e_comp_object_hwc_update_set(ec->frame, EINA_FALSE);

        if (plane_trace_debug)
           ELOGF("E_PLANE", "Plane:%p Display Client", ec->pixmap, ec, plane);

        if (!e_plane_renderer_surface_queue_clear(renderer))
           ERR("fail to e_plane_renderer_surface_queue_clear");

        /* acquire the surface from the client_queue */
        tsurface = e_plane_renderer_client_surface_recieve(renderer_client);
        if (!tsurface)
           ERR("fail to e_plane_renderer_client_surface_recieve");

        /* enqueue the surface to the layer_queue */
        if (!e_plane_renderer_surface_queue_enqueue(plane->renderer, tsurface))
          {
              e_plane_renderer_surface_send(renderer, ec, tsurface);
              ERR("fail to e_plane_renderer_surface_queue_enqueue");
          }
     }

   /* aquire */
   tsurface = e_plane_renderer_surface_queue_acquire(plane->renderer);

   return tsurface;
}

static void
_e_plane_surface_on_client_release(E_Plane *plane, tbm_surface_h tsurface)
{
   E_Client *ec = plane->ec;

   if (!ec)
     {
        ERR("no ec at plane.");
        return;
     }
}

static tbm_surface_h
_e_plane_surface_from_client_acquire(E_Plane *plane)
{
   E_Client *ec = plane->ec;
   E_Pixmap *pixmap = ec->pixmap;
   E_Comp_Wl_Buffer *buffer = e_pixmap_resource_get(pixmap);
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Plane_Renderer *renderer = plane->renderer;
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, NULL);

   if (!e_comp_object_hwc_update_exists(ec->frame)) return NULL;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Display Client Plane(%p)", pixmap, ec, plane);

   e_comp_object_hwc_update_set(ec->frame, EINA_FALSE);

   if (plane->is_fb)
     {
        if (!e_plane_renderer_surface_queue_clear(renderer))
            ERR("fail to e_plane_renderer_surface_queue_clear");
     }

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
   if (!tsurface)
     {
        ERR("fail to wayland_tbm_server_get_surface");
        return NULL;
     }

   return tsurface;
}

static void
_e_plane_surface_on_ecore_evas_release(E_Plane *plane, tbm_surface_h tsurface)
{
   /* release the tsurface */
   e_plane_renderer_surface_queue_release(plane->renderer, tsurface);
}

static tbm_surface_h
_e_plane_surface_from_ecore_evas_acquire(E_Plane *plane)
{
   E_Plane_Renderer *renderer = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_h tqueue = NULL;
   E_Output *output = plane->output;

   renderer = plane->renderer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(renderer, NULL);

   if (!renderer->tqueue)
     {
        if(!(tqueue = _get_tbm_surface_queue(e_comp)))
          {
             ERR("fail to _get_tbm_surface_queue");
             return NULL;
          }

        if (!e_plane_renderer_surface_queue_set(renderer, tqueue))
          {
             ERR("fail to e_plane_renderer_queue_set");
             return NULL;
          }

        /* dpms on at the first */
        if (!e_output_dpms_set(output, E_OUTPUT_DPMS_ON))
           WRN("fail to set the dpms on.");
     }

   /* aquire */
   tsurface = e_plane_renderer_surface_queue_acquire(plane->renderer);

   if (tsurface)
     {
        if (plane_trace_debug)
           ELOGF("E_PLANE", "Display Canvas Plane(%p)", NULL, NULL, plane);
     }

   return tsurface;
}

static void
_e_plane_surface_send_dequeuable_surfaces(E_Plane *plane)
{
   tbm_surface_h tsurface = NULL;
   E_Plane_Renderer *renderer = plane->renderer;

   /* export dequeuable surface */
   while(e_plane_renderer_surface_queue_can_dequeue(renderer))
     {
        /* dequeue */
        tsurface = e_plane_renderer_surface_queue_dequeue(renderer);
        if (!tsurface)
          {
             ERR("fail to dequeue surface");
             continue;
          }

        e_plane_renderer_surface_send(renderer, renderer->ec, tsurface);
    }
}

static void
_e_plane_renderer_client_cb_new(void *data EINA_UNUSED, E_Client *ec)
{
   E_Plane_Renderer_Client *renderer_client = NULL;

   renderer_client = e_plane_renderer_client_new(ec);
   EINA_SAFETY_ON_NULL_RETURN(renderer_client);

   ec->renderer_client = renderer_client;
}

static void
_e_plane_renderer_client_cb_del(void *data EINA_UNUSED, E_Client *ec)
{
   E_Plane *plane = NULL;
   E_Plane_Renderer *renderer = NULL;
   E_Plane_Renderer_Client *renderer_client = NULL;

   renderer_client = e_plane_renderer_client_get(ec);
   if (!renderer_client) return;

   renderer = e_plane_renderer_client_renderer_get(renderer_client);
   if (renderer)
     {
        plane = e_plane_renderer_plane_get(renderer);
        if (!plane)
          {
             e_plane_renderer_client_free(renderer_client);
             ec->renderer_client = NULL;
             return;
          }

        e_plane_ec_set(plane, NULL);

        if (plane->ec == ec)
          {
             plane->ec = NULL;
             plane->need_ev = EINA_TRUE;
          }
     }

   /* destroy the renderer_client */
   e_plane_renderer_client_free(renderer_client);
   ec->renderer_client = NULL;
}

EINTERN Eina_Bool
e_plane_init(void)
{
#ifdef HAVE_HWC
   if (client_hook_new) return EINA_TRUE;
   if (client_hook_del) return EINA_TRUE;

   client_hook_new =  e_client_hook_add(E_CLIENT_HOOK_NEW_CLIENT, _e_plane_renderer_client_cb_new, NULL);
   client_hook_del =  e_client_hook_add(E_CLIENT_HOOK_DEL, _e_plane_renderer_client_cb_del, NULL);

   /* e_renderer init */
   if (!e_plane_renderer_init())
     {
        ERR("fail to e_plane_renderer_init.");
        return EINA_FALSE;
     }
#endif

   E_EVENT_PLANE_WIN_CHANGE = ecore_event_type_new();

   return EINA_TRUE;
}

EINTERN void
e_plane_shutdown(void)
{
#ifdef HAVE_HWC
   /* e_plane_renderer_shutdown */
   e_plane_renderer_shutdown();

   if (client_hook_new)
     {
        e_client_hook_del(client_hook_new);
        client_hook_new = NULL;
     }

   if (client_hook_del)
     {
        e_client_hook_del(client_hook_del);
        client_hook_del = NULL;
     }
#endif
}

EINTERN E_Plane *
e_plane_new(E_Output *output, int index)
{
   E_Plane *plane = NULL;
   tdm_layer *tlayer = NULL;
   tdm_output *toutput = NULL;
   tdm_layer_capability layer_capabilities;
   char name[40];
   E_Plane_Renderer *renderer = NULL;
   tdm_error tdm_err = TDM_ERROR_NONE;
   unsigned int buffer_flags = 0;
   int zpos;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   toutput = output->toutput;
   EINA_SAFETY_ON_NULL_RETURN_VAL(toutput, NULL);

   plane = E_NEW(E_Plane, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);
   plane->index = index;

   tlayer = tdm_output_get_layer(toutput, index, NULL);
   if (!tlayer)
   {
      ERR("fail to get layer.");
      free(plane);
      return NULL;
   }
   plane->tlayer = tlayer;

   snprintf(name, sizeof(name), "%s-plane-%d", output->id, index);
   plane->name = eina_stringshare_add(name);

   CLEAR(layer_capabilities);
   tdm_layer_get_capabilities(plane->tlayer, &layer_capabilities);

   /* check that the layer uses the reserve nd memory */
   if (layer_capabilities & TDM_LAYER_CAPABILITY_RESEVED_MEMORY)
       plane->reserved_memory = EINA_TRUE;

   tdm_layer_get_zpos(tlayer, &zpos);
   plane->zpos = zpos;
   plane->output = output;

   tdm_err = tdm_layer_get_buffer_flags(plane->tlayer, &buffer_flags);
   if (tdm_err == TDM_ERROR_NONE)
      plane->buffer_flags = buffer_flags;

   /* check the layer is the primary layer */
   if (layer_capabilities & TDM_LAYER_CAPABILITY_PRIMARY)
     {
        plane->is_primary = EINA_TRUE;
        plane->is_fb = EINA_TRUE; // TODO: query from libtdm if it is fb target plane

        renderer = e_plane_renderer_new(plane);
        if (!renderer)
          {
              ERR("fail to e_plane_renderer_new");
              free(plane);
              return NULL;
          }

        plane->renderer = renderer;
     }

   if (layer_capabilities & TDM_LAYER_CAPABILITY_VIDEO)
      plane->type = E_PLANE_TYPE_VIDEO;
   else if (layer_capabilities & TDM_LAYER_CAPABILITY_CURSOR)
      plane->type = E_PLANE_TYPE_CURSOR;
   else if (layer_capabilities & TDM_LAYER_CAPABILITY_GRAPHIC)
      plane->type = E_PLANE_TYPE_GRAPHIC;
   else
      plane->type = E_PLANE_TYPE_INVALID;

   INF("E_PLANE: (%d) plane:%p name:%s zpos:%d capa:%s %s",
       index, plane, plane->name, plane->zpos,plane->is_primary?"primary":"", plane->reserved_memory?"reserved_memory":"");

   return plane;
}

EINTERN void
e_plane_free(E_Plane *plane)
{
   if (!plane) return;

   if (plane->name) eina_stringshare_del(plane->name);
   if (plane->renderer) e_plane_renderer_del(plane->renderer);
   if (plane->ec) e_plane_ec_set(plane, NULL);

   free(plane);
}

EINTERN Eina_Bool
e_plane_hwc_setup(E_Plane *plane)
{
   const char *name;
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   /* we assume that the primary plane gets a ecore_evas */
   if (!plane->is_fb) return EINA_FALSE;

   name = ecore_evas_engine_name_get(e_comp->ee);
   if (!strcmp("gl_drm", name))
     {
        Evas_Engine_Info_GL_Drm *einfo = NULL;
        /* get the evas_engine_gl_drm information */
        einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
        if (!einfo) return EINA_FALSE;
        /* enable hwc to evas engine gl_drm */
        einfo->info.hwc_enable = EINA_TRUE;
        return EINA_TRUE;
     }
   else if(!strcmp("drm_tbm", name))
     {
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_plane_render(E_Plane *plane)
{
   E_Plane_Renderer *renderer = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   renderer = plane->renderer;
   if (!renderer) return EINA_TRUE;
   if (plane->ec) return EINA_TRUE;

   if (!e_plane_renderer_render(renderer, plane->is_fb))
     {
        ERR("fail to e_plane_renderer_render");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_fetch(E_Plane *plane)
{
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (!plane->renderer && plane->tsurface &&
       !plane->pending_commit_data)
     {
        if (!_e_plane_surface_unset(plane))
          {
             ERR("failed to unset surface plane:%p", plane);
             return EINA_FALSE;
          }
     }

   if (plane->need_to_commit)
     {
        plane->need_to_commit = EINA_FALSE;
        return EINA_TRUE;
     }

   if (plane->pending_commit_data)
      return EINA_FALSE;

   if (plane->is_fb && !plane->ec)
     {
        /* acquire the surface */
        tsurface = _e_plane_surface_from_ecore_evas_acquire(plane);
     }
   else
     {
        if (!plane->ec) return EINA_FALSE;

        /* acquire the surface */
        if (plane->reserved_memory)
          tsurface = _e_plane_surface_from_client_acquire_reserved(plane);
        else
          tsurface = _e_plane_surface_from_client_acquire(plane);

        /* For send frame::done to client */
        if (!tsurface)
          e_pixmap_image_clear(plane->ec->pixmap, 1);
     }

   if (!tsurface) return EINA_FALSE;

   e_plane_renderer_previous_surface_set(plane->renderer, plane->tsurface);
   plane->tsurface = tsurface;

   /* set plane info and set tsurface to the plane */
   if (!_e_plane_surface_set(plane, tsurface))
     {
        ERR("fail: _e_plane_set_info.");
        e_plane_unfetch(plane);
        return EINA_FALSE;
     }

   /* set the update_exist to be true */
   e_plane_renderer_update_exist_set(plane->renderer, EINA_TRUE);

   return EINA_TRUE;
}

EINTERN void
e_plane_unfetch(E_Plane *plane)
{
   tbm_surface_h displaying_tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN(plane);
   EINA_SAFETY_ON_NULL_RETURN(plane->tsurface);

   if (plane->is_fb && !plane->ec)
     {
        _e_plane_surface_on_ecore_evas_release(plane, plane->tsurface);
     }
   else
     {
        if (!plane->ec) return;

        if (plane->reserved_memory) _e_plane_surface_on_client_reserved_release(plane, plane->tsurface);
        else _e_plane_surface_on_client_release(plane, plane->tsurface);
     }

   displaying_tsurface = e_plane_renderer_displaying_surface_get(plane->renderer);

   plane->tsurface = displaying_tsurface;

   /* set plane info and set prevous tsurface to the plane */
   if (!_e_plane_surface_set(plane, displaying_tsurface))
     {
        ERR("fail: _e_plane_set_info.");
        return;
     }
}

EINTERN E_Plane_Commit_Data *
e_plane_commit_data_aquire(E_Plane *plane)
{
   E_Plane_Commit_Data *data = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   if (!plane->renderer) return NULL;

   /* check update_exist */
   if (!e_plane_renderer_update_exist_check(plane->renderer))
     return NULL;

   if (plane->is_fb && !plane->ec)
     {
        data = E_NEW(E_Plane_Commit_Data, 1);
        data->plane = plane;
        data->tsurface = plane->tsurface;
        tbm_surface_internal_ref(data->tsurface);
        data->ec = NULL;

        /* set the update_exist to be false */
        e_plane_renderer_update_exist_set(plane->renderer, EINA_FALSE);
        /* set the pending to be true */
        e_plane_renderer_pending_set(plane->renderer, EINA_TRUE);
        plane->pending_commit_data = data;

        return data;
     }
   else
     {
        if (plane->ec)
          {
             data = E_NEW(E_Plane_Commit_Data, 1);
             data->plane = plane;
             data->tsurface = plane->tsurface;
             tbm_surface_internal_ref(data->tsurface);
             data->ec = plane->ec;
             e_comp_wl_buffer_reference(&data->buffer_ref, e_pixmap_resource_get(plane->ec->pixmap));

             /* set the update_exist to be false */
             e_plane_renderer_update_exist_set(plane->renderer, EINA_FALSE);
             /* set the pending to be true */
             e_plane_renderer_pending_set(plane->renderer, EINA_TRUE);
             plane->pending_commit_data = data;

             /* send frame event enlightenment dosen't send frame evnet in nocomp */
             e_pixmap_image_clear(plane->ec->pixmap, 1);
             return data;
          }
     }

   return NULL;
}

EINTERN void
e_plane_commit_data_release(E_Plane_Commit_Data *data)
{
   E_Plane *plane = NULL;
   E_Plane_Renderer *renderer = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_h displaying_tsurface = NULL;
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN(data);

   plane = data->plane;
   tsurface = data->tsurface;
   ec = data->ec;
   renderer = plane->renderer;

   if (!renderer)
     {
        if (!_e_plane_surface_unset(plane))
           ERR("failed to unset surface plane:%p", plane);

        e_comp_wl_buffer_reference(&data->buffer_ref, NULL);
        plane->pending_commit_data = NULL;
        tbm_surface_internal_unref(tsurface);
        free(data);
        return;
     }

   displaying_tsurface = e_plane_renderer_displaying_surface_get(renderer);

   if (plane->is_fb && !ec)
     {
        /* composite */
        /* debug */
        if (plane_trace_debug)
          ELOGF("E_PLANE", "Done    Plane(%p)  tsurface(%p) tqueue(%p) data(%p)::Canvas",
               NULL, NULL, plane, tsurface, renderer->tqueue, data);
        if (plane->reserved_memory)
          {
             if (displaying_tsurface)
               {
                  e_plane_renderer_surface_queue_release(plane->renderer, displaying_tsurface);

                  if (plane->ec)
                    {
                       _e_plane_surface_on_client_reserved_release(plane, displaying_tsurface);
                       _e_plane_surface_send_dequeuable_surfaces(plane);
                    }

               }
          }
        else
          {
             if (displaying_tsurface && !plane->displaying_buffer_ref.buffer)
                e_plane_renderer_surface_queue_release(plane->renderer, displaying_tsurface);
          }

        e_comp_wl_buffer_reference(&plane->displaying_buffer_ref, NULL);
        e_plane_renderer_displaying_surface_set(renderer, tsurface);
     }
   else
     {
        /* no composite */
        /* debug */
        if (plane_trace_debug)
           ELOGF("E_PLANE", "Done    Plane(%p)     wl_buffer(%p) tsurface(%p) tqueue(%p) data(%p) wl_buffer_ref(%p) ::Client",
             ec->pixmap, ec, plane, _get_wl_buffer(ec), tsurface, renderer->tqueue, data, _get_wl_buffer_ref(ec));

        if (plane->reserved_memory)
          {
             /* release */
             if (displaying_tsurface)
               {
                  e_plane_renderer_surface_queue_release(plane->renderer, displaying_tsurface);

                  if (plane->ec)
                    {
                       _e_plane_surface_on_client_reserved_release(plane, displaying_tsurface);
                       _e_plane_surface_send_dequeuable_surfaces(plane);
                    }
               }
          }
        else
          {
             /* release */
             if (displaying_tsurface)
               {
                  _e_plane_surface_on_client_release(plane, displaying_tsurface);

                  if (!plane->displaying_buffer_ref.buffer)
                     e_plane_renderer_surface_queue_release(plane->renderer, displaying_tsurface);
               }
          }

        e_comp_wl_buffer_reference(&plane->displaying_buffer_ref, data->buffer_ref.buffer);
        e_plane_renderer_displaying_surface_set(renderer, tsurface);

        e_comp_wl_buffer_reference(&data->buffer_ref, NULL);
     }

   /* set the pending to be false */
   e_plane_renderer_pending_set(plane->renderer, EINA_FALSE);
   plane->pending_commit_data = NULL;

   tbm_surface_internal_unref(tsurface);
   free(data);
}

EINTERN Eina_Bool
e_plane_is_reserved(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   return plane->is_reserved;
}

EINTERN void
e_plane_reserved_set(E_Plane *plane, Eina_Bool set)
{
   E_Plane_Renderer *renderer = NULL;

   EINA_SAFETY_ON_NULL_RETURN(plane);

   if (!set && plane->is_reserved)
     {
        renderer = plane->renderer;
        if (renderer)
          {
             if (plane->is_fb)
               {
                  e_plane_renderer_ecore_evas_use(plane->renderer);
               }
             else
               {
                  e_plane_renderer_del(renderer);
                  plane->renderer = NULL;
               }
          }
     }

   plane->is_reserved = set;
}

EINTERN void
e_plane_hwc_trace_debug(Eina_Bool onoff)
{
   if (onoff == plane_trace_debug) return;
   plane_trace_debug = onoff;
   e_plane_renderer_hwc_trace_debug(onoff);
   INF("Plane: hwc trace_debug is %s", onoff?"ON":"OFF");
}

E_API Eina_Bool
e_plane_type_set(E_Plane *plane, E_Plane_Type type)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   plane->type = type;

   return EINA_TRUE;
}

E_API Eina_Bool
e_plane_role_set(E_Plane *plane, E_Plane_Role role)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   plane->role = role;

   return EINA_TRUE;
}

E_API E_Plane_Role
e_plane_role_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, E_PLANE_ROLE_NONE);

   return plane->role;
}

E_API E_Plane_Type
e_plane_type_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, E_PLANE_TYPE_INVALID);

   return plane->type;
}

E_API E_Client *
e_plane_ec_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   return plane->ec;
}

E_API Eina_Bool
e_plane_ec_set(E_Plane *plane, E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (plane_trace_debug)
      ELOGF("E_PLANE", "Request Plane(%p) ec Set", (ec ? ec->pixmap : NULL), ec, plane);

   if (ec)
     {
        if (plane->ec == ec) return EINA_TRUE;

        if (plane->reserved_memory)
           e_plane_reserved_set(plane, EINA_TRUE);

        if (!plane->is_fb)
          {
             if (!plane->renderer)
                plane->renderer = e_plane_renderer_new(plane);
          }

        EINA_SAFETY_ON_NULL_RETURN_VAL(plane->renderer, EINA_FALSE);

        if (!e_plane_renderer_ec_set(plane->renderer, ec))
           return EINA_FALSE;

        if (plane->reserved_memory)
           _e_plane_surface_send_dequeuable_surfaces(plane);

        e_comp_object_hwc_update_set(ec->frame, EINA_TRUE);
     }
   else
     {
        if (!plane->is_fb)
          {
             if (plane->renderer)
               {
                  e_plane_renderer_del(plane->renderer);
                  plane->renderer = NULL;
               }
          }
        else
          {
             if (!e_plane_renderer_ecore_evas_use(plane->renderer))
               {
                 ERR("failed to use ecore_evas plane:%p", plane);
                 return EINA_FALSE;
               }
          }
     }

   plane->ec = ec;
   plane->need_ev = EINA_TRUE;

   if (plane_trace_debug)
      ELOGF("E_PLANE", "Plane(%p) ec Set", (ec ? ec->pixmap : NULL), ec, plane);

   return EINA_TRUE;
}

E_API E_Client *
e_plane_ec_prepare_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   return plane->prepare_ec;
}

E_API Eina_Bool
e_plane_ec_prepare_set(E_Plane *plane, E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   plane->prepare_ec = ec;

   return EINA_TRUE;
}

E_API const char *
e_plane_ec_prepare_set_last_error_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   return _e_plane_ec_last_err;
}

E_API Eina_Bool
e_plane_is_primary(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (plane->is_primary) return EINA_TRUE;

   return EINA_FALSE;
}

E_API Eina_Bool
e_plane_is_cursor(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (plane->type == E_PLANE_TYPE_CURSOR) return EINA_TRUE;

   return EINA_FALSE;
}

E_API E_Plane_Color
e_plane_color_val_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, E_PLANE_COLOR_INVALID);

   return plane->color;
}

E_API Eina_Bool
e_plane_is_fb_target(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (plane->is_fb) return EINA_TRUE;

   return EINA_FALSE;
}
