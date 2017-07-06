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
static Eina_Bool _e_plane_pp_layer_data_commit(E_Plane *plane, E_Plane_Commit_Data *data);
static Eina_Bool _e_plane_pp_commit(E_Plane *plane, E_Plane_Commit_Data *data);

E_API int E_EVENT_PLANE_WIN_CHANGE = -1;

static int _e_plane_hooks_delete = 0;
static int _e_plane_hooks_walking = 0;

static Eina_Inlist *_e_plane_hooks[] =
{
   [E_PLANE_HOOK_VIDEO_SET] = NULL,
};

static void
_e_plane_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Plane_Hook *ch;
   unsigned int x;
   for (x = 0; x < E_PLANE_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_plane_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_plane_hooks[x] = eina_inlist_remove(_e_plane_hooks[x], EINA_INLIST_GET(ch));
          free(ch);
       }
}

static void
_e_plane_hook_call(E_Plane_Hook_Point hookpoint, E_Plane *plane)
{
   E_Plane_Hook *ch;

   _e_plane_hooks_walking++;
   EINA_INLIST_FOREACH(_e_plane_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        ch->func(ch->data, plane);
     }
   _e_plane_hooks_walking--;
   if ((_e_plane_hooks_walking == 0) && (_e_plane_hooks_delete > 0))
     _e_plane_hooks_clean();
}

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

   /* skip the set the surface to the tdm layer */
   if (plane->skip_surface_set) return EINA_TRUE;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Unset   Plane(%p) zpos(%d)", NULL, NULL, plane, plane->zpos);

   CLEAR(plane->info);

   error = tdm_layer_unset_buffer(tlayer);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_unset_buffer");
        return EINA_FALSE;
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
_e_plane_tlayer_info_set(E_Plane *plane, unsigned int size_w, unsigned int size_h,
                         unsigned int src_x, unsigned int src_y, unsigned int src_w, unsigned int src_h,
                         unsigned int dst_x, unsigned int dst_y, unsigned int dst_w, unsigned int dst_h,
                         tdm_transform transform)
{
   if (plane_trace_debug)
     {
        ELOGF("E_PLANE", "Set  Plane(%p)     (%dx%d,[%d,%d,%d,%d]=>[%d,%d,%d,%d])",
              NULL, NULL, plane,
              plane->info.src_config.size.h, plane->info.src_config.size.v,
              plane->info.src_config.pos.x, plane->info.src_config.pos.y,
              plane->info.src_config.pos.w, plane->info.src_config.pos.h,
              plane->info.dst_pos.x, plane->info.dst_pos.y,
              plane->info.dst_pos.w, plane->info.dst_pos.h);
     }

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
     {
        /* change the information at plane->info */
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

        return EINA_TRUE;
     }

   return EINA_FALSE;
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

   /* skip the set the surface to the tdm layer */
   if (plane->skip_surface_set) return EINA_TRUE;

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

   if (_e_plane_tlayer_info_set(plane, aligned_width, surf_info.height,
                                0, 0, surf_info.width, surf_info.height,
                                dst_x, dst_y, dst_w, dst_h,
                                TDM_TRANSFORM_NORMAL))
     {
        error = tdm_layer_set_info(tlayer, &plane->info);
        if (error != TDM_ERROR_NONE)
          {
             ERR("fail to tdm_layer_set_info");
             return EINA_FALSE;
          }
     }

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Set  Plane(%p)     tsurface(%p)", NULL, NULL, plane, tsurface);

   error = tdm_layer_set_buffer(tlayer, tsurface);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_set_buffer");
        return EINA_FALSE;
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
   if (plane->unset_candidate)   { plane->unset_candidate = EINA_FALSE; print_log = EINA_TRUE; }
   if (plane->unset_counter > 0) { plane->unset_counter = 0;            print_log = EINA_TRUE; }
   if (plane->unset_try)         { plane->unset_try = EINA_FALSE;       print_log = EINA_TRUE; }
   if (plane->unset_commit)      { plane->unset_commit = EINA_FALSE;    print_log = EINA_TRUE; }

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

static Eina_Bool
_e_plane_pp_info_set(E_Plane *plane)
{
   tdm_info_pp pp_info;
   tdm_error ret = TDM_ERROR_NONE;
   unsigned int aligned_width = 0;
   tbm_surface_info_s surf_info;
   tbm_surface_h tsurface = plane->tsurface;
   int dst_w, dst_h;

   /* when the pp_set_info is true, change the pp set_info */
   if (!plane->pp_set_info) return EINA_TRUE;
   plane->pp_set_info = EINA_FALSE;

   tbm_surface_get_info(tsurface, &surf_info);

   aligned_width = _e_plane_aligned_width_get(tsurface);
   if (aligned_width == 0) return EINA_FALSE;

   e_output_size_get(plane->output, &dst_w, &dst_h);

   pp_info.src_config.size.h = aligned_width;
   pp_info.src_config.size.v = surf_info.height;
   pp_info.src_config.pos.x = plane->pp_rect.x;
   pp_info.src_config.pos.y = plane->pp_rect.y;
   pp_info.src_config.pos.w = plane->pp_rect.w;
   pp_info.src_config.pos.h = plane->pp_rect.h;
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

   if (plane_trace_debug)
     ELOGF("E_PLANE", "PP Info  Plane(%p) rect(x,y,w,h = %d,%d,%d,%d)",
           NULL, NULL, plane, plane->pp_rect.x, plane->pp_rect.y, plane->pp_rect.w, plane->pp_rect.h);

   return EINA_TRUE;
}

static void
_e_plane_pp_layer_commit_handler(tdm_layer *layer, unsigned int sequence,
                                 unsigned int tv_sec, unsigned int tv_usec,
                                 void *user_data)
{
   E_Plane_Commit_Data *data = (E_Plane_Commit_Data *)user_data;
   E_Plane *plane = NULL;

   EINA_SAFETY_ON_NULL_RETURN(data);

   plane = data->plane;

   plane->pp_layer_commit = EINA_FALSE;

   /* if pp_set is false, do not deal with pending list */
   if (!plane->pp_set)
     {
        tbm_surface_internal_unref(data->tsurface);
        free(data);
        return;
     }

   if (plane->pp_tqueue && plane->pp_tsurface)
     {
        /* release and unref the current pp surface on the plane */
        tbm_surface_queue_release(plane->pp_tqueue, plane->pp_tsurface);
        tbm_surface_internal_unref(plane->pp_tsurface);
     }

   /* set the new pp surface to the plane */
   plane->pp_tsurface = data->tsurface;

   free(data);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "PP Layer Commit Handler Plane(%p)", NULL, NULL, plane);

   /* deal with the pending layer commit */
   data = eina_list_nth(plane->pending_pp_commit_data_list, 0);
   if (data)
     {
        plane->pending_pp_commit_data_list = eina_list_remove(plane->pending_pp_commit_data_list, data);

        if (!_e_plane_pp_layer_data_commit(plane, data))
          {
             ERR("fail to _e_plane_pp_layer_commit");
             return;
          }
     }

   /* deal with the pending pp commit */
   data = eina_list_nth(plane->pending_pp_data_list, 0);
   if (data)
     {
        plane->pending_pp_data_list = eina_list_remove(plane->pending_pp_data_list, data);

        if (!_e_plane_pp_commit(plane, data))
          {
             ERR("fail _e_plane_pp_commit");
             return;
          }
     }
}

