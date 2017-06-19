#include "e.h"

# include <gbm/gbm_tbm.h>
# include <tdm.h>
# include <tdm_helper.h>
# include <tbm_surface.h>
# include <tbm_surface_internal.h>
# include <wayland-tbm-server.h>
# include <Evas_Engine_GL_Drm.h>
# include <Evas_Engine_GL_Tbm.h>
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
static void _e_plane_zoom_pending_data_pp(E_Plane *plane);

E_API int E_EVENT_PLANE_WIN_CHANGE = -1;

static E_Comp_Wl_Buffer *
_get_comp_wl_buffer(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   if (!cdata) return NULL;

   E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata->buffer_ref;

   return buffer_ref->buffer;
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
   else if(!strcmp(name, "gl_drm_tbm"))
     {
        Evas_Engine_Info_GL_Tbm *info;
        info = (Evas_Engine_Info_GL_Tbm *)evas_engine_info_get(e_comp->evas);
        EINA_SAFETY_ON_NULL_RETURN_VAL(info, NULL);
        tbm_queue = (tbm_surface_queue_h)info->info.tbm_queue;
     }
   else if(!strcmp(name, "drm_tbm"))
     {
        Evas_Engine_Info_Software_Tbm *info;
        info = (Evas_Engine_Info_Software_Tbm *)evas_engine_info_get(e_comp->evas);
        EINA_SAFETY_ON_NULL_RETURN_VAL(info, NULL);
        tbm_queue = (tbm_surface_queue_h)info->info.tbm_queue;
     }

   return tbm_queue;
}

static void
_e_plane_renderer_unset(E_Plane *plane)
{
   Eina_List *data_l;
   E_Plane_Commit_Data *data = NULL;

   plane->display_info.renderer = NULL;

   EINA_LIST_FOREACH(plane->commit_data_list, data_l, data)
     data->renderer = NULL;

   if (plane->renderer)
     e_plane_renderer_del(plane->renderer);

   plane->renderer = NULL;
}

