#include "e.h"

# include <gbm/gbm_tbm.h>
# include <tdm.h>
# include <tdm_helper.h>
# include <tbm_surface.h>
# include <tbm_surface_internal.h>
# include <wayland-tbm-server.h>
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
   [E_PLANE_HOOK_UNSET] = NULL,
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
   return e_comp->e_comp_screen->tqueue;
}

static Eina_Bool
_e_plane_surface_can_set(E_Plane *plane, tbm_surface_h tsurface)
{
   E_Output *output = NULL;
   E_Plane *tmp_plane = NULL;
   Eina_List *l;

   output = plane->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   EINA_LIST_FOREACH(output->planes, l, tmp_plane)
     {
        if (plane == tmp_plane) continue;

        if (tmp_plane->tsurface == tsurface)
          {
             if (plane_trace_debug)
               ELOGF("E_PLANE", "Used    Plane(%p) zpos(%d) tsurface(%p)",
                      NULL, tmp_plane, tmp_plane->zpos, tsurface);

             return EINA_FALSE;
          }
     }

   return EINA_TRUE;
}

EINTERN void
e_plane_renderer_clean(E_Plane *plane)
{
   Eina_List *data_l;
   E_Plane_Commit_Data *data = NULL;

   EINA_SAFETY_ON_NULL_RETURN(plane);

   plane->display_info.renderer = NULL;

   EINA_LIST_FOREACH(plane->commit_data_list, data_l, data)
     data->renderer = NULL;
}

EINTERN void
e_plane_renderer_unset(E_Plane *plane)
{
   E_Plane_Role role;

   EINA_SAFETY_ON_NULL_RETURN(plane);

   if (!plane->renderer) return;

   role = e_plane_role_get(plane);
   if (role != E_PLANE_ROLE_CURSOR)
     {
        if (plane->reserved_memory)
          e_plane_renderer_reserved_deactivate(plane->renderer);
        else
          e_plane_renderer_deactivate(plane->renderer);
     }

   if (plane->renderer->exported_wl_buffer_count > 0) return;

   e_plane_renderer_clean(plane);
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
     ELOGF("E_PLANE", "Unset   Plane(%p) zpos(%d)", NULL, plane, plane->zpos);

   CLEAR(plane->info);

   error = tdm_layer_unset_buffer(tlayer);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_unset_buffer: error(%d)", error);
        return EINA_FALSE;
     }

   _e_plane_hook_call(E_PLANE_HOOK_UNSET, plane);

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
              NULL, plane,
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

static void
_e_plane_cursor_position_get(E_Pointer *ptr, int width, int height, int *x, int *y)
{
   int rotation;

   rotation = ptr->rotation;

   switch (rotation)
     {
      case 0:
        *x = ptr->x - ptr->hot.x;
        *y = ptr->y - ptr->hot.y;
        break;
      case 90:
        *x = ptr->x - ptr->hot.y;
        *y = ptr->y + ptr->hot.x - width;
        break;
      case 180:
        *x = ptr->x + ptr->hot.x - width;
        *y = ptr->y + ptr->hot.y - height;
        break;
      case 270:
        *x = ptr->x + ptr->hot.y - height;
        *y = ptr->y - ptr->hot.x;
        break;
      default:
        *x = ptr->x - ptr->hot.x;
        *y = ptr->y - ptr->hot.y;
        break;
     }

   return;
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

             _e_plane_cursor_position_get(pointer, surf_info.width, surf_info.height,
                                          &dst_x, &dst_y);
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

   if (plane->ec && !plane->display_info.buffer_ref.buffer)
     tbm_surface_internal_set_damage(tsurface, 0, 0, surf_info.width, surf_info.height);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Set  Plane(%p)     tsurface(%p)", NULL, plane, tsurface);

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
_e_plane_surface_cancel_acquire(E_Plane *plane, tbm_surface_h tsurface)
{
   E_Client *ec = plane->ec;
   E_Plane_Renderer *renderer = plane->renderer;

   if ((plane->is_fb && !plane->ec) ||
       (plane->ec && plane->role == E_PLANE_ROLE_OVERLAY && plane->is_reserved))
     {
        if (!e_plane_renderer_surface_queue_cancel_acquire(renderer, tsurface))
          ERR("fail to e_plane_renderer_surface_queue_cancel_acquire");
     }

   if (ec) e_comp_object_hwc_update_set(ec->frame, EINA_TRUE);

   return;
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
                ec, plane, plane->zpos, ec, e_client_util_name_get(ec));

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

   if (!buffer) return NULL;

   if (!e_comp_object_hwc_update_exists(ec->frame)) return NULL;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Display Plane(%p) zpos(%d)   Client ec(%p, %s)",
           ec, plane, plane->zpos, ec, e_client_util_name_get(ec));

   e_comp_object_hwc_update_set(ec->frame, EINA_FALSE);

   if (plane->is_fb)
     {
        if (!e_plane_renderer_surface_queue_clear(renderer))
          ERR("fail to e_plane_renderer_surface_queue_clear");
     }

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
           ec, plane, plane->zpos, ec);

   e_comp_object_hwc_update_set(ec->frame, EINA_FALSE);

   if (!e_plane_renderer_cursor_surface_refresh(renderer, ec))
     {
        ERR("Failed to e_plane_renderer_cursor_surface_refresh");
        return NULL;
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
                NULL, plane, plane->zpos);
     }

   return tsurface;
}

static tbm_surface_h
_e_plane_external_surface_acquire(E_Plane *plane)
{
   tbm_surface_h tsurface = NULL;
   tdm_error ret = TDM_ERROR_NONE;
   E_Plane *plane_primary_output_fb = NULL;

   if (plane->ext_state == E_OUTPUT_EXT_MIRROR)
     {
        if (e_hwc_policy_get(plane->output_primary->hwc) == E_HWC_POLICY_PLANES)
          {
             plane_primary_output_fb = e_output_fb_target_get(plane->output_primary);
             EINA_SAFETY_ON_NULL_RETURN_VAL(plane_primary_output_fb, NULL);

             if ((plane->pp_rect.x != plane->mirror_rect.x) ||
                 (plane->pp_rect.y != plane->mirror_rect.y) ||
                 (plane->pp_rect.w != plane->mirror_rect.w) ||
                 (plane->pp_rect.h != plane->mirror_rect.h))
               e_plane_zoom_set(plane, &plane->mirror_rect);

             tsurface = tdm_layer_get_displaying_buffer(plane_primary_output_fb->tlayer, &ret);
             EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, NULL);
          }
        else
          {
             tsurface = plane->output_primary->hwc->target_hwc_window->hwc_window.buffer.tsurface;
             EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, NULL);
          }
     }
   else if (plane->ext_state == E_OUTPUT_EXT_PRESENTATION)
     {
        if (plane->ec)
          {
             /* acquire the surface */
             if (plane->reserved_memory)
               tsurface = _e_plane_surface_from_client_acquire_reserved(plane);
             else
               tsurface = _e_plane_surface_from_client_acquire(plane);

             if ((tbm_surface_get_width(tsurface) <= 1) || (tbm_surface_get_height(tsurface) <= 1))
               return NULL;
          }
        else
          return NULL;
     }

   if (tsurface == plane->tsurface)
     return NULL;

   return tsurface;
}