static Eina_Bool
_e_plane_pp_layer_commit(E_Plane *plane, tbm_surface_h tsurface)
{
   tbm_surface_h pp_tsurface = NULL;
   tbm_error_e tbm_err;
   E_Plane_Commit_Data *data = NULL;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "PP Layer Commit  Plane(%p)     pp_tsurface(%p)", NULL, NULL, plane, tsurface);

   tbm_err = tbm_surface_queue_enqueue(plane->pp_tqueue, tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("fail tbm_surface_queue_enqueue");
        goto fail;
     }

   tbm_err = tbm_surface_queue_acquire(plane->pp_tqueue, &pp_tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("fail tbm_surface_queue_acquire");
        goto fail;
     }

   data = E_NEW(E_Plane_Commit_Data, 1);
   data->plane = plane;
   data->renderer = NULL;
   data->tsurface = pp_tsurface;
   tbm_surface_internal_ref(data->tsurface);
   data->ec = NULL;

   if (plane->pp_layer_commit)
     {
        plane->pending_pp_commit_data_list = eina_list_append(plane->pending_pp_commit_data_list, data);
        return EINA_TRUE;
     }

   if (!_e_plane_pp_layer_data_commit(plane, data))
     {
        ERR("fail to _e_plane_pp_layer_data_commit");
        return EINA_FALSE;
     }

   return EINA_TRUE;