static Eina_Bool
_e_plane_surface_unset(E_Plane *plane)
{
   tdm_layer *tlayer = plane->tlayer;
   tdm_error error;

   if (plane_trace_debug)
      ELOGF("E_PLANE", "Unset   Plane(%p) zpos(%d)", NULL, NULL, plane, plane->zpos);

   CLEAR(plane->info);

   if (plane->activation)
     {
        error = tdm_layer_unset_buffer(tlayer);
        if (error != TDM_ERROR_NONE)
          {
              ERR("fail to tdm_layer_unset_buffer");
              return EINA_FALSE;
          }
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

static unsigned int
_e_plane_aligned_width_get(tbm_surface_h tsurface)
{
   unsigned int aligned_width = 0;
   tbm_surface_info_s surf_info;

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
     }

   return aligned_width;
}

static Eina_Bool
_e_plane_surface_set(E_Plane *plane, tbm_surface_h tsurface)
{
   tbm_surface_info_s surf_info;
   tdm_error error;
   tdm_layer *tlayer = plane->tlayer;
   E_Output *output = plane->output;
   E_Client *ec = plane->ec;
   unsigned int aligned_width;
   int dst_x, dst_y, dst_w, dst_h;

   /* set layer when the layer infomation is different from the previous one */
   tbm_surface_get_info(tsurface, &surf_info);

   aligned_width = _e_plane_aligned_width_get(tsurface);
   if (aligned_width == 0) return EINA_FALSE;

   if (ec)
     {
        if (plane->role == E_PLANE_ROLE_CURSOR)
          {
             E_Pointer *pointer = e_pointer_get(ec);
             if (!pointer)
               {
                  ERR("ec doesn't have E_Pointer");
                  return EINA_FALSE;
               }

             dst_x = pointer->x - pointer->hot.x;
             dst_y = pointer->y - pointer->hot.y;
          }
        else
          {
             dst_x = ec->x;
             dst_y = ec->y;
          }

        /* if output is transformed, the position of a buffer on screen should be also
         * transformed.
         */
        if (output->config.rotation > 0)
          {
             int bw, bh;
             e_pixmap_size_get(ec->pixmap, &bw, &bh);
             e_comp_wl_rect_convert(ec->zone->w, ec->zone->h,
                                    output->config.rotation / 90, 1,
                                    dst_x, dst_y, bw, bh,
                                    &dst_x, &dst_y, NULL, NULL);
          }
        dst_w = surf_info.width;
        dst_h = surf_info.height;
     }
   else
     {
        dst_x = output->config.geom.x;
        dst_y = output->config.geom.y;
        dst_w = output->config.geom.w;
        dst_h = output->config.geom.h;
     }

   if (plane->info.src_config.size.h != aligned_width ||
       plane->info.src_config.size.v != surf_info.height ||
       plane->info.src_config.pos.x != 0 ||
       plane->info.src_config.pos.y != 0 ||
       plane->info.src_config.pos.w != surf_info.width ||
       plane->info.src_config.pos.h != surf_info.height ||
       plane->info.dst_pos.x != dst_x ||
       plane->info.dst_pos.y != dst_y ||
       plane->info.dst_pos.w != dst_w ||
       plane->info.dst_pos.h != dst_h ||
       plane->info.transform != TDM_TRANSFORM_NORMAL)
     {
        plane->info.src_config.size.h = aligned_width;
        plane->info.src_config.size.v = surf_info.height;
        plane->info.src_config.pos.x = 0;
        plane->info.src_config.pos.y = 0;
        plane->info.src_config.pos.w = surf_info.width;
        plane->info.src_config.pos.h = surf_info.height;
        plane->info.dst_pos.x = dst_x;
        plane->info.dst_pos.y = dst_y;
        plane->info.dst_pos.w = dst_w;
        plane->info.dst_pos.h = dst_h;
        plane->info.transform = TDM_TRANSFORM_NORMAL;

        if (plane->activation)
          {
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
        ELOGF("E_PLANE", "Set     Plane(%p) zpos(%d)   tsurface(%p) (%dx%d,[%d,%d,%d,%d]=>[%d,%d,%d,%d])",
              NULL, NULL, plane, plane->zpos, tsurface,
              plane->info.src_config.size.h, plane->info.src_config.size.h,
              plane->info.src_config.pos.x, plane->info.src_config.pos.y,
              plane->info.src_config.pos.w, plane->info.src_config.pos.h,
              plane->info.dst_pos.x, plane->info.dst_pos.y,
              plane->info.dst_pos.w, plane->info.dst_pos.h);
     }

   if (plane->activation)
     {
        error = tdm_layer_set_buffer(tlayer, tsurface);
        if (error != TDM_ERROR_NONE)
          {
             ERR("fail to tdm_layer_set_buffer");
             return EINA_FALSE;
          }
     }

   _e_plane_ev(plane, E_EVENT_PLANE_WIN_CHANGE);

   return EINA_TRUE;
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

   if (!e_plane_renderer_surface_queue_clear(renderer))
      ERR("fail to e_plane_renderer_surface_queue_clear");

   if (e_comp_object_hwc_update_exists(ec->frame))
     {
        e_comp_object_hwc_update_set(ec->frame, EINA_FALSE);

        if (plane_trace_debug)
          ELOGF("E_PLANE", "Display Plane(%p) zpos(%d)   Client ec(%p, %s)",
                ec->pixmap, ec, plane, plane->zpos, ec, e_client_util_name_get(ec));

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

static tbm_surface_h
_e_plane_surface_from_client_acquire(E_Plane *plane)
{
   E_Client *ec = plane->ec;
   E_Comp_Wl_Buffer *buffer = _get_comp_wl_buffer(ec);
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Plane_Renderer *renderer = plane->renderer;
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, NULL);

   if (!e_comp_object_hwc_update_exists(ec->frame)) return NULL;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Display Plane(%p) zpos(%d)   Client ec(%p, %s)",
           ec->pixmap, ec, plane, plane->zpos, ec, e_client_util_name_get(ec));

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

static tbm_surface_h
_e_plane_cursor_surface_acquire(E_Plane *plane)
{
   tbm_surface_h tsurface = NULL;
   E_Plane_Renderer *renderer = plane->renderer;
   E_Client *ec = plane->ec;
   E_Comp_Wl_Buffer *buffer = NULL;

   buffer = ec->comp_data->buffer_ref.buffer;
   if (!buffer) return NULL;

   if (!e_comp_object_hwc_update_exists(ec->frame)) return NULL;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Display Cursor Plane(%p) zpos(%d)   ec(%p)",
           NULL, ec, plane, plane->zpos, ec);

   e_comp_object_hwc_update_set(ec->frame, EINA_FALSE);

   tsurface = e_plane_renderer_cursor_surface_get(renderer);

   if (plane->display_info.buffer_ref.buffer != buffer || !tsurface)
     {
        if (!e_plane_renderer_cursor_surface_refresh(renderer, ec))
          {
             ERR("Failed to e_plane_renderer_cursor_surface_refresh");
             return NULL;
          }
     }

   tsurface = e_plane_renderer_cursor_surface_get(renderer);

   return tsurface;
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
             WRN("fail to _get_tbm_surface_queue");
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
          ELOGF("E_PLANE", "Display Plane(%p) zpos(%d)   Canvas",
                NULL, NULL, plane, plane->zpos);
     }

   return tsurface;
}

static void
_e_plane_surface_send_dequeuable_surfaces(E_Plane *plane)
{
   tbm_surface_h tsurface = NULL;
   E_Plane_Renderer *renderer = plane->renderer;

   if (!e_plane_renderer_ec_valid_check(renderer, renderer->ec)) return;

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

static void
_e_plane_unset_reset(E_Plane *plane)
{
   Eina_Bool print_log = EINA_FALSE;

   /* reset the unset plane flags */
   if (plane->unset_candidate) {plane->unset_candidate = EINA_FALSE; print_log = EINA_TRUE;}
   if (plane->unset_counter > 0) {plane->unset_counter = 0; print_log = EINA_TRUE;}
   if (plane->unset_try) {plane->unset_try = EINA_FALSE; print_log = EINA_TRUE;}
   if (plane->unset_commit) {plane->unset_commit = EINA_FALSE; print_log = EINA_TRUE;}

   if (print_log && plane_trace_debug)
      ELOGF("E_PLANE", " Plane(%p) Unset flags Reset", NULL, NULL, plane);
}

static void
_e_plane_unset_candidate_set(E_Plane *plane)
{
   E_Plane *fb_target = NULL;

   fb_target = e_output_fb_target_get(plane->output);
   if (fb_target)
     {
        if(fb_target->ec)
          plane->unset_counter = 0;
        else
          plane->unset_counter = e_plane_renderer_render_count_get(fb_target->renderer) + 1;
     }

   plane->unset_candidate = EINA_TRUE;
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
   plane->activation = EINA_TRUE;

   tdm_err = tdm_layer_get_buffer_flags(plane->tlayer, &buffer_flags);
   if (tdm_err == TDM_ERROR_NONE)
     plane->buffer_flags = buffer_flags;

   /* check the layer is the primary layer */
   if (layer_capabilities & TDM_LAYER_CAPABILITY_PRIMARY)
     {
        plane->is_primary = EINA_TRUE;
        plane->is_fb = EINA_TRUE; // TODO: query from libtdm if it is fb target plane
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
   if (plane->renderer) _e_plane_renderer_unset(plane);
   if (plane->ec) e_plane_ec_set(plane, NULL);

   free(plane);
}

EINTERN Eina_Bool
e_plane_setup(E_Plane *plane)
{
   const char *name;
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);
   E_Plane_Renderer *renderer = NULL;

   /* we assume that the primary plane gets a ecore_evas */
   if (!plane->is_fb) return EINA_FALSE;

   name = ecore_evas_engine_name_get(e_comp->ee);
   if (!strcmp("gl_drm", name))
     {
        Evas_Engine_Info_GL_Drm *einfo = NULL;
        /* get the evas_engine_gl_drm information */
        einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
        if (!einfo)
          {
             ERR("fail to get the GL_Drm einfo.");
             goto hwc_setup_fail;
          }
        /* enable hwc to evas engine gl_drm */
        einfo->info.hwc_enable = EINA_TRUE;
        ecore_evas_manual_render_set(e_comp->ee, 1);
     }
   else if(!strcmp("gl_drm_tbm", name))
     {
        ecore_evas_manual_render_set(e_comp->ee, 1);
     }
   else if(!strcmp("drm_tbm", name))
     {
        ecore_evas_manual_render_set(e_comp->ee, 1);
     }

   renderer = e_plane_renderer_new(plane);
   if (!renderer)
     {
        ERR("fail to e_plane_renderer_new");
        free(plane);
        return EINA_FALSE;
     }
   plane->renderer = renderer;

   return EINA_TRUE;

hwc_setup_fail:
   ecore_evas_manual_render_set(e_comp->ee, 0);

   if (plane->renderer)
     {
        e_plane_renderer_del(plane->renderer);
        plane->renderer = NULL;
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

   if (e_comp_canvas_norender_get() > 0)
     {
        if (plane_trace_debug)
          ELOGF("E_PLANE_RENDERER", "Canvas norender is set. No Display.", NULL, NULL);

        return EINA_FALSE;
     }

   if (plane->wait_commit)
      return EINA_FALSE;

   if (plane->is_fb && !plane->ec)
     {
        /* acquire the surface */
        tsurface = _e_plane_surface_from_ecore_evas_acquire(plane);
     }
   else
     {
        if (plane->ec)
          {
             if (plane->role == E_PLANE_ROLE_OVERLAY)
               {
                  /* acquire the surface */
                  if (plane->reserved_memory)
                    tsurface = _e_plane_surface_from_client_acquire_reserved(plane);
                  else
                    tsurface = _e_plane_surface_from_client_acquire(plane);
               }
             else if (plane->role == E_PLANE_ROLE_CURSOR)
               {
                  tsurface = _e_plane_cursor_surface_acquire(plane);
               }
             else
               {
                  ERR("not supported plane:%p role:%d", plane, plane->role);
                  return EINA_FALSE;
               }

             /* For send frame::done to client */
             if (!tsurface)
                e_pixmap_image_clear(plane->ec->pixmap, 1);
          }
     }

   /* exist tsurface for update plane */
   if (tsurface)
     {
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
     }
   else
     {
        if (e_plane_is_unset_try(plane))
          {
             if (eina_list_count(plane->commit_data_list))
               return EINA_FALSE;

              plane->tsurface = NULL;

              /* set plane info and set tsurface to the plane */
              if (!_e_plane_surface_unset(plane))
                {
                   ERR("failed to unset surface plane:%p", plane);
                   return EINA_FALSE;
                }
          }
        else
          {
             return EINA_FALSE;
          }
     }

   return EINA_TRUE;
}

EINTERN void
e_plane_unfetch(E_Plane *plane)
{
   tbm_surface_h displaying_tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN(plane);
   EINA_SAFETY_ON_NULL_RETURN(plane->tsurface);

   /* do not reset the plane when the plan is trying to unset */
   if (e_plane_is_unset_try(plane)) return;

   if (plane->is_fb && !plane->ec)
     {
        e_plane_renderer_surface_queue_release(plane->renderer, plane->tsurface);
     }
   else
     {
        if (!plane->ec) return;
        if (plane->reserved_memory)
          e_plane_renderer_surface_queue_release(plane->renderer, plane->tsurface);
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

static void
_e_plane_vblank_handler(tdm_output *output, unsigned int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec,
                                  void *user_data)
{
   E_Plane *plane = (E_Plane *)user_data;

   EINA_SAFETY_ON_NULL_RETURN(plane);

   plane->wait_commit = EINA_FALSE;
}

static void
_e_plane_commit_hanler(tdm_layer *layer, unsigned int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec,
                                  void *user_data)
{
   E_Plane_Commit_Data *data = (E_Plane_Commit_Data *)user_data;

   EINA_SAFETY_ON_NULL_RETURN(data);

   TRACE_DS_ASYNC_END((unsigned int)layer, [PLANE:COMMIT~HANDLER]);

   e_plane_commit_data_release(data);
}

static void
_e_plane_zoom_destroy(E_Plane *plane)
{
   if (plane->zoom_tqueue)
     {
        tbm_surface_queue_destroy(plane->zoom_tqueue);
        plane->zoom_tqueue = NULL;
     }
   if (plane->tpp)
     {
        tdm_pp_destroy(plane->tpp);
        plane->tpp = NULL;
     }

   ERR("_e_plane_zoom_destroy done");
}

EINTERN Eina_Bool
e_plane_commit(E_Plane *plane)
{
   E_Plane_Commit_Data *data = NULL;
   tdm_error error = TDM_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   data = e_plane_commit_data_aquire(plane);

   if (!data) return EINA_TRUE;

   TRACE_DS_ASYNC_BEGIN((unsigned int)plane->tlayer, [PLANE:COMMIT~HANDLER]);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Commit  Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
           NULL, NULL, plane, plane->zpos, data->tsurface, plane->renderer ? plane->renderer->tqueue : NULL,
           data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);

   if (plane->activation)
     {
        error = tdm_layer_commit(plane->tlayer, _e_plane_commit_hanler, data);
        if (error != TDM_ERROR_NONE)
          {
             ERR("fail to tdm_layer_commit plane:%p, zpos:%d", plane, plane->zpos);
             e_plane_commit_data_release(data);
             return EINA_FALSE;
          }

        error = tdm_output_wait_vblank(plane->output->toutput, 1, 0, _e_plane_vblank_handler, (void *)plane);
        if (error != TDM_ERROR_NONE)
          {
             ERR("fail to tdm_output_wait_vblank plane:%p, zpos:%d", plane, plane->zpos);
             return EINA_FALSE;
          }

        /* send frame event enlightenment dosen't send frame evnet in nocomp */
        if (plane->ec)
           e_pixmap_image_clear(plane->ec->pixmap, 1);

        if (plane->zoom_unset)
          {
             if (e_plane_is_fb_target(plane))
               {
                  if (plane->zoom_tsurface)
                    {
                       tbm_surface_queue_release(plane->zoom_tqueue, plane->zoom_tsurface);
                       tbm_surface_internal_unref(plane->zoom_tsurface);

                       plane->zoom_tsurface = NULL;
                    }

                  if ((eina_list_count(plane->pending_pp_zoom_data_list) == 0) &&
                      (eina_list_count(plane->zoom_data_list) == 0))
                    {
                       _e_plane_zoom_destroy(plane);
                       plane->zoom_unset = EINA_FALSE;
                    }
               }
          }
     }

   plane->wait_commit = EINA_TRUE;

   return EINA_TRUE;
}

EINTERN E_Plane_Commit_Data *
e_plane_commit_data_aquire(E_Plane *plane)
{
   E_Plane_Commit_Data *data = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   if (plane->unset_commit)
     {
        data = E_NEW(E_Plane_Commit_Data, 1);
        data->plane = plane;
        data->renderer = NULL;
        data->tsurface = NULL;
        data->ec = NULL;

        /* reset to be the initail unset values */
        _e_plane_unset_reset(plane);
     }
   else
     {
        if (!plane->renderer) return NULL;

        /* check update_exist */
        if (!e_plane_renderer_update_exist_check(plane->renderer))
          return NULL;

        if (plane->ec)
          {
             data = E_NEW(E_Plane_Commit_Data, 1);
             data->plane = plane;
             data->renderer = plane->renderer;
             data->tsurface = plane->tsurface;
             tbm_surface_internal_ref(data->tsurface);
             data->ec = plane->ec;

             /* set the update_exist to be false */
             e_plane_renderer_update_exist_set(plane->renderer, EINA_FALSE);

             e_comp_wl_buffer_reference(&data->buffer_ref, _get_comp_wl_buffer(plane->ec));
          }
        else
          {
             if (plane->is_fb)
               {
                  data = E_NEW(E_Plane_Commit_Data, 1);
                  data->plane = plane;
                  data->renderer = plane->renderer;
                  data->tsurface = plane->tsurface;
                  tbm_surface_internal_ref(data->tsurface);
                  data->ec = NULL;

                  /* set the update_exist to be false */
                  e_plane_renderer_update_exist_set(plane->renderer, EINA_FALSE);
               }
          }
     }

   if (data)
     plane->commit_data_list = eina_list_append(plane->commit_data_list, data);

   return data;
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
   renderer = data->renderer;
   tsurface = data->tsurface;
   ec = data->ec;

   /* update renderer info */
   if (renderer)
     {
        displaying_tsurface = e_plane_renderer_displaying_surface_get(renderer);
        e_plane_renderer_displaying_surface_set(renderer, tsurface);
        e_plane_renderer_previous_surface_set(renderer, displaying_tsurface);
     }

   if (!tsurface)
     {
        if (plane_trace_debug)
          ELOGF("E_PLANE", "Done    Plane(%p) zpos(%d)   data(%p)::Unset", NULL, NULL, plane, plane->zpos, data);

        e_comp_wl_buffer_reference(&plane->display_info.buffer_ref, NULL);
     }
   else if (!ec)
     {
        /* composite */
        /* debug */
        if (plane_trace_debug)
          ELOGF("E_PLANE", "Done    Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)::Canvas",
                NULL, NULL, plane, plane->zpos, tsurface, renderer ? renderer->tqueue : NULL,
                data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);

        e_comp_wl_buffer_reference(&plane->display_info.buffer_ref, NULL);
     }
   else
     {
        /* no composite */
        /* debug */
        if (plane_trace_debug)
          ELOGF("E_PLANE", "Done    Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)::Client",
                ec->pixmap, ec, plane, plane->zpos, tsurface, (renderer ? renderer->tqueue : NULL),
                data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);

        e_comp_wl_buffer_reference(&plane->display_info.buffer_ref, data->buffer_ref.buffer);
     }

    e_comp_wl_buffer_reference(&data->buffer_ref, NULL);

    if (plane->role != E_PLANE_ROLE_CURSOR)
      {
        if (plane->display_info.renderer && plane->display_info.tsurface)
          {
             if (plane->reserved_memory)
               {
                  if (plane->ec)
                    {
                       e_plane_renderer_surface_queue_release(plane->display_info.renderer, plane->display_info.tsurface);
                       _e_plane_surface_send_dequeuable_surfaces(plane);
                    }
                  else
                    {
                       e_plane_renderer_sent_surface_recevie(plane->display_info.renderer, plane->display_info.tsurface);
                       e_plane_renderer_surface_queue_release(plane->display_info.renderer, plane->display_info.tsurface);
                    }
               }
             else
               {
                  if (!plane->display_info.ec)
                    e_plane_renderer_surface_queue_release(plane->display_info.renderer, plane->display_info.tsurface);
               }
          }
      }

    /* update plane display info */
    plane->display_info.renderer = renderer;
    plane->display_info.ec = ec;

    if (plane->display_info.tsurface)
      {
         tbm_surface_internal_unref(plane->display_info.tsurface);
         plane->display_info.tsurface = NULL;
      }

    if (tsurface)
      {
         tbm_surface_internal_ref(tsurface);
         plane->display_info.tsurface = tsurface;
         tbm_surface_internal_unref(tsurface);
      }

   plane->commit_data_list = eina_list_remove(plane->commit_data_list, data);
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
                  _e_plane_renderer_unset(plane);
                  e_plane_role_set(plane, E_PLANE_ROLE_NONE);
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

EINTERN Eina_Bool
e_plane_is_unset_candidate(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   return plane->unset_candidate;
}

EINTERN Eina_Bool
e_plane_is_unset_try(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   return plane->unset_try;
}

EINTERN void
e_plane_unset_try_set(E_Plane *plane, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(plane);

   if (plane->unset_try == set) return;

   if (set)
     {
        if (!e_plane_is_unset_candidate(plane))
          {
             WRN("Plane is not unset_candidate.");
             return;
          }

       plane->unset_candidate = EINA_FALSE;
       plane->unset_try = EINA_TRUE;

       if (plane_trace_debug)
         ELOGF("E_PLANE", "Plane(%p) Set unset_try. unset_counter(%d)", NULL, NULL, plane, plane->unset_counter);
     }
   else
     {
        plane->unset_commit = EINA_TRUE;
        plane->unset_try = EINA_FALSE;

       if (plane_trace_debug)
         ELOGF("E_PLANE", "Plane(%p) UnSet unset_try. unset_counter(%d)", NULL, NULL, plane, plane->unset_counter);
     }
}

EINTERN Eina_Bool
e_plane_unset_commit_check(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (!e_plane_is_unset_try(plane))
     {
        WRN("Plane is not unset_try.");
        return EINA_FALSE;
     }

   plane->unset_counter--;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Plane(%p) Check unset_commit. unset_counter(%d)", NULL, NULL, plane, plane->unset_counter);

   if (plane->unset_counter > 0) return EINA_FALSE;

   return EINA_TRUE;
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
      ELOGF("E_PLANE", "Request Plane(%p) zpos(%d)   Set ec(%p, %s)",
            (ec ? ec->pixmap : NULL), ec, plane, plane->zpos, ec, e_client_util_name_get(ec));

   if (ec)
     {
        if (plane->ec == ec) return EINA_TRUE;

        if (plane->ec_redirected)
          {
             if (plane->ec) e_client_redirected_set(plane->ec, EINA_TRUE);
             plane->ec_redirected = EINA_FALSE;
          }

        if (plane->reserved_memory)
          e_plane_reserved_set(plane, EINA_TRUE);

        if (e_policy_client_is_cursor(ec))
          {
             if ((plane->renderer) && (plane->role != E_PLANE_ROLE_CURSOR))
               {
                  e_plane_renderer_del(plane->renderer);
                  plane->renderer = NULL;
               }

             if (!plane->renderer)
               plane->renderer = e_plane_renderer_new(plane);

             EINA_SAFETY_ON_NULL_RETURN_VAL(plane->renderer, EINA_FALSE);

             e_plane_role_set(plane, E_PLANE_ROLE_CURSOR);

             if (!e_plane_renderer_cursor_ec_set(plane->renderer, ec))
               {
                  plane->ec = NULL;
                  return EINA_FALSE;
               }
          }
        else
          {
             if (!plane->is_fb)
               {
                  if ((plane->renderer) && (plane->role != E_PLANE_ROLE_OVERLAY))
                    _e_plane_renderer_unset(plane);

                  if (!plane->renderer)
                    plane->renderer = e_plane_renderer_new(plane);
               }

             EINA_SAFETY_ON_NULL_RETURN_VAL(plane->renderer, EINA_FALSE);

             e_plane_role_set(plane, E_PLANE_ROLE_OVERLAY);

             if (!e_plane_renderer_ec_set(plane->renderer, ec))
               {
                  plane->ec = NULL;
                  return EINA_FALSE;
               }

             if (plane->reserved_memory)
               _e_plane_surface_send_dequeuable_surfaces(plane);
          }

        if (!plane->is_fb) _e_plane_unset_reset(plane);

        e_comp_object_hwc_update_set(ec->frame, EINA_TRUE);

        if (ec->redirected) plane->ec_redirected = EINA_TRUE;
        e_client_redirected_set(ec, EINA_FALSE);
     }
   else
     {
        /* To set null to a plane means two things.
           1. if the plane is fb target, the plane uses the ecore_evas.
           2. if the plane is not fb target, the plane needs to unset
              at the time that the result of the ecore_evas renderer(compositing)
              is finished with the tsurface(ec) of the plane. For this,
              we set the unset_candidate flags to the plane and measure to unset
              the plane at the e_output_commit.
         */
        if (plane->is_fb)
          {
             if (!e_plane_renderer_ecore_evas_use(plane->renderer))
               {
                  ERR("failed to use ecore_evas plane:%p", plane);
                  return EINA_FALSE;
               }
          }
        else
          {
             if (plane->tsurface)
               {
                  _e_plane_unset_candidate_set(plane);
                  if (plane_trace_debug)
                    ELOGF("E_PLANE", "Plane(%p) Set the unset_candidate", (plane->ec ? ec->pixmap : NULL), ec, plane);
               }

             if (plane->renderer)
               {
                  _e_plane_renderer_unset(plane);
                  e_plane_role_set(plane, E_PLANE_ROLE_NONE);
               }
          }

        if (plane->ec_redirected)
          {
             if (plane->ec) e_client_redirected_set(plane->ec, EINA_TRUE);
             plane->ec_redirected = EINA_FALSE;
          }
     }

   plane->ec = ec;
   plane->need_ev = EINA_TRUE;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Plane(%p) zpos(%d)   Set ec(%p, %s)",
           (ec ? ec->pixmap : NULL), ec, plane, plane->zpos, ec, e_client_util_name_get(ec));

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
   E_Plane_Type type;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   type = plane->type;

   if (ec)
     {
        // check surface is set on capable layer
        if (e_policy_client_is_cursor(ec))
          {
             if (type != E_PLANE_TYPE_CURSOR && type != E_PLANE_TYPE_GRAPHIC)
               return EINA_FALSE;
          }
        else
          {
             if (type != E_PLANE_TYPE_GRAPHIC)
               return EINA_FALSE;
          }
     }

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

EINTERN void
e_plane_show_state(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN(plane);

   ELOGF("E_PLANE", "Plane(%p) zpos(%d) ec(%p) display tsurface(%p)",
         NULL, NULL, plane, plane->zpos, plane->ec, plane->display_info.tsurface);

   if (plane->renderer)
     e_plane_renderer_show_state(plane->renderer);
}

EINTERN void
e_plane_activation_set(E_Plane *plane, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(plane);

   if (plane->activation == set) return;

   plane->activation = set;

   ELOGF("E_PLANE", "Plane(%p) activation set to be %s",
         NULL, NULL, plane, set?"True":"False");
}

static Eina_Bool
_e_plane_tdm_info_changed_check(E_Plane *plane, unsigned int size_w, unsigned int size_h,
                              unsigned int src_x, unsigned int src_y, unsigned int src_w, unsigned int src_h,
                              unsigned int dst_x, unsigned int dst_y, unsigned int dst_w, unsigned int dst_h,
                              tdm_transform transform)
{
   if (plane->info.src_config.size.h != size_w ||
       plane->info.src_config.size.v != size_h ||
       plane->info.src_config.pos.x != src_x ||
       plane->info.src_config.pos.y != src_y ||
       plane->info.src_config.pos.w != src_w ||
       plane->info.src_config.pos.h != src_h ||
       plane->info.dst_pos.x != dst_x ||
       plane->info.dst_pos.y != dst_y ||
       plane->info.dst_pos.w != dst_w ||
       plane->info.dst_pos.h != dst_h ||
       plane->info.transform != transform)
     return EINA_TRUE;

   return EINA_FALSE;
}

static void
_e_plane_tdm_info_set(E_Plane *plane, unsigned int size_w, unsigned int size_h,
                          unsigned int src_x, unsigned int src_y, unsigned int src_w, unsigned int src_h,
                          unsigned int dst_x, unsigned int dst_y, unsigned int dst_w, unsigned int dst_h,
                          tdm_transform transform)
{
   plane->info.src_config.size.h = size_w;
   plane->info.src_config.size.v = size_h;
   plane->info.src_config.pos.x = src_x;
   plane->info.src_config.pos.y = src_y;
   plane->info.src_config.pos.w = src_w;
   plane->info.src_config.pos.h = src_h;
   plane->info.dst_pos.x = dst_x;
   plane->info.dst_pos.y = dst_y;
   plane->info.dst_pos.w = dst_w;
   plane->info.dst_pos.h = dst_h;
   plane->info.transform = transform;
}

static Eina_Bool
_e_plane_zoom_set_pp_info(E_Plane *plane)
{
   tdm_info_pp pp_info;
   tdm_error ret = TDM_ERROR_NONE;
   unsigned int aligned_width = 0;
   tbm_surface_info_s surf_info;
   tbm_surface_h tsurface = plane->tsurface;
   int dst_w, dst_h;

   tbm_surface_get_info(tsurface, &surf_info);

   aligned_width = _e_plane_aligned_width_get(tsurface);
   if (aligned_width == 0) return EINA_FALSE;

   e_output_size_get(plane->output, &dst_w, &dst_h);

   pp_info.src_config.size.h = aligned_width;
   pp_info.src_config.size.v = surf_info.height;
   pp_info.src_config.pos.x = plane->zoom_rect_temp.x;
   pp_info.src_config.pos.y = plane->zoom_rect_temp.y;
   pp_info.src_config.pos.w = plane->zoom_rect_temp.w;
   pp_info.src_config.pos.h = plane->zoom_rect_temp.h;
   pp_info.src_config.format = TBM_FORMAT_ARGB8888;

   pp_info.dst_config.size.h = aligned_width;
   pp_info.dst_config.size.v = surf_info.height;
   pp_info.dst_config.pos.x = 0;
   pp_info.dst_config.pos.y = 0;
   pp_info.dst_config.pos.w = dst_w;
   pp_info.dst_config.pos.h = dst_h;
   pp_info.dst_config.format = TBM_FORMAT_ARGB8888;

   pp_info.transform = TDM_TRANSFORM_NORMAL;
   pp_info.sync = 0;
   pp_info.flags = 0;

   ret = tdm_pp_set_info(plane->tpp, &pp_info);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   plane->zoom_rect.x = plane->zoom_rect_temp.x;
   plane->zoom_rect.y = plane->zoom_rect_temp.y;
   plane->zoom_rect.w = plane->zoom_rect_temp.w;
   plane->zoom_rect.h = plane->zoom_rect_temp.h;

   DBG("_e_plane_zoom_set_pp_info success(src x:%d,y:%d,w:%d,h:%d)",
       plane->zoom_rect.x, plane->zoom_rect.y, plane->zoom_rect.w, plane->zoom_rect.h);

   return EINA_TRUE;
}

static void
_e_plane_zoom_commit_data_release(E_Plane_Pp_Data *data)
{
   E_Plane *plane = NULL;
   tbm_surface_h zoom_tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN(data);

   plane = data->plane;
   zoom_tsurface = data->zoom_tsurface;

   if (plane->zoom_tsurface)
     {
        tbm_surface_queue_release(plane->zoom_tqueue, plane->zoom_tsurface);

        tbm_surface_internal_unref(plane->zoom_tsurface);
     }
   plane->zoom_tsurface = zoom_tsurface;

   plane->zoom_data_list = eina_list_remove(plane->zoom_data_list, data);

   free(data);

   if (eina_list_count(plane->pending_pp_zoom_data_list) != 0)
     _e_plane_zoom_pending_data_pp(plane);
}

static void
_e_plane_zoom_commit_handler(tdm_layer *layer, unsigned int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec,
                                  void *user_data)
{
   E_Plane_Pp_Data *data = (E_Plane_Pp_Data *)user_data;

   EINA_SAFETY_ON_NULL_RETURN(data);

   TRACE_DS_ASYNC_END((unsigned int)layer, [PLANE:COMMIT~HANDLER]);

   _e_plane_zoom_commit_data_release(data);
}

static void
_e_plane_zoom_pp_cb(tdm_pp *pp, tbm_surface_h tsurface_src, tbm_surface_h tsurface_dst, void *user_data)
{
   E_Plane_Pp_Data *pp_data = NULL;
   E_Plane_Commit_Data *data = NULL;
   E_Plane *plane = NULL;
   tbm_surface_info_s surf_info;
   tbm_surface_h zoom_tsurface;
   tbm_error_e tbm_err;
   tdm_error tdm_err;
   tdm_layer *tlayer = NULL;
   E_Output *output = NULL;
   unsigned int aligned_width;

   pp_data = (E_Plane_Pp_Data *)user_data;
   if (!pp_data)
     {
        ERR("_e_plane_zoom_pp_cb: fail no pp_data");
        return;
     }

   data = pp_data->data;
   if (!data)
     ERR("_e_plane_zoom_pp_cb: fail no data");

   plane = pp_data->plane;
   if (!plane)
     {
        ERR("_e_plane_zoom_pp_cb: fail no plane");
        return;
     }

   if (data)
     {
        if (data->ec)
          e_pixmap_image_clear(data->ec->pixmap, 1);
        e_plane_commit_data_release(data);
        pp_data->data = NULL;
     }

   plane->pending_commit = EINA_FALSE;
   if (plane->zoom_unset)
     {
        tbm_surface_queue_release(plane->zoom_tqueue, tsurface_dst);
        tbm_surface_internal_unref(tsurface_dst);

        plane->zoom_data_list = eina_list_remove(plane->zoom_data_list, pp_data);

        free(pp_data);

        return;
     }

   tlayer = plane->tlayer;
   output = plane->output;

   /* set layer when the layer infomation is different from the previous one */
   tbm_surface_get_info(tsurface_dst, &surf_info);

   aligned_width = _e_plane_aligned_width_get(tsurface_dst);
   if (aligned_width == 0)
     {
        ERR("_e_plane_zoom_pp_cb: aligned_width 0");
        _e_plane_zoom_commit_data_release(pp_data);
        return;
     }

   if (_e_plane_tdm_info_changed_check(plane, aligned_width, surf_info.height, 0, 0, surf_info.width, surf_info.height,
                                       output->config.geom.x, output->config.geom.y, output->config.geom.w, output->config.geom.h,
                                       TDM_TRANSFORM_NORMAL))
     {
        _e_plane_tdm_info_set(plane, aligned_width, surf_info.height, 0, 0, surf_info.width, surf_info.height,
                              output->config.geom.x, output->config.geom.y, output->config.geom.w, output->config.geom.h,
                              TDM_TRANSFORM_NORMAL);

        tdm_err = tdm_layer_set_info(tlayer, &plane->info);
        if (tdm_err != TDM_ERROR_NONE)
          {
             ERR("_e_plane_zoom_pp_cb: fail tdm_layer_set_info");
             _e_plane_zoom_commit_data_release(pp_data);
             return;
          }
     }

   if (plane_trace_debug)
     {
        ELOGF("E_PLANE", "Set(zoom)  Plane(%p)     tsurface(%p) (%dx%d,[%d,%d,%d,%d]=>[%d,%d,%d,%d])",
              NULL, NULL, plane, tsurface_dst,
              plane->info.src_config.size.h, plane->info.src_config.size.v,
              plane->info.src_config.pos.x, plane->info.src_config.pos.y,
              plane->info.src_config.pos.w, plane->info.src_config.pos.h,
              plane->info.dst_pos.x, plane->info.dst_pos.y,
              plane->info.dst_pos.w, plane->info.dst_pos.h);
     }

   tbm_err = tbm_surface_queue_enqueue(plane->zoom_tqueue, tsurface_dst);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("_e_plane_zoom_pp_cb : fail tbm_surface_queue_enqueue");
        _e_plane_zoom_commit_data_release(pp_data);
        return;
     }

   tbm_err = tbm_surface_queue_acquire(plane->zoom_tqueue, &zoom_tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("_e_plane_zoom_pp_cb : fail tbm_surface_queue_acquire");
        _e_plane_zoom_commit_data_release(pp_data);
        return;
     }

   tdm_err = tdm_layer_set_buffer(tlayer, zoom_tsurface);
   if (tdm_err != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_set_buffer");
        _e_plane_zoom_commit_data_release(pp_data);
        return;
     }

   pp_data->zoom_tsurface = zoom_tsurface;
   pp_data->plane = plane;

   tdm_err = tdm_layer_commit(tlayer, _e_plane_zoom_commit_handler, pp_data);
   if (tdm_err != TDM_ERROR_NONE)
     {
        ERR("_e_plane_zoom_pp_cb: fail to tdm_layer_commit plane:%p, zpos:%d", plane, plane->zpos);
        _e_plane_zoom_commit_data_release(pp_data);
        return;
     }

   return;
}

static void
_e_plane_zoom_pending_data_pp(E_Plane *plane)
{
   tdm_info_pp pp_info;
   tdm_error ret = TDM_ERROR_NONE;
   tbm_surface_h tsurface = plane->tsurface;
   tbm_surface_info_s surf_info;
   int dst_w, dst_h;
   E_Plane_Commit_Data *data = NULL;
   E_Plane_Pp_Data *pp_data = NULL;
   tbm_error_e tbm_err = TBM_ERROR_NONE;
   tbm_surface_h zoom_tsurface = NULL;
   tdm_error tdm_err = TDM_ERROR_NONE;
   Eina_List *l, *ll;

   EINA_SAFETY_ON_NULL_RETURN(plane);

   if (plane->zoom_unset)
     {
        EINA_LIST_FOREACH_SAFE(plane->pending_pp_zoom_data_list, l, ll, data)
          {
             if (!data) continue;

             plane->pending_pp_zoom_data_list = eina_list_remove_list(plane->pending_pp_zoom_data_list, l);

             if (data->ec)
               e_pixmap_image_clear(data->ec->pixmap, 1);
             e_plane_commit_data_release(data);
          }

        return;
     }

   data = eina_list_nth(plane->pending_pp_zoom_data_list, 0);
   if (!data)
     {
        ERR("_e_plane_zoom_pending_data_pp: fail E_Plane_Commit_Data get");
        return;
     }

   tbm_err = tbm_surface_queue_dequeue(plane->zoom_tqueue, &zoom_tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("_e_plane_zoom_pending_data_pp: fail tbm_surface_queue_dequeue");
        return;
     }
   tbm_surface_internal_ref(zoom_tsurface);

   plane->pending_pp_zoom_data_list = eina_list_remove(plane->pending_pp_zoom_data_list, data);

   pp_data = E_NEW(E_Plane_Pp_Data, 1);
   pp_data->zoom_tsurface = zoom_tsurface;
   pp_data->data = data;
   pp_data->plane = plane;

   plane->zoom_data_list = eina_list_append(plane->zoom_data_list, pp_data);

   if (plane->zoom_rect.x != plane->zoom_rect_temp.x || plane->zoom_rect.y != plane->zoom_rect_temp.y ||
       plane->zoom_rect.w != plane->zoom_rect_temp.w || plane->zoom_rect.h != plane->zoom_rect_temp.h)
     {
        if (!_e_plane_zoom_set_pp_info(plane))
          {
             ERR("_e_plane_zoom_pending_data_pp: fail _e_plane_zoom_set_pp_info");
             goto pp_fail;
          }
     }

   tdm_err = tdm_pp_set_done_handler(plane->tpp, _e_plane_zoom_pp_cb, pp_data);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, pp_fail);

   tdm_err = tdm_pp_attach(plane->tpp, tsurface, zoom_tsurface);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, pp_fail);

   tdm_err = tdm_pp_commit(plane->tpp);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, pp_fail);

   return;

pp_fail:
   plane->zoom_data_list = eina_list_remove(plane->zoom_data_list, pp_data);

   if (data)
     e_plane_commit_data_release(data);

   if (pp_data)
     free(pp_data);

   ERR("_e_plane_zoom_pending_data_pp: fail");

   return;
}

static Eina_Bool
_e_plane_zoom_find_data(E_Plane *plane, E_Plane_Commit_Data *data)
{
   Eina_List *l = NULL;
   E_Plane_Pp_Data *pp_data = NULL;

   EINA_LIST_FOREACH(plane->zoom_data_list, l, pp_data)
     {
        if (!pp_data) continue;
        if (!pp_data->data) continue;
        if (pp_data->data == data) return EINA_TRUE;
     }

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_plane_zoom_commit(E_Plane *plane)
{
   tbm_surface_h tsurface = plane->tsurface;
   E_Plane_Commit_Data *data = NULL;
   E_Plane_Pp_Data *pp_data = NULL;
   int count = 0;
   tbm_error_e tbm_err = TBM_ERROR_NONE;
   tbm_surface_h zoom_tsurface = NULL;
   tdm_error tdm_err = TDM_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   count = eina_list_count(plane->pending_commit_data_list);
   if (count == 0) return EINA_TRUE;

   data = eina_list_nth(plane->pending_commit_data_list, count - 1);
   if (!data)
     {
        ERR("e_plane_zoom_commit: fail E_Plane_Commit_Data get");
        return EINA_FALSE;
     }
   if (_e_plane_zoom_find_data(plane, data))
     {
         ERR("e_plane_zoom_commit: _e_plane_zoom_find_data return");
        return EINA_TRUE;
     }

   if (!tbm_surface_queue_can_dequeue(plane->zoom_tqueue, 0))
     {
        plane->pending_pp_zoom_data_list = eina_list_append(plane->pending_pp_zoom_data_list, data);
        return EINA_TRUE;
     }

   if (eina_list_count(plane->pending_pp_zoom_data_list) != 0)
     {
        plane->pending_pp_zoom_data_list = eina_list_append(plane->pending_pp_zoom_data_list, data);
        return EINA_TRUE;
     }

   tbm_err = tbm_surface_queue_dequeue(plane->zoom_tqueue, &zoom_tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("e_plane_zoom_commit: fail tbm_surface_queue_dequeue");
        return EINA_FALSE;
     }
   tbm_surface_internal_ref(zoom_tsurface);

   pp_data = E_NEW(E_Plane_Pp_Data, 1);
   pp_data->zoom_tsurface = zoom_tsurface;
   pp_data->data = data;
   pp_data->plane = plane;

   plane->zoom_data_list = eina_list_append(plane->zoom_data_list, pp_data);

   if (plane->zoom_rect.x != plane->zoom_rect_temp.x || plane->zoom_rect.y != plane->zoom_rect_temp.y ||
       plane->zoom_rect.w != plane->zoom_rect_temp.w || plane->zoom_rect.h != plane->zoom_rect_temp.h)
     {
        if (!_e_plane_zoom_set_pp_info(plane))
          {
             ERR("e_plane_zoom_commit: fail _e_plane_zoom_set_pp_info");
             goto pp_fail;
          }
     }

   tdm_err = tdm_pp_set_done_handler(plane->tpp, _e_plane_zoom_pp_cb, pp_data);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, pp_fail);

   tdm_err = tdm_pp_attach(plane->tpp, tsurface, zoom_tsurface);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, pp_fail);

   tdm_err = tdm_pp_commit(plane->tpp);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, pp_fail);

   return EINA_TRUE;