static void
_e_plane_surface_send_usable_dequeuable_surfaces(E_Plane *plane)
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

        e_plane_renderer_surface_usable_send(renderer, renderer->ec, tsurface);
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

        e_plane_ec_prepare_set(plane, NULL);
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
_e_plane_fb_target_all_set_unset_counter_reset(E_Plane *fb_target)
{
   E_Plane *plane = NULL;
   E_Output *output = NULL;
   Eina_List *l;

   output = fb_target->output;
   EINA_SAFETY_ON_NULL_RETURN(output);

   EINA_LIST_FOREACH(output->planes, l, plane)
     {
        /* reset the unset_counter */
        if (plane->unset_counter > 0)
          {
             plane->unset_counter = 1;

             if (plane_trace_debug)
               ELOGF("E_PLANE", " Plane(%p) Unset Counter Reset", NULL, plane);
          }

        /* reset the set_counter */
        if (plane->set_counter > 0)
          {
             plane->set_counter = 1;

             if (plane_trace_debug)
               ELOGF("E_PLANE", " Plane(%p) Set Counter Reset", NULL, plane);
          }
     }
}

static void
_e_plane_fb_target_all_unset_candidate_sync_fb(E_Plane *fb_target)
{
   E_Plane *plane = NULL;
   E_Output *output = NULL;
   Eina_Bool set_sync_count = EINA_FALSE;
   Eina_List *l;

   output = fb_target->output;
   EINA_SAFETY_ON_NULL_RETURN(output);

   EINA_LIST_FOREACH(output->planes, l, plane)
     {
        if ((plane->unset_candidate) && (plane->unset_counter == 0))
          {
             plane->unset_counter = 1;
             set_sync_count = EINA_TRUE;
          }
     }

   if (set_sync_count)
     e_plane_renderer_surface_queue_sync_count_set(fb_target->renderer, 1);
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
     ELOGF("E_PLANE", " Plane(%p) Unset flags Reset", NULL, plane);
}

static void
_e_plane_unset_candidate_set(E_Plane *plane, Eina_Bool sync)
{
   E_Plane *fb_target = NULL;

   EINA_SAFETY_ON_NULL_RETURN(plane->ec);

   fb_target = e_output_fb_target_get(plane->output);
   if (fb_target)
     {
        if(fb_target->ec && !sync)
          plane->unset_counter = 0;
        else
          {
             Eina_Bool visible = EINA_FALSE;
             E_Plane_Renderer *renderer = fb_target->renderer;

             EINA_SAFETY_ON_NULL_RETURN(renderer);

             visible = evas_object_visible_get(plane->ec->frame);

             plane->unset_counter = e_plane_renderer_render_count_get(fb_target->renderer);

             if (visible || (!visible && !renderer->rendered))
               {
                  plane->unset_counter += 1;
                  e_plane_renderer_surface_queue_sync_count_set(fb_target->renderer, 1);
               }
             else
               e_plane_renderer_surface_queue_sync_count_set(fb_target->renderer, 0);

             e_plane_renderer_ecore_evas_force_render(fb_target->renderer);
          }
     }

   plane->unset_candidate = EINA_TRUE;
}

static void
_e_plane_set_counter_reset(E_Plane *plane)
{
   plane->set_counter = 0;
}

static void
_e_plane_set_counter_set(E_Plane *plane, E_Client *ec)
{
   E_Plane *fb_target = NULL;

   fb_target = e_output_fb_target_get(plane->output);
   EINA_SAFETY_ON_NULL_RETURN(fb_target);

   if (fb_target->ec ||
       e_plane_is_fb_target(plane) ||
       !ec->redirected ||
       !e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role))
     plane->set_counter = 0;
   else
     {
        E_Plane_Renderer *renderer = NULL;

        renderer = fb_target->renderer;
        EINA_SAFETY_ON_NULL_RETURN(renderer);

        plane->set_counter = e_plane_renderer_render_count_get(fb_target->renderer) + 1;
        e_plane_renderer_surface_queue_sync_count_set(fb_target->renderer, 1);

        e_plane_renderer_ecore_evas_force_render(fb_target->renderer);
     }

    if (plane_trace_debug)
      ELOGF("E_PLANE", "Plane(%p) set_counter(%d)", NULL, plane, plane->set_counter);
}

static Eina_Bool
_e_plane_pp_info_set(E_Plane *plane, tbm_surface_h dst_tsurface)
{
   tdm_info_pp pp_info;
   tdm_error ret = TDM_ERROR_NONE;
   unsigned int aligned_width_src = 0, aligned_width_dst = 0;
   tbm_surface_info_s surf_info_src, surf_info_dst;
   tbm_surface_h src_tsurface = plane->tsurface;

   /* when the pp_set_info is true, change the pp set_info */
   if (!plane->pp_set_info) return EINA_TRUE;
   plane->pp_set_info = EINA_FALSE;

   tbm_surface_get_info(src_tsurface, &surf_info_src);

   aligned_width_src = _e_plane_aligned_width_get(src_tsurface);
   if (aligned_width_src == 0) return EINA_FALSE;

   tbm_surface_get_info(dst_tsurface, &surf_info_dst);

   aligned_width_dst = _e_plane_aligned_width_get(dst_tsurface);
   if (aligned_width_dst == 0) return EINA_FALSE;

   pp_info.src_config.size.h = aligned_width_src;
   pp_info.src_config.size.v = surf_info_src.height;
   pp_info.src_config.format = surf_info_src.format;

   pp_info.dst_config.size.h = aligned_width_dst;
   pp_info.dst_config.size.v = surf_info_dst.height;
   pp_info.dst_config.format = surf_info_dst.format;

   pp_info.transform = TDM_TRANSFORM_NORMAL;
   pp_info.sync = 0;
   pp_info.flags = 0;

   if (plane->is_external)
     {
        pp_info.src_config.pos.x = 0;
        pp_info.src_config.pos.y = 0;
        pp_info.src_config.pos.w = surf_info_src.width;
        pp_info.src_config.pos.h = surf_info_src.height;
        pp_info.dst_config.pos.x = plane->pp_rect.x;
        pp_info.dst_config.pos.y = plane->pp_rect.y;
        pp_info.dst_config.pos.w = plane->pp_rect.w;
        pp_info.dst_config.pos.h = plane->pp_rect.h;
     }
   else
     {
        pp_info.src_config.pos.x = plane->pp_rect.x;
        pp_info.src_config.pos.y = plane->pp_rect.y;
        pp_info.src_config.pos.w = plane->pp_rect.w;
        pp_info.src_config.pos.h = plane->pp_rect.h;
        pp_info.dst_config.pos.x = 0;
        pp_info.dst_config.pos.y = 0;
        pp_info.dst_config.pos.w = surf_info_dst.width;
        pp_info.dst_config.pos.h = surf_info_dst.height;
     }
   ret = tdm_pp_set_info(plane->tpp, &pp_info);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "PP Info  Plane(%p) src_rect(%d,%d),(%d,%d), dst_rect(%d,%d),(%d,%d)",
           NULL, plane,
           pp_info.src_config.pos.x, pp_info.src_config.pos.y, pp_info.src_config.pos.w, pp_info.src_config.pos.h,
           pp_info.dst_config.pos.x, pp_info.dst_config.pos.y, pp_info.dst_config.pos.w, pp_info.dst_config.pos.h);

   return EINA_TRUE;
}

static void
_e_plane_ext_pp_commit_data_release(E_Plane_Commit_Data *data)
{
   E_Plane *plane = NULL;
   E_Plane_Renderer *renderer = NULL;
   tbm_surface_h tsurface = NULL;
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN(data);

   plane = data->plane;
   renderer = data->renderer;
   tsurface = data->tsurface;
   ec = data->ec;

   if (plane_trace_debug)
     ELOGF("E_PLANE EXT", "Done    Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)::Canvas",
           NULL, plane, plane->zpos, tsurface, renderer ? renderer->tqueue : NULL,
           data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);

   e_comp_wl_buffer_reference(&data->buffer_ref, NULL);

   /* update plane display info */
   plane->display_info.renderer = renderer;
   plane->display_info.ec = ec;

   if (tsurface)
     tbm_surface_internal_unref(tsurface);

   plane->commit_data_list = eina_list_remove(plane->commit_data_list, data);
   free(data);
}