fail:
   tbm_surface_queue_release(plane->pp_tqueue, tsurface);
   if (pp_tsurface && pp_tsurface != tsurface)
     tbm_surface_queue_release(plane->pp_tqueue, pp_tsurface);

   return EINA_FALSE;
}

static Eina_Bool
_e_plane_pp_layer_data_commit(E_Plane *plane, E_Plane_Commit_Data *data)
{
   tbm_surface_info_s surf_info;
   unsigned int aligned_width;
   int dst_w, dst_h;
   tdm_layer *tlayer = plane->tlayer;
   tdm_error tdm_err;

   EINA_SAFETY_ON_NULL_RETURN_VAL(data, EINA_FALSE);

   aligned_width = _e_plane_aligned_width_get(data->tsurface);
   if (aligned_width == 0)
     {
        ERR("aligned_width 0");
        goto fail;
     }

   /* set layer when the layer infomation is different from the previous one */
   tbm_surface_get_info(data->tsurface, &surf_info);

   /* get the size of the output */
   e_output_size_get(plane->output, &dst_w, &dst_h);

   if (_e_plane_tlayer_info_set(plane, aligned_width, surf_info.height,
                                0, 0, surf_info.width, surf_info.height,
                                0, 0, dst_w, dst_h,
                                TDM_TRANSFORM_NORMAL))
     {
        tdm_err = tdm_layer_set_info(tlayer, &plane->info);
        if (tdm_err != TDM_ERROR_NONE)
          {
             ERR("fail tdm_layer_set_info");
             goto fail;
          }
     }

   tdm_err = tdm_layer_set_buffer(tlayer, data->tsurface);
   if (tdm_err != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_set_buffer");
        goto fail;
     }

   tdm_err = tdm_layer_commit(tlayer, _e_plane_pp_layer_commit_handler, data);
   if (tdm_err != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_commit plane:%p, zpos:%d", plane, plane->zpos);
        goto fail;
     }

   plane->pp_layer_commit = EINA_TRUE;

   return EINA_TRUE;

fail:
   tbm_surface_internal_unref(data->tsurface);
   tbm_surface_queue_release(plane->pp_tqueue, data->tsurface);
   free(data);

   return EINA_FALSE;
}

static void
_e_plane_pp_commit_handler(tdm_pp *pp, tbm_surface_h tsurface_src, tbm_surface_h tsurface_dst, void *user_data)
{
   E_Plane_Commit_Data *data = NULL;
   E_Plane *plane = NULL;

   data = (E_Plane_Commit_Data *)user_data;
   EINA_SAFETY_ON_NULL_RETURN(data);
   plane = data->plane;
   EINA_SAFETY_ON_NULL_RETURN(plane);

   /* release the commit data */
   if (data->ec) e_pixmap_image_clear(data->ec->pixmap, 1);
   e_plane_commit_data_release(data);

   plane->wait_commit = EINA_FALSE;
   plane->pp_commit = EINA_FALSE;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "PP Commit Handler Plane(%p)", NULL, NULL, plane);

   /* if pp_set is false, skip the commit */
   if (!plane->pp_set)
     {
        if (plane->tpp)
          {
             tdm_pp_destroy(plane->tpp);
             plane->tpp = NULL;
          }
        goto done;
     }

   if (!_e_plane_pp_layer_commit(plane, tsurface_dst))
     ERR("fail to _e_plane_pp_layer_commit");