pp_fail:
   plane->zoom_data_list = eina_list_remove(plane->zoom_data_list, pp_data);

   if (data)
     e_plane_commit_data_release(data);

   if (pp_data)
     free(pp_data);

   ERR("e_plane_zoom_commit: fail");

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_plane_zoom_set(E_Plane *plane, Eina_Rectangle *rect)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   E_Comp_Screen *e_comp_screen = NULL;
   tdm_error ret = TDM_ERROR_NONE;
   int w, h;

   if ((plane->zoom_rect_temp.x == rect->x) && (plane->zoom_rect_temp.y == rect->y) &&
       (plane->zoom_rect_temp.w == rect->w) && (plane->zoom_rect_temp.h == rect->h))
     return EINA_TRUE;

   if (plane->zoom_unset) plane->zoom_unset = EINA_FALSE;

   e_comp_screen = e_comp->e_comp_screen;
   e_output_size_get(plane->output, &w, &h);

   if (!plane->tpp)
     {
        plane->tpp = tdm_display_create_pp(e_comp_screen->tdisplay, &ret);
        if (ret != TDM_ERROR_NONE)
          {
             ERR("fail tdm pp create");
             goto fail;
          }
     }

   if (!plane->zoom_tqueue)
     {
        plane->zoom_tqueue = tbm_surface_queue_create(3, w, h, TBM_FORMAT_ARGB8888, plane->buffer_flags | TBM_BO_SCANOUT);
        if (!plane->zoom_tqueue)
          {
             ERR("fail tbm_surface_queue_create");
             goto fail;
          }
     }

   plane->zoom_rect_temp.x = rect->x;
   plane->zoom_rect_temp.y = rect->y;
   plane->zoom_rect_temp.w = rect->w;
   plane->zoom_rect_temp.h = rect->h;

   return EINA_TRUE;

fail:
   if (plane->tpp)
     {
        tdm_pp_destroy(plane->tpp);
        plane->tpp = NULL;
     }

   return EINA_FALSE;
}

EINTERN void
e_plane_zoom_unset(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN(plane);

   plane->zoom_rect.x = plane->zoom_rect_temp.x = 0;
   plane->zoom_rect.y = plane->zoom_rect_temp.y = 0;
   plane->zoom_rect.w = plane->zoom_rect_temp.w = 0;
   plane->zoom_rect.h = plane->zoom_rect_temp.h = 0;

   plane->zoom_unset = EINA_TRUE;
}