static void
_e_plane_pp_pending_data_remove(E_Plane *plane)
{
   E_Plane_Commit_Data *data = NULL;
   Eina_List *l = NULL, *ll = NULL;

   if (plane->pp_layer_commit_data)
     {
        data = plane->pp_layer_commit_data;
        plane->pp_layer_commit_data = NULL;

        tbm_surface_internal_unref(data->tsurface);
        tbm_surface_queue_release(plane->pp_tqueue, data->tsurface);
        E_FREE(data);
     }

   if (eina_list_count(plane->pending_pp_commit_data_list) != 0)
     {
        EINA_LIST_FOREACH_SAFE(plane->pending_pp_commit_data_list, l, ll, data)
          {
             if (!data) continue;
             plane->pending_pp_commit_data_list = eina_list_remove_list(plane->pending_pp_commit_data_list, l);
             tbm_surface_queue_release(plane->pp_tqueue, data->tsurface);
             tbm_surface_internal_unref(data->tsurface);
             E_FREE(data);
          }
     }
   eina_list_free(plane->pending_pp_commit_data_list);
   plane->pending_pp_commit_data_list = NULL;

   if (eina_list_count(plane->pending_pp_data_list) != 0)
     {
        EINA_LIST_FOREACH_SAFE(plane->pending_pp_data_list, l, ll, data)
          {
             if (!data) continue;
             plane->pending_pp_data_list = eina_list_remove_list(plane->pending_pp_data_list, l);

             if (plane->is_external)
               {
                  _e_plane_ext_pp_commit_data_release(data);
               }
             else
               {
                  if (data->ec) e_pixmap_image_clear(data->ec->pixmap, 1);
                  e_plane_commit_data_release(data);
               }
          }
     }
   eina_list_free(plane->pending_pp_data_list);
   plane->pending_pp_data_list = NULL;
}

static void
_e_plane_pp_pending_data_treat(E_Plane *plane)
{
   E_Plane_Commit_Data *data = NULL;

   /* deal with the pending layer commit */
   if (eina_list_count(plane->pending_pp_commit_data_list) != 0)
     {
        data = eina_list_nth(plane->pending_pp_commit_data_list, 0);
        if (data)
          {
             plane->pending_pp_commit_data_list = eina_list_remove(plane->pending_pp_commit_data_list, data);

             if (plane_trace_debug)
               ELOGF("E_PLANE", "PP Layer Commit Handler start pending commit data(%p) tsurface(%p)", NULL, data, data->tsurface);

             if (!_e_plane_pp_layer_data_commit(plane, data))
               {
                  ERR("fail to _e_plane_pp_layer_commit");
                  return;
               }
          }
     }

   /* deal with the pending pp commit */
   if (eina_list_count(plane->pending_pp_data_list) != 0)
     {
        data = eina_list_nth(plane->pending_pp_data_list, 0);
        if (data)
          {
             if (!tbm_surface_queue_can_dequeue(plane->pp_tqueue, 0))
               return;

             plane->pending_pp_data_list = eina_list_remove(plane->pending_pp_data_list, data);

             if (plane_trace_debug)
               ELOGF("E_PLANE", "PP Layer Commit Handler start pending pp data(%p) tsurface(%p)", NULL, data, data->tsurface);

             if (!_e_plane_pp_commit(plane, data))
               {
                  ERR("fail _e_plane_pp_commit");
                  e_plane_commit_data_release(data);
                  return;
               }
          }
     }
}

static void
_e_plane_pp_layer_commit_handler(tdm_layer *layer, unsigned int sequence,
                                 unsigned int tv_sec, unsigned int tv_usec,
                                 void *user_data)
{
   E_Plane *plane;
   E_Plane_Commit_Data *data;
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN(user_data);

   plane = user_data;

   plane->pp_layer_commit = EINA_FALSE;

   /* layer already resetted */
   if (plane->pp_layer_commit_data)
     {
        data = plane->pp_layer_commit_data;
        plane->pp_layer_commit_data = NULL;

        /* if pp_set is false, do not deal with pending list */
        if (!plane->pp_set)
          {
             tbm_surface_internal_unref(data->tsurface);
             E_FREE(data);

             if (plane->is_external)
               {
                  tdm_layer_unset_buffer(plane->tlayer);
                  tdm_layer_commit(plane->tlayer, NULL, NULL);
               }

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

        E_FREE(data);
     }

   if (plane_trace_debug)
     ELOGF("E_PLANE", "PP Layer Commit Handler Plane(%p)", NULL, plane);

   output = plane->output;
   if (e_output_dpms_get(output) == E_OUTPUT_DPMS_OFF)
     {
        _e_plane_pp_pending_data_remove(plane);

        if (plane->is_external)
          tdm_layer_unset_buffer(plane->tlayer);

        return;
     }

   _e_plane_pp_pending_data_treat(plane);
}

static Eina_Bool
_e_plane_pp_layer_commit(E_Plane *plane, tbm_surface_h tsurface)
{
   tbm_surface_h pp_tsurface = NULL;
   tbm_error_e tbm_err;
   E_Plane_Commit_Data *data = NULL;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "PP Layer Commit  Plane(%p)     pp_tsurface(%p)", NULL, plane, tsurface);

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
   if (!data) goto fail;
   data->plane = plane;
   data->renderer = NULL;
   data->tsurface = pp_tsurface;
   tbm_surface_internal_ref(data->tsurface);
   data->ec = NULL;

   if ((plane->pp_layer_commit) || eina_list_count(plane->pending_pp_commit_data_list))
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
   E_Output *output = NULL;
   tbm_surface_info_s surf_info;
   unsigned int aligned_width;
   int dst_w, dst_h;
   tdm_layer *tlayer = plane->tlayer;
   tdm_error tdm_err;

   EINA_SAFETY_ON_NULL_RETURN_VAL(data, EINA_FALSE);

   output = plane->output;
   if (e_output_dpms_get(output) == E_OUTPUT_DPMS_OFF)
     {
        _e_plane_pp_pending_data_remove(plane);
        goto fail;
     }

   if (plane->is_external)
     {
        if (!e_output_connected(output))
          {
             tdm_layer_unset_buffer(tlayer);
             goto fail;
          }
     }

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

   if (dst_w != surf_info.width || dst_h != surf_info.height)
     {
        tbm_surface_queue_release(plane->pp_tqueue, data->tsurface);
        tbm_surface_internal_unref(data->tsurface);
        E_FREE(data);
        DBG("queue reset current:%dx%d, old:%dx%d", dst_w, dst_h, surf_info.width, surf_info.height);
        _e_plane_pp_pending_data_treat(plane);

        return EINA_TRUE;
     }

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

   tdm_err = tdm_layer_commit(tlayer, _e_plane_pp_layer_commit_handler, plane);
   if (tdm_err != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_commit plane:%p, zpos:%d", plane, plane->zpos);
        goto fail;
     }

   plane->pp_layer_commit = EINA_TRUE;
   plane->pp_layer_commit_data = data;

   return EINA_TRUE;

fail:
   tbm_surface_internal_unref(data->tsurface);
   tbm_surface_queue_release(plane->pp_tqueue, data->tsurface);
   E_FREE(data);

   return EINA_FALSE;
}