done:
   tbm_surface_internal_unref(tsurface_src);
   tbm_surface_internal_unref(tsurface_dst);
}

static Eina_Bool
_e_plane_pp_commit(E_Plane *plane, E_Plane_Commit_Data *data)
{
   tbm_surface_h pp_tsurface = NULL;
   tbm_error_e tbm_err = TBM_ERROR_NONE;
   tdm_error tdm_err = TDM_ERROR_NONE;
   tbm_surface_h tsurface = plane->tsurface;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "PP Commit  Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
           NULL, NULL, plane, plane->zpos, data->tsurface, plane->pp_tqueue,
           data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);

   tbm_err = tbm_surface_queue_dequeue(plane->pp_tqueue, &pp_tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("fail tbm_surface_queue_dequeue");
        return EINA_FALSE;
     }

   if (!_e_plane_pp_info_set(plane))
     {
        ERR("fail _e_plane_pp_info_set");
        goto pp_fail;
     }

   tdm_err = tdm_pp_set_done_handler(plane->tpp, _e_plane_pp_commit_handler, data);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, pp_fail);

   tbm_surface_internal_ref(pp_tsurface);
   tbm_surface_internal_ref(tsurface);
   tdm_err = tdm_pp_attach(plane->tpp, tsurface, pp_tsurface);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, attach_fail);

   tdm_err = tdm_pp_commit(plane->tpp);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, commit_fail);

   plane->wait_commit = EINA_TRUE;
   plane->pp_commit = EINA_TRUE;

   return EINA_TRUE;

commit_fail:
attach_fail:
   tbm_surface_internal_unref(pp_tsurface);
   tbm_surface_internal_unref(tsurface);
pp_fail:

   ERR("failed _e_plane_pp_commit");

   return EINA_FALSE;
}

E_API E_Plane_Hook *
e_plane_hook_add(E_Plane_Hook_Point hookpoint, E_Plane_Hook_Cb func, const void *data)
{
   E_Plane_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_PLANE_HOOK_LAST, NULL);
   ch = E_NEW(E_Plane_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_plane_hooks[hookpoint] = eina_inlist_append(_e_plane_hooks[hookpoint], EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_plane_hook_del(E_Plane_Hook *ch)
{
   ch->delete_me = 1;
   if (_e_plane_hooks_walking == 0)
     {
        _e_plane_hooks[ch->hookpoint] = eina_inlist_remove(_e_plane_hooks[ch->hookpoint], EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_plane_hooks_delete++;
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
   const tbm_format *formats;
   int count;
   int i;

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
   plane->skip_surface_set = EINA_FALSE;

   tdm_err = tdm_layer_get_buffer_flags(plane->tlayer, &buffer_flags);
   if (tdm_err == TDM_ERROR_NONE)
     plane->buffer_flags = buffer_flags;

   /* check the layer is the primary layer */
   if (layer_capabilities & TDM_LAYER_CAPABILITY_PRIMARY)
     plane->is_primary = EINA_TRUE;

   if (layer_capabilities & TDM_LAYER_CAPABILITY_VIDEO)
     plane->type = E_PLANE_TYPE_VIDEO;
   else if (layer_capabilities & TDM_LAYER_CAPABILITY_CURSOR)
     plane->type = E_PLANE_TYPE_CURSOR;
   else if (layer_capabilities & TDM_LAYER_CAPABILITY_GRAPHIC)
     plane->type = E_PLANE_TYPE_GRAPHIC;
   else
     plane->type = E_PLANE_TYPE_INVALID;

   tdm_err = tdm_layer_get_available_formats(plane->tlayer, &formats, &count);
   if (tdm_err != TDM_ERROR_NONE)
     {
        ERR("fail to get available formats");
        E_FREE(plane);
        return NULL;
     }

   for ( i = 0 ; i < count ; i++)
     plane->available_formats = eina_list_append(plane->available_formats, &formats[i]);

   INF("E_PLANE: (%d) plane:%p name:%s zpos:%d capa:%s %s",
       index, plane, plane->name,
       plane->zpos,plane->is_primary ? "primary" : "",
       plane->reserved_memory ? "reserved_memory" : "");

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

static Eina_Bool
_e_plane_fb_target_change(E_Plane *fb_target, E_Plane *plane)
{
   E_Plane_Renderer *renderer = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(fb_target, EINA_FALSE);

   if (fb_target == plane) return EINA_TRUE;

   renderer = fb_target->renderer;

   if (plane->renderer)
     _e_plane_renderer_unset(plane);

   renderer->plane = plane;
   plane->renderer = renderer;
   fb_target->renderer = NULL;

   fb_target->is_fb = EINA_FALSE;
   plane->is_fb = EINA_TRUE;

   fb_target->unset_counter = 1;
   fb_target->unset_candidate = EINA_TRUE;

   CLEAR(plane->info);
   _e_plane_unset_reset(plane);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Change fb_target Plane(%p) zpos(%d) -> plane(%p) zpos(%d)",
           NULL, NULL, fb_target, fb_target->zpos, plane, plane->zpos);

   return EINA_TRUE;
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
_e_plane_fb_target_change_check(E_Plane *plane)
{
   if (!plane->is_fb) return;
   if (!plane->fb_change || !plane->fb_change_counter) return;

   plane->fb_change_counter--;

   if (plane->fb_change_counter) return;

   if (!_e_plane_fb_target_change(plane, plane->fb_change))
     ERR("fail to change fb_target");

   plane->fb_change = NULL;

   return;
}

EINTERN Eina_Bool
e_plane_commit(E_Plane *plane)
{
   E_Plane_Commit_Data *data = NULL;
   tdm_error error = TDM_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   data = e_plane_commit_data_aquire(plane);

   if (!data) return EINA_TRUE;

   _e_plane_fb_target_change_check(plane);

   TRACE_DS_ASYNC_BEGIN((unsigned int)plane->tlayer, [PLANE:COMMIT~HANDLER]);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Commit  Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
           NULL, NULL, plane, plane->zpos, data->tsurface, plane->renderer ? plane->renderer->tqueue : NULL,
           data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);

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

        if (plane->reserved_video)
          {
             e_comp_override_del();
             plane->reserved_video = EINA_FALSE;
             plane->is_video = EINA_TRUE;

             if (plane_trace_debug)
               ELOGF("E_PLANE", "Call HOOK_VIDEO_SET Plane(%p) zpos(%d)", NULL, NULL, plane, plane->zpos);

             _e_plane_hook_call(E_PLANE_HOOK_VIDEO_SET, plane);
          }
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
         * 1. if the plane is fb target, the plane uses the ecore_evas.
         * 2. if the plane is not fb target, the plane needs to unset
         *    at the time that the result of the ecore_evas renderer(compositing)
         *    is finished with the tsurface(ec) of the plane. For this,
         *    we set the unset_candidate flags to the plane and measure to unset
         *    the plane at the e_output_commit.
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

EINTERN Eina_Bool
e_plane_fb_target_set(E_Plane *plane, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   plane->is_fb = set;

   return EINA_TRUE;
}

E_API Eina_Bool
e_plane_is_fb_target(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (plane->is_fb) return EINA_TRUE;

   return EINA_FALSE;
}

EINTERN Eina_List *
e_plane_available_tbm_formats_get(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   return plane->available_formats;
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

E_API Eina_Bool
e_plane_video_usable(E_Plane *plane)
{
   E_Plane *fb_target = NULL;
   E_Output *output;
   E_Plane *find_plane;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   output = plane->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   fb_target = e_output_fb_target_get(output);
   EINA_SAFETY_ON_NULL_RETURN_VAL(fb_target, EINA_FALSE);

   if (fb_target->zpos > plane->zpos) return EINA_TRUE;

   if (fb_target->is_reserved) return EINA_FALSE;

   find_plane = e_output_plane_get_by_zpos(output, plane->zpos + 1);
   if (!find_plane) return EINA_FALSE;

   return EINA_TRUE;
}

E_API Eina_Bool
e_plane_video_set(E_Plane *plane, Eina_Bool set, Eina_Bool *wait)
{
   E_Output *output = NULL;
   E_Plane *fb_target = NULL;
   E_Plane *default_fb = NULL;
   E_Plane *change_plane = NULL;
   Eina_Bool fb_hwc_on = EINA_FALSE;
   Eina_Bool fb_full_comp = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (wait) *wait = EINA_FALSE;

   if(!e_plane_video_usable(plane))
     {
        ERR("plane:%p zpos:%d not video usable", plane, plane->zpos);
        return EINA_FALSE;
     }

   output = plane->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   fb_target = e_output_fb_target_get(output);
   EINA_SAFETY_ON_NULL_RETURN_VAL(fb_target, EINA_FALSE);

   if (fb_target->ec) fb_hwc_on = EINA_TRUE;
   if (e_output_is_fb_full_compositing(output)) fb_full_comp = EINA_TRUE;

   if (set)
     {
        if (fb_target->zpos > plane->zpos)
          {
             plane->is_video = EINA_TRUE;
             e_plane_role_set(plane, E_PLANE_ROLE_VIDEO);
             if (wait) *wait = EINA_FALSE;
             return EINA_TRUE;
          }

        change_plane = e_output_plane_get_by_zpos(output, plane->zpos + 1);
        EINA_SAFETY_ON_NULL_RETURN_VAL(change_plane, EINA_FALSE);

        e_comp_override_add();
        plane->reserved_video = EINA_TRUE;

        fb_target->fb_change_counter = e_plane_renderer_render_count_get(fb_target->renderer);

        if (fb_hwc_on || fb_full_comp || !fb_target->fb_change_counter)
          {
             if (fb_full_comp) e_plane_renderer_ecore_evas_force_render(fb_target->renderer);

             if (!_e_plane_fb_target_change(fb_target, change_plane))
               {
                  e_comp_override_del();
                  ERR("fail to change fb_target");
                  return EINA_FALSE;
               }
          }
        else
          {
             fb_target->fb_change = change_plane;
          }

        e_plane_role_set(plane, E_PLANE_ROLE_VIDEO);
        if (wait) *wait = EINA_TRUE;

        if (plane_trace_debug)
          ELOGF("E_PLANE", "Video   Plane(%p) zpos(%d) Set wait(%d) counter(%d) change_plane(%p) zpos(%d)",
                NULL, NULL, plane, plane->zpos, wait ? *wait : 0, fb_target->fb_change_counter,
                change_plane, change_plane->zpos);
     }
   else
     {
        default_fb = e_output_default_fb_target_get(output);
        EINA_SAFETY_ON_NULL_RETURN_VAL(default_fb, EINA_FALSE);

        if (plane->reserved_video)
          {
             plane->reserved_video = EINA_FALSE;
             e_comp_override_del();
          }

        if (default_fb->zpos > plane->zpos)
          {
             plane->is_video = EINA_FALSE;
             e_plane_role_set(plane, E_PLANE_ROLE_NONE);
             return EINA_TRUE;
          }

        e_comp_override_add();

        if (fb_full_comp) e_plane_renderer_ecore_evas_force_render(fb_target->renderer);

        if (!_e_plane_fb_target_change(fb_target, default_fb))
          {
             ERR("fail to change fb_target");
             e_comp_override_del();
             return EINA_FALSE;
          }

        plane->is_video = EINA_FALSE;
        e_plane_role_set(plane, E_PLANE_ROLE_NONE);

        e_comp_override_del();

        if (plane_trace_debug)
          ELOGF("E_PLANE", "Video   Plane(%p) zpos(%d) Unset default_fb(%p) zpos(%d)",
                NULL, NULL, plane, plane->zpos, default_fb, default_fb->zpos);
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_pp_commit(E_Plane *plane)
{
   E_Plane_Commit_Data *data = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane->pp_tqueue, EINA_FALSE);

   data = e_plane_commit_data_aquire(plane);
   if (!data) return EINA_TRUE;

   if (!tbm_surface_queue_can_dequeue(plane->pp_tqueue, 0))
     {
        if (plane_trace_debug)
          ELOGF("E_PLANE", "PP Commit  Can Dequeue failed Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
                NULL, NULL, plane, plane->zpos, data->tsurface, plane->pp_tqueue,
                data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);
        plane->pending_pp_data_list = eina_list_append(plane->pending_pp_data_list, data);
        return EINA_TRUE;
     }

   if (eina_list_count(plane->pending_pp_data_list) != 0)
     {
        if (plane_trace_debug)
          ELOGF("E_PLANE", "PP Commit  Pending pp data remained Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
                NULL, NULL, plane, plane->zpos, data->tsurface, plane->pp_tqueue,
                data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);
        plane->pending_pp_data_list = eina_list_append(plane->pending_pp_data_list, data);
        return EINA_TRUE;
     }

   if (!_e_plane_pp_commit(plane, data))
     {
        ERR("fail _e_plane_pp_commit");
        e_plane_commit_data_release(data);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_zoom_set(E_Plane *plane, Eina_Rectangle *rect)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   E_Comp_Screen *e_comp_screen = NULL;
   tdm_error ret = TDM_ERROR_NONE;
   int w, h;

   if ((plane->pp_rect.x == rect->x) &&
       (plane->pp_rect.y == rect->y) &&
       (plane->pp_rect.w == rect->w) &&
       (plane->pp_rect.h == rect->h))
     return EINA_TRUE;

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

   if (!plane->pp_tqueue)
     {
        plane->pp_tqueue = tbm_surface_queue_create(3, w, h, TBM_FORMAT_ARGB8888, plane->buffer_flags | TBM_BO_SCANOUT);
        if (!plane->pp_tqueue)
          {
             ERR("fail tbm_surface_queue_create");
             goto fail;
          }
     }

   plane->pp_rect.x = rect->x;
   plane->pp_rect.y = rect->y;
   plane->pp_rect.w = rect->w;
   plane->pp_rect.h = rect->h;

   plane->pp_set = EINA_TRUE;
   plane->skip_surface_set = EINA_TRUE;
   plane->pp_set_info = EINA_TRUE;

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
   E_Plane_Commit_Data *data = NULL;
   Eina_List *l, *ll;

   EINA_SAFETY_ON_NULL_RETURN(plane);

   plane->pp_set_info = EINA_FALSE;
   plane->skip_surface_set = EINA_FALSE;
   plane->pp_set = EINA_FALSE;

   plane->pp_rect.x = 0;
   plane->pp_rect.y = 0;
   plane->pp_rect.w = 0;
   plane->pp_rect.h = 0;

   EINA_LIST_FOREACH_SAFE(plane->pending_pp_commit_data_list, l, ll, data)
     {
        if (!data) continue;
        plane->pending_pp_commit_data_list = eina_list_remove_list(plane->pending_pp_commit_data_list, l);
        tbm_surface_queue_release(plane->pp_tqueue, data->tsurface);
        tbm_surface_internal_unref(data->tsurface);
        free(data);
     }

   EINA_LIST_FOREACH_SAFE(plane->pending_pp_data_list, l, ll, data)
     {
        if (!data) continue;
        plane->pending_pp_data_list = eina_list_remove_list(plane->pending_pp_data_list, l);
        if (data->ec) e_pixmap_image_clear(data->ec->pixmap, 1);
        e_plane_commit_data_release(data);
     }

   if (plane->pp_tsurface)
     {
        tbm_surface_queue_release(plane->pp_tqueue, plane->pp_tsurface);
        tbm_surface_internal_unref(plane->pp_tsurface);
        plane->pp_tsurface = NULL;
     }

   if (plane->pp_tqueue)
     {
        tbm_surface_queue_destroy(plane->pp_tqueue);
        plane->pp_tqueue = NULL;
     }

   if (!plane->pp_commit)
     {
        if (plane->tpp)
          {
             tdm_pp_destroy(plane->tpp);
             plane->tpp = NULL;
          }
     }
}