static E_Plane_Commit_Data *
_e_plane_pp_data_get(E_Plane *plane, tbm_surface_h tsurface)
{
   Eina_List *l;
   E_Plane_Commit_Data *data = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane->pp_data_list, NULL);

   EINA_LIST_FOREACH(plane->pp_data_list, l, data)
     {
        if (!data) continue;

        if (data->tsurface == tsurface)
          return data;
     }

   return NULL;
}

static void
_e_plane_pp_commit_handler(tdm_pp *pp, tbm_surface_h tsurface_src, tbm_surface_h tsurface_dst, void *user_data)
{
   E_Output *output = NULL;
   E_Plane *plane = NULL;
   E_Plane_Commit_Data *data = NULL;
   tbm_surface_info_s info;
   int w, h;

   plane = (E_Plane *)user_data;
   EINA_SAFETY_ON_NULL_RETURN(plane);

   plane->wait_commit = EINA_FALSE;
   plane->pp_commit = EINA_FALSE;

   data = _e_plane_pp_data_get(plane, tsurface_src);
   EINA_SAFETY_ON_NULL_RETURN(data);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "PP Commit Handler Plane(%p), data(%p)", NULL, plane, data);

   plane->pp_data_list = eina_list_remove(plane->pp_data_list, data);

   /* release the commit data */
   if (plane->is_external)
     {
        _e_plane_ext_pp_commit_data_release(data);
     }
   else
     {
        if (data->ec) e_pixmap_image_clear(data->ec->pixmap, 1);
        e_plane_commit_data_release(data);
     }

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

   output = plane->output;
   if (e_output_dpms_get(output) == E_OUTPUT_DPMS_OFF)
     {
        _e_plane_pp_pending_data_remove(plane);
        tbm_surface_queue_release(plane->pp_tqueue, tsurface_dst);

        goto done;
     }

     /* check queue reset */
     e_output_size_get(output, &w, &h);
     tbm_surface_get_info(tsurface_dst, &info);
     if (w != info.width || h != info.height)
       {
          tbm_surface_queue_release(plane->pp_tqueue, tsurface_dst);
          DBG("queue reset current:%dx%d, old:%dx%d", w, h, info.width, info.height);
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
   E_Output *output = NULL;
   tbm_surface_h pp_tsurface = NULL;
   tbm_error_e tbm_err = TBM_ERROR_NONE;
   tdm_error tdm_err = TDM_ERROR_NONE;
   tbm_surface_h tsurface = data->tsurface;

   if (plane_trace_debug)
     ELOGF("E_PLANE", "PP Commit  Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
           NULL, plane, plane->zpos, data->tsurface, plane->pp_tqueue,
           data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);

   output = plane->output;
   if (e_output_dpms_get(output) == E_OUTPUT_DPMS_OFF)
     {
        _e_plane_pp_pending_data_remove(plane);
        return EINA_FALSE;
     }

   tbm_err = tbm_surface_queue_dequeue(plane->pp_tqueue, &pp_tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("fail tbm_surface_queue_dequeue");
        return EINA_FALSE;
     }

   if (!_e_plane_pp_info_set(plane, pp_tsurface))
     {
        ERR("fail _e_plane_pp_info_set");
        goto pp_fail;
     }

   tdm_err = tdm_pp_set_done_handler(plane->tpp, _e_plane_pp_commit_handler, plane);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, pp_fail);

   tbm_surface_internal_ref(pp_tsurface);
   tbm_surface_internal_ref(tsurface);
   tdm_err = tdm_pp_attach(plane->tpp, tsurface, pp_tsurface);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, attach_fail);

   plane->pp_data_list = eina_list_append(plane->pp_data_list, data);

   tdm_err = tdm_pp_commit(plane->tpp);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, commit_fail);

   plane->wait_commit = EINA_TRUE;
   plane->pp_commit = EINA_TRUE;

   return EINA_TRUE;

commit_fail:
   plane->pp_data_list = eina_list_remove(plane->pp_data_list, data);
attach_fail:
   tbm_surface_internal_unref(pp_tsurface);
   tbm_surface_internal_unref(tsurface);
pp_fail:
   tbm_surface_queue_release(plane->pp_tqueue, pp_tsurface);

   ERR("failed _e_plane_pp_commit");

   return EINA_FALSE;
}

static void
_e_plane_ex_commit_handler(tdm_layer *layer, unsigned int sequence,
                       unsigned int tv_sec, unsigned int tv_usec,
                       void *user_data)
{
   E_Plane_Commit_Data *data = (E_Plane_Commit_Data *)user_data;
   E_Plane *plane = NULL;
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN(data);

   TRACE_DS_ASYNC_END((intptr_t)layer, [PLANE:COMMIT~HANDLER]);

   plane = data->plane;

   if (!plane->commit_per_vblank)
     plane->wait_commit = EINA_FALSE;

   e_plane_commit_data_release(data);

   output = plane->output;

   if (!e_output_connected(output))
     tdm_layer_unset_buffer(plane->tlayer);
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

static Eina_Bool
_e_plane_external_commit(E_Plane *plane, E_Plane_Commit_Data *data)
{
   tdm_error error = TDM_ERROR_NONE;

   TRACE_DS_ASYNC_BEGIN((intptr_t)plane->tlayer, [PLANE:COMMIT~HANDLER]);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Ex Commit  Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
           NULL, plane, plane->zpos, data->tsurface, plane->renderer ? plane->renderer->tqueue : NULL,
           data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);

   tbm_surface_info_s surf_info;
   unsigned int aligned_width;
   int dst_w, dst_h;
   tdm_layer *tlayer = plane->tlayer;

   EINA_SAFETY_ON_NULL_RETURN_VAL(data, EINA_FALSE);

   aligned_width = _e_plane_aligned_width_get(data->tsurface);
   if (aligned_width == 0)
     {
        ERR("aligned_width 0");
        return EINA_FALSE;
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
        error = tdm_layer_set_info(tlayer, &plane->info);
        if (error != TDM_ERROR_NONE)
          {
             ERR("fail tdm_layer_set_info");
             return EINA_FALSE;
          }
     }

   error = tdm_layer_set_buffer(tlayer, data->tsurface);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_set_buffer");
        return EINA_FALSE;
     }

   error = tdm_layer_commit(plane->tlayer, _e_plane_ex_commit_handler, data);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_commit plane:%p, zpos:%d", plane, plane->zpos);
        return EINA_FALSE;
     }

   if (plane->commit_per_vblank)
     {
        error = tdm_output_wait_vblank(plane->output->toutput, 1, 0, _e_plane_vblank_handler, (void *)plane);
        if (error != TDM_ERROR_NONE)
          {
            ERR("fail to tdm_output_wait_vblank plane:%p, zpos:%d", plane, plane->zpos);
            return EINA_FALSE;
          }
     }

   /* send frame event enlightenment dosen't send frame evnet in nocomp */
   if (plane->ec)
     e_pixmap_image_clear(plane->ec->pixmap, 1);

   plane->wait_commit = EINA_TRUE;

   return EINA_TRUE;
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
   E_Comp_Screen *comp_screen = NULL;
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

   comp_screen = output->e_comp_screen;
   EINA_SAFETY_ON_NULL_RETURN_VAL(comp_screen, NULL);

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

   if (output->index != 0)
     plane->is_external = EINA_TRUE;

   if (tdm_helper_output_commit_per_vblank_enabled(plane->output->toutput))
     plane->commit_per_vblank = EINA_TRUE;

   INF("E_PLANE: (%d) plane:%p name:%s zpos:%d capa:%s %s",
       index, plane, plane->name,
       plane->zpos,plane->is_primary ? "primary" : "",
       plane->reserved_memory ? "reserved_memory" : "");

   return plane;
}

EINTERN void
e_plane_free(E_Plane *plane)
{
   Eina_List *l = NULL, *ll = NULL;
   const tbm_format *formats;

   if (!plane) return;

   EINA_LIST_FOREACH_SAFE(plane->available_formats, l, ll, formats)
     {
        if (!formats) continue;
        plane->available_formats = eina_list_remove_list(plane->available_formats, l);
     }

   if (plane->name) eina_stringshare_del(plane->name);
   if (plane->renderer) e_plane_renderer_unset(plane);
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
   EINA_SAFETY_ON_NULL_RETURN_VAL(name, EINA_FALSE);

   if(!strcmp("gl_drm_tbm", name))
     {
        ecore_evas_manual_render_set(e_comp->ee, 1);
     }
   else if(!strcmp("drm_tbm", name))
     {
        ecore_evas_manual_render_set(e_comp->ee, 1);
     }
   else if(!strcmp("gl_tbm", name))
     {
        ecore_evas_manual_render_set(e_comp->ee, 1);
     }
   else if(!strcmp("gl_tbm_ES", name))
     {
        ecore_evas_manual_render_set(e_comp->ee, 1);
     }
   else if(!strcmp("software_tbm", name))
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
}

EINTERN Eina_Bool
e_plane_render(E_Plane *plane)
{
   E_Plane_Renderer *renderer = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   renderer = plane->renderer;
   if (!renderer) return EINA_TRUE;
   if (plane->is_external) return EINA_TRUE;

   if (plane->ec)
     {
        if (!e_plane_renderer_norender(renderer, plane->is_fb))
          {
             ERR("fail to e_plane_renderer_norender");
             return EINA_FALSE;
          }
     }
   else
     {
        if (!e_plane_renderer_render(renderer, plane->is_fb))
          {
             ERR("fail to e_plane_renderer_render");
             return EINA_FALSE;
          }
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_is_fetch_retry(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   return plane->fetch_retry;
}

static void
_e_plane_fb_sync_vblank_handler(tdm_output *output, unsigned int sequence,
                        unsigned int tv_sec, unsigned int tv_usec,
                        void *user_data)
{
   E_Plane *plane = (E_Plane *)user_data;

   EINA_SAFETY_ON_NULL_RETURN(plane);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Done fb_target_sync plane(%p) zpos(%d)", NULL, plane, plane->zpos);

   plane->fb_sync_wait = EINA_FALSE;
   plane->fb_sync_done = EINA_TRUE;
}

static Eina_Bool
_e_plane_fb_target_pending_commit_sync_check(E_Plane *plane)
{
   E_Output *output;
   E_Plane *tmp_plane;
   Eina_List *l;

   if (!plane->is_fb) return EINA_FALSE;
   if (!e_plane_renderer_surface_queue_can_acquire(plane->renderer)) return EINA_FALSE;

   output = plane->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   EINA_LIST_FOREACH(output->planes, l, tmp_plane)
     {
        if (tmp_plane->is_fb) continue;
        if ((tmp_plane->unset_counter == 1) || (tmp_plane->set_counter == 1))
          {
             if (eina_list_count(tmp_plane->commit_data_list))
               return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_plane_fb_target_sync_set(E_Plane *plane)
{
   E_Output *output;
   E_Plane *tmp_plane;
   Eina_List *l;
   Eina_Bool set = EINA_FALSE;
   tdm_error error = TDM_ERROR_NONE;

   if (!plane->is_fb) return EINA_FALSE;
   if (plane->commit_per_vblank) return EINA_FALSE;
   if (!e_plane_renderer_surface_queue_can_acquire(plane->renderer)) return EINA_FALSE;

   output = plane->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   EINA_LIST_FOREACH(output->planes, l, tmp_plane)
     {
        if (tmp_plane->is_fb) continue;
        if ((tmp_plane->unset_counter == 1) || (tmp_plane->set_counter == 1))
          {
             set = EINA_TRUE;
             break;
          }
     }

   if (!set) return EINA_FALSE;

   error = tdm_output_wait_vblank(output->toutput, 1, 0,
                                  _e_plane_fb_sync_vblank_handler,
                                  (void *)plane);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_output_wait_vblank plane:%p, zpos:%d", plane, plane->zpos);
        return EINA_FALSE;
     }

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Set fb_target_sync plane(%p) zpos(%d)", NULL, plane, plane->zpos);

   plane->fb_sync_wait = EINA_TRUE;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_fetch(E_Plane *plane)
{
   tbm_surface_h tsurface = NULL;
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   output = plane->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   if (e_comp_canvas_norender_get() > 0)
     {
        if (plane_trace_debug)
          ELOGF("E_PLANE_RENDERER", "Canvas norender is set. No Display.", NULL);

        return EINA_FALSE;
     }

   if (plane->wait_commit)
     return EINA_FALSE;

   if (plane->is_external)
     {
        tsurface = _e_plane_external_surface_acquire(plane);
     }
   else if (plane->is_fb && !plane->ec)
     {
        if (_e_plane_fb_target_pending_commit_sync_check(plane))
          return EINA_FALSE;

        if (plane->fb_sync_wait)
          return EINA_FALSE;

        if (!plane->fb_sync_done && _e_plane_fb_target_sync_set(plane))
          return EINA_FALSE;

        plane->fb_sync_done = EINA_FALSE;

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
        if (!_e_plane_surface_can_set(plane, tsurface))
          {
             _e_plane_surface_cancel_acquire(plane, tsurface);
             plane->fetch_retry = EINA_TRUE;

             return EINA_FALSE;
          }

        if (plane->fetch_retry) plane->fetch_retry = EINA_FALSE;

        plane->tsurface = tsurface;

        if ((output->dpms != E_OUTPUT_DPMS_OFF) && !output->fake_config)
          {
             /* set plane info and set tsurface to the plane */
             if (!_e_plane_surface_set(plane, tsurface))
               {
                  ERR("fail: _e_plane_set_info.");
                  e_plane_unfetch(plane);
                  return EINA_FALSE;
               }
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

   /* do not reset the plane when the plan is trying to unset */
   if (e_plane_is_unset_try(plane)) return;

   EINA_SAFETY_ON_NULL_RETURN(plane->tsurface);

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
     e_plane_renderer_unset(plane);

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
           NULL, fb_target, fb_target->zpos, plane, plane->zpos);

   return EINA_TRUE;
}

static void
_e_plane_commit_hanler(tdm_layer *layer, unsigned int sequence,
                       unsigned int tv_sec, unsigned int tv_usec,
                       void *user_data)
{
   E_Plane_Commit_Data *data = (E_Plane_Commit_Data *)user_data;
   E_Plane *plane = NULL;

   EINA_SAFETY_ON_NULL_RETURN(data);

   TRACE_DS_ASYNC_END((intptr_t)layer, [PLANE:COMMIT~HANDLER]);

   plane = data->plane;

   if (!plane->commit_per_vblank)
     plane->wait_commit = EINA_FALSE;

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

static void
_e_plane_update_fps(E_Plane *plane)
{
   if (e_comp->calc_fps)
     {
        double dt;
        double tim = ecore_time_get();

        dt = tim - plane->frametimes[0];
        plane->frametimes[0] = tim;

        plane->time += dt;
        plane->cframes++;

        if (plane->lapse == 0.0)
          {
             plane->lapse = tim;
             plane->flapse = plane->cframes;
          }
        else if ((tim - plane->lapse) >= 0.5)
          {
             plane->fps = (plane->cframes - plane->flapse) / (tim - plane->lapse);
             plane->lapse = tim;
             plane->flapse = plane->cframes;
             plane->time = 0.0;
          }
     }
}

EINTERN Eina_Bool
e_plane_offscreen_commit(E_Plane *plane)
{
   E_Plane_Commit_Data *data = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if ((plane->pp_set) && e_plane_is_fb_target(plane))
     _e_plane_pp_pending_data_remove(plane);

   if (plane->unset_commit) return EINA_TRUE;

   data = e_plane_commit_data_aquire(plane);

   if (!data) return EINA_TRUE;

   _e_plane_fb_target_change_check(plane);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Commit  Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p) Offscreen",
           NULL, plane, plane->zpos, data->tsurface, plane->renderer ? plane->renderer->tqueue : NULL,
           data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);

   e_plane_commit_data_release(data);

   /* send frame event enlightenment doesn't send frame event in nocomp */
   if (plane->ec)
     e_pixmap_image_clear(plane->ec->pixmap, 1);

   if (!plane->is_fb && plane->unset_ec_pending)
     {
        plane->unset_ec_pending = EINA_FALSE;
        e_plane_ec_set(plane, NULL);
        INF("Plane:%p zpos:%d Done unset_ec_pending", plane, plane->zpos);
     }

   return EINA_TRUE;
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

   TRACE_DS_ASYNC_BEGIN((intptr_t)plane->tlayer, [PLANE:COMMIT~HANDLER]);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Commit  Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
           NULL, plane, plane->zpos, data->tsurface, plane->renderer ? plane->renderer->tqueue : NULL,
           data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);

   error = tdm_layer_commit(plane->tlayer, _e_plane_commit_hanler, data);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_layer_commit plane:%p, zpos:%d", plane, plane->zpos);
        e_plane_commit_data_release(data);
        return EINA_FALSE;
     }

   if (plane->commit_per_vblank)
     {
        error = tdm_output_wait_vblank(plane->output->toutput, 1, 0, _e_plane_vblank_handler, (void *)plane);
        if (error != TDM_ERROR_NONE)
          {
            ERR("fail to tdm_output_wait_vblank plane:%p, zpos:%d", plane, plane->zpos);
            return EINA_FALSE;
          }
     }

   /* send frame event enlightenment dosen't send frame evnet in nocomp */
   if (plane->ec)
     e_pixmap_image_clear(plane->ec->pixmap, 1);

   plane->wait_commit = EINA_TRUE;

   _e_plane_update_fps(plane);

   if (!plane->is_fb && plane->unset_ec_pending)
     {
        plane->unset_ec_pending = EINA_FALSE;
        e_plane_ec_set(plane, NULL);
        INF("Plane:%p zpos:%d Done unset_ec_pending", plane, plane->zpos);
     }

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
        if (!data) return NULL;
        data->plane = plane;
        data->renderer = plane->renderer;
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
             if (!data) return NULL;
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
                  if (!data) return NULL;
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
          ELOGF("E_PLANE", "Done    Plane(%p) zpos(%d)   data(%p)::Unset", NULL, plane, plane->zpos, data);

        e_comp_wl_buffer_reference(&plane->display_info.buffer_ref, NULL);

        if (plane->reserved_video)
          {
             e_comp_override_del();
             plane->reserved_video = EINA_FALSE;
             plane->is_video = EINA_TRUE;

             if (plane_trace_debug)
               ELOGF("E_PLANE", "Call HOOK_VIDEO_SET Plane(%p) zpos(%d)", NULL, plane, plane->zpos);

             _e_plane_hook_call(E_PLANE_HOOK_VIDEO_SET, plane);
          }
     }
   else if (!ec)
     {
        /* composite */
        /* debug */
        if (plane_trace_debug)
          ELOGF("E_PLANE", "Done    Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)::Canvas",
                NULL, plane, plane->zpos, tsurface, renderer ? renderer->tqueue : NULL,
                data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);

        e_comp_wl_buffer_reference(&plane->display_info.buffer_ref, NULL);
     }
   else
     {
        /* no composite */
        /* debug */
        if (plane_trace_debug)
          ELOGF("E_PLANE", "Done    Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)::Client",
                ec, plane, plane->zpos, tsurface, (renderer ? renderer->tqueue : NULL),
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
                       _e_plane_surface_send_usable_dequeuable_surfaces(plane);
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

E_API Eina_Bool
e_plane_is_reserved(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   return plane->is_reserved;
}

E_API void
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
                  if (!plane->ec)
                    {
                       e_plane_renderer_unset(plane);
                       e_plane_role_set(plane, E_PLANE_ROLE_NONE);
                    }
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
          ELOGF("E_PLANE", "Plane(%p) Set unset_try. unset_counter(%d)", NULL, plane, plane->unset_counter);
     }
   else
     {
        plane->unset_commit = EINA_TRUE;
        plane->unset_try = EINA_FALSE;

        if (plane_trace_debug)
          ELOGF("E_PLANE", "Plane(%p) UnSet unset_try. unset_counter(%d)", NULL, plane, plane->unset_counter);
     }
}

EINTERN Eina_Bool
e_plane_unset_commit_check(E_Plane *plane, Eina_Bool fb_commit)
{
   E_Plane *fb_target = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   fb_target = e_output_fb_target_get(plane->output);

   if (!e_plane_is_unset_try(plane))
     {
        WRN("Plane is not unset_try.");
        return EINA_FALSE;
     }

   if (fb_commit)
     {
        plane->unset_counter--;

        if (fb_target) e_plane_renderer_ecore_evas_force_render(fb_target->renderer);

        if (plane_trace_debug)
          ELOGF("E_PLANE", "Plane(%p) Check unset_commit. unset_counter(%d)", NULL, plane, plane->unset_counter);
     }

   if (plane->unset_counter > 0) return EINA_FALSE;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_set_commit_check(E_Plane *plane, Eina_Bool fb_commit)
{
   E_Plane *fb_target = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   fb_target = e_output_fb_target_get(plane->output);

   if (!plane->ec) return EINA_TRUE;
   if (!plane->set_counter) return EINA_TRUE;

   if (fb_commit)
     {
        plane->set_counter--;

        if (fb_target) e_plane_renderer_ecore_evas_force_render(fb_target->renderer);

        if (plane_trace_debug)
          ELOGF("E_PLANE", "Plane(%p) Check set counter. set_counter(%d)", NULL, plane, plane->set_counter);
     }

   if (plane->set_counter > 0) return EINA_FALSE;

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

static E_Plane*
_e_plane_ec_used_check(E_Plane *plane, E_Client *ec)
{
   E_Output *output = NULL;
   E_Plane *tmp_plane = NULL;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, NULL);

   output = plane->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   EINA_LIST_FOREACH(output->planes, l, tmp_plane)
     {
        if (plane == tmp_plane) continue;

        if (tmp_plane->ec == ec)
          {
             if (plane_trace_debug)
               ELOGF("E_PLANE", "Used    Plane(%p) zpos(%d) ec(%p)",
                      NULL, tmp_plane, tmp_plane->zpos, ec);

             return tmp_plane;
          }
     }

   return NULL;
}

E_API Eina_Bool
e_plane_ec_set(E_Plane *plane, E_Client *ec)
{
   E_Plane *used_plane = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (plane_trace_debug)
     ELOGF("E_PLANE", "Request Plane(%p) zpos(%d)   Set ec(%p, %s)",
           ec, plane, plane->zpos, ec, e_client_util_name_get(ec));

   if (plane->is_external) goto end;

   if (ec && !e_object_is_del(E_OBJECT(ec)) && (plane->is_fb || !plane->ec))
     {
        if (plane->ec == ec) return EINA_TRUE;

        if (plane->ec_redirected && plane->ec) e_client_redirected_set(plane->ec, EINA_TRUE);

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
               goto set_fail;
          }
        else
          {
             if (!plane->is_fb)
               {
                  if (e_plane_is_unset_candidate(plane) ||
                      e_plane_is_unset_try(plane) ||
                      plane->unset_commit)
                    {
                       INF("Trying to unset plane:%p zpos:%d", plane, plane->zpos);
                       return EINA_FALSE;
                    }

                  if ((plane->renderer) && (plane->role != E_PLANE_ROLE_OVERLAY))
                    e_plane_renderer_unset(plane);

                  if (!plane->renderer)
                    plane->renderer = e_plane_renderer_new(plane);
               }

             EINA_SAFETY_ON_NULL_RETURN_VAL(plane->renderer, EINA_FALSE);

             e_plane_role_set(plane, E_PLANE_ROLE_OVERLAY);

             if (!e_plane_renderer_ec_set(plane->renderer, ec))
               goto set_fail;

             if (plane->reserved_memory)
               _e_plane_surface_send_usable_dequeuable_surfaces(plane);
          }

        if (plane->is_fb)
          _e_plane_fb_target_all_set_unset_counter_reset(plane);

        _e_plane_set_counter_set(plane, ec);

        e_comp_object_hwc_update_set(ec->frame, EINA_TRUE);

        if (plane->ec_redirected)
          {
             if (plane->ec)
               {
                  used_plane = _e_plane_ec_used_check(plane, plane->ec);
                  if (used_plane)
                    e_client_redirected_set(plane->ec, EINA_FALSE);
                  else
                    e_client_redirected_set(plane->ec, EINA_TRUE);
               }

             plane->ec_redirected = EINA_FALSE;
          }

        used_plane = _e_plane_ec_used_check(plane, ec);
        if (used_plane)
          plane->ec_redirected = used_plane->ec_redirected;
        else
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
        if (!plane->is_fb && plane->ec && plane->set_counter &&
            evas_object_visible_get(plane->ec->frame))
          {
             if (!plane->unset_ec_pending)
               {
                  INF("Plane:%p zpos:%d Set unset_ec_pending ", plane, plane->zpos);
                  plane->unset_ec_pending = EINA_TRUE;
               }

             if (ec)
               return EINA_FALSE;
             else
               return EINA_TRUE;
          }

        if (plane->unset_ec_pending)
          {
             INF("Plane:%p zpos:%d Reset unset_ec_pending", plane, plane->zpos);
             plane->unset_ec_pending = EINA_FALSE;
          }

        if (plane->ec_redirected && plane->ec) e_client_redirected_set(plane->ec, EINA_TRUE);

        if (plane->is_fb)
          {
             if (!e_plane_renderer_ecore_evas_use(plane->renderer))
               {
                  ERR("failed to use ecore_evas plane:%p", plane);
                  return EINA_FALSE;
               }

             if (plane->ec)
               _e_plane_fb_target_all_unset_candidate_sync_fb(plane);
          }
        else
          {
             if (plane->tsurface && plane->ec)
               {
                  if (ec)
                    _e_plane_unset_candidate_set(plane, EINA_TRUE);
                  else
                    _e_plane_unset_candidate_set(plane, EINA_FALSE);

                  if (plane_trace_debug)
                    ELOGF("E_PLANE", "Plane(%p) Set the unset_candidate", ec, plane);
               }

             if (plane->renderer)
               {
                  _e_plane_set_counter_reset(plane);
                  e_plane_renderer_unset(plane);
                  e_plane_role_set(plane, E_PLANE_ROLE_NONE);
               }
          }

        if (plane->ec_redirected)
          {
             if (plane->ec)
               {
                  used_plane = _e_plane_ec_used_check(plane, plane->ec);
                  if (used_plane)
                    e_client_redirected_set(plane->ec, EINA_FALSE);
                  else
                    e_client_redirected_set(plane->ec, EINA_TRUE);
               }

             plane->ec_redirected = EINA_FALSE;
          }

        if (ec)
          {
             plane->ec = NULL;
             plane->need_ev = EINA_TRUE;

             ELOGF("E_PLANE", "Plane(%p) zpos(%d)   Set NULL",
                   ec, plane, plane->zpos);

             return EINA_FALSE;
          }
     }
end:
   if (plane->ec != ec)
     ELOGF("E_PLANE", "Plane(%p) zpos(%d)   Set ec(%p, %s)",
           ec, plane, plane->zpos, ec, e_client_util_name_get(ec));

   plane->ec = ec;
   plane->need_ev = EINA_TRUE;

   return EINA_TRUE;

set_fail:
   if (plane->ec_redirected)
     {
       if (plane->ec)
         {
            used_plane = _e_plane_ec_used_check(plane, plane->ec);
            if (used_plane)
              e_client_redirected_set(plane->ec, EINA_FALSE);
            else
              e_client_redirected_set(plane->ec, EINA_TRUE);
         }

       plane->ec_redirected = EINA_FALSE;
     }

   plane->ec = NULL;

   return EINA_FALSE;
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

EINTERN Eina_Bool
e_plane_available_formats_get(E_Plane *plane, const tbm_format **formats, int *count)
{
   tdm_error tdm_err;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(formats, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(count, EINA_FALSE);

   tdm_err = tdm_layer_get_available_formats(plane->tlayer, formats, count);
   if (tdm_err != TDM_ERROR_NONE)
     {
        ERR("fail to get available formats");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN void
e_plane_show_state(E_Plane *plane)
{
   EINA_SAFETY_ON_NULL_RETURN(plane);

   ELOGF("E_PLANE", "Plane(%p) zpos(%d) ec(%p) display tsurface(%p)",
         NULL, plane, plane->zpos, plane->ec, plane->display_info.tsurface);

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
                NULL, plane, plane->zpos, wait ? *wait : 0, fb_target->fb_change_counter,
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
                NULL, plane, plane->zpos, default_fb, default_fb->zpos);
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
                NULL, plane, plane->zpos, data->tsurface, plane->pp_tqueue,
                data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);
        plane->pending_pp_data_list = eina_list_append(plane->pending_pp_data_list, data);
        return EINA_TRUE;
     }

   if (eina_list_count(plane->pending_pp_data_list) != 0)
     {
        if (plane_trace_debug)
          ELOGF("E_PLANE", "PP Commit  Pending pp data remained Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
                NULL, plane, plane->zpos, data->tsurface, plane->pp_tqueue,
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
e_plane_pp_commit_possible_check(E_Plane *plane)
{
   if (!plane->pp_set) return EINA_FALSE;

   if (plane->pp_tqueue)
     {
        if (!tbm_surface_queue_can_dequeue(plane->pp_tqueue, 0))
          return EINA_FALSE;
     }

   if (plane->pending_pp_data_list)
     {
        if (eina_list_count(plane->pending_pp_data_list) != 0)
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
   EINA_SAFETY_ON_NULL_RETURN(plane);

   plane->pp_set_info = EINA_FALSE;
   plane->skip_surface_set = EINA_FALSE;
   plane->pp_set = EINA_FALSE;

   plane->pp_rect.x = 0;
   plane->pp_rect.y = 0;
   plane->pp_rect.w = 0;
   plane->pp_rect.h = 0;

   _e_plane_pp_pending_data_remove(plane);

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

EINTERN Eina_Bool
e_plane_fps_get(E_Plane *plane, double *fps)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   if (plane->old_fps == plane->fps)
     return EINA_FALSE;

   if (plane->fps > 0.0)
     {
        *fps = plane->fps;
        plane->old_fps = plane->fps;
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

EINTERN void
e_plane_dpms_off(E_Plane *plane)
{
   E_Plane_Commit_Data *data;
   Eina_List *l = NULL, *ll = NULL;
   tdm_error ret;

   if (!plane) return;

   /* pp */
   _e_plane_pp_pending_data_remove(plane);
   EINA_LIST_FOREACH_SAFE(plane->pp_data_list, l, ll, data)
     {
        e_plane_commit_data_release(data);
     }
   eina_list_free(plane->pp_data_list);
   plane->pp_data_list = NULL;

   /* TODO: fine to skip primary layer? If DRM system, the only way to unset
    * primary layer's buffer is resetting mode setting.
    */
   if (e_plane_is_primary(plane)) return;

   /* layer */
   e_plane_ec_prepare_set(plane, NULL);
   e_plane_ec_set(plane, NULL);

   _e_plane_unset_reset(plane);
   _e_plane_surface_unset(plane);

   ret = tdm_layer_commit(plane->tlayer, NULL, NULL);
   if (ret != TDM_ERROR_NONE)
     ERR("fail to tdm_layer_commit plane:%p, zpos:%d", plane, plane->zpos);

   EINA_LIST_FOREACH_SAFE(plane->commit_data_list, l, ll, data)
     {
        e_plane_commit_data_release(data);
     }
}

EINTERN Eina_Bool
e_plane_external_fetch(E_Plane *plane)
{
   tbm_surface_h tsurface = NULL;
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   output = plane->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   if (plane->wait_commit)
     return EINA_FALSE;

   tsurface = _e_plane_external_surface_acquire(plane);

   /* exist tsurface for update plane */
   if (tsurface)
     {
        plane->tsurface = tsurface;

        if (output->dpms != E_OUTPUT_DPMS_OFF)
          {
             /* set plane info and set tsurface to the plane */
             if (!_e_plane_surface_set(plane, tsurface))
               {
                  ERR("fail: _e_plane_set_info.");
                  e_plane_unfetch(plane);
                  return EINA_FALSE;
               }
          }

        /* set the update_exist to be true */
        e_plane_renderer_update_exist_set(plane->renderer, EINA_TRUE);
     }
   else
     return EINA_FALSE;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_plane_external_commit(E_Plane *plane)
{
   E_Plane_Commit_Data *data = NULL;
   tbm_surface_info_s surf_info;
   int w, h;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(plane->pp_tqueue, EINA_FALSE);

   data = e_plane_commit_data_aquire(plane);
   if (!data) return EINA_TRUE;

   tbm_surface_get_info(data->tsurface, &surf_info);
   e_output_size_get(plane->output, &w, &h);

   if (plane->ext_state == E_OUTPUT_EXT_PRESENTATION)
     {
        if (w == surf_info.width && h == surf_info.height)
          {
             if (_e_plane_external_commit(plane, data))
               return EINA_TRUE;
          }

        e_plane_commit_data_release(data);
        return EINA_FALSE;
     }
   else /* plane->ext_state == E_OUTPUT_EXT_MIRROR */
     {
        if (!tbm_surface_queue_can_dequeue(plane->pp_tqueue, 0))
          {
             if (plane_trace_debug)
               ELOGF("E_PLANE", "Ex Commit  Can Dequeue failed Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
                     NULL, plane, plane->zpos, data->tsurface, plane->pp_tqueue,
                     data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);
             plane->pending_pp_data_list = eina_list_append(plane->pending_pp_data_list, data);
             return EINA_TRUE;
          }

        if (eina_list_count(plane->pending_pp_data_list) != 0)
          {
             if (plane_trace_debug)
               ELOGF("E_PLANE", "Ex Commit  Pending pp data remained Plane(%p) zpos(%d)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
                     NULL, plane, plane->zpos, data->tsurface, plane->pp_tqueue,
                     data->buffer_ref.buffer ? data->buffer_ref.buffer->resource : NULL, data);
             plane->pending_pp_data_list = eina_list_append(plane->pending_pp_data_list, data);
             return EINA_TRUE;
          }

        if (!_e_plane_pp_commit(plane, data))
          {
             ERR("fail _e_plane_pp_commit");
             _e_plane_ext_pp_commit_data_release(data);
             return EINA_FALSE;
          }

        return EINA_TRUE;
     }
}

EINTERN Eina_Bool
e_plane_external_set(E_Plane *plane, Eina_Rectangle *rect, E_Output_Ext_State state)
{
   Eina_Bool ret = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   DBG("e_plane_external_set. state(%d) rect(%d,%d)(%d,%d)",
       state, rect->x, rect->y, rect->w, rect->h);

   plane->mirror_rect.x = rect->x;
   plane->mirror_rect.y = rect->y;
   plane->mirror_rect.w = rect->w;
   plane->mirror_rect.h = rect->h;

   if (plane->ext_state == E_OUTPUT_EXT_NONE)
     {
        ret = e_plane_zoom_set(plane, &plane->mirror_rect);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == EINA_TRUE, EINA_FALSE);
     }
   else if (plane->ext_state == E_OUTPUT_EXT_MIRROR)
     _e_plane_pp_pending_data_remove(plane);

   plane->ext_state = state;

   if (!plane->renderer)
     plane->renderer = e_plane_renderer_new(plane);

   return EINA_TRUE;
}

EINTERN void
e_plane_external_unset(E_Plane *plane)
{
   if (plane->renderer)
     e_plane_renderer_del(plane->renderer);
   plane->renderer = NULL;

   e_plane_zoom_unset(plane);

   plane->mirror_rect.x = 0;
   plane->mirror_rect.y = 0;
   plane->mirror_rect.w = 0;
   plane->mirror_rect.h = 0;

   if (plane->ext_state == E_OUTPUT_EXT_MIRROR)
     {
        if (!plane->pp_layer_commit)
          {
             tdm_layer_unset_buffer(plane->tlayer);
             tdm_layer_commit(plane->tlayer, NULL, NULL);
          }
     }
   else if (plane->ext_state == E_OUTPUT_EXT_PRESENTATION)
     {
        if (!plane->wait_commit)
          {
             tdm_layer_unset_buffer(plane->tlayer);
             tdm_layer_commit(plane->tlayer, NULL, NULL);
          }
     }
   plane->ext_state = E_OUTPUT_EXT_NONE;

   DBG("e_plane_external_unset");
}

EINTERN Eina_Bool
e_plane_external_reset(E_Plane *plane, Eina_Rectangle *rect)
{
   Eina_Bool ret = EINA_FALSE;
   int w, h;

   EINA_SAFETY_ON_NULL_RETURN_VAL(plane, EINA_FALSE);

   DBG("e_plane_external_reset. state(%d) rect(%d,%d)(%d,%d)",
       plane->ext_state, rect->x, rect->y, rect->w, rect->h);

   if (!plane->tpp)
     {
        ERR("no created pp");
        return EINA_FALSE;
     }

   if (!plane->pp_tqueue)
     {
        ERR("no created pp_queue");
        return EINA_FALSE;
     }

   if (tbm_surface_queue_set_modes(plane->pp_tqueue, TBM_SURFACE_QUEUE_MODE_GUARANTEE_CYCLE) !=
       TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail queue set mode");
        return EINA_FALSE;
     }

   e_output_size_get(plane->output, &w, &h);
   if (tbm_surface_queue_reset(plane->pp_tqueue, w, h, TBM_FORMAT_ARGB8888) !=
       TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail queue reset");
        return EINA_FALSE;
     }

   plane->mirror_rect.x = rect->x;
   plane->mirror_rect.y = rect->y;
   plane->mirror_rect.w = rect->w;
   plane->mirror_rect.h = rect->h;

   if (plane->ext_state == E_OUTPUT_EXT_MIRROR)
     {
        ret = e_plane_zoom_set(plane, &plane->mirror_rect);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == EINA_TRUE, EINA_FALSE);
     }

   return EINA_TRUE;
}
