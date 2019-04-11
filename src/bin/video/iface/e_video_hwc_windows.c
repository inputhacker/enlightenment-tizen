#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "e_video_internal.h"
#include "e_video_hwc.h"

//#define DUMP_BUFFER

#define IFACE_ENTRY                                      \
   E_Video_Hwc_Windows *evhw;                              \
   evhw = container_of(iface, E_Video_Hwc_Windows, base)

typedef struct _E_Video_Hwc_Windows E_Video_Hwc_Windows;

struct _E_Video_Hwc_Windows
{
   E_Video_Comp_Iface base;

   E_Client *ec;
   Ecore_Window window;
   tdm_output *output;
   E_Output *e_output;
   E_Hwc_Window *hwc_window;
   E_Hwc *hwc;
   E_Client_Video_Info info;
   tbm_surface_h cur_tsurface; // tsurface to be set this layer.
   E_Client *e_client;
   Eina_List *ec_event_handler;

   /* input info */
   tbm_format tbmfmt;
   Eina_List *input_buffer_list;

   /* in screen coordinates */
   E_Video_Hwc_Geometry geo, old_geo;

   E_Comp_Wl_Buffer *old_comp_buffer;

   /* converter info */
   tbm_format pp_tbmfmt;
   tdm_pp *pp;
   Eina_Rectangle pp_r;    /* converter dst content rect */
   Eina_List *pp_buffer_list;
   Eina_List *next_buffer;
   Eina_Bool pp_scanout;

   int pp_align;
   int pp_minw, pp_minh, pp_maxw, pp_maxh;
   int video_align;

   /* When a video buffer be attached, it will be appended to the end of waiting_list .
    * And when it's committed, it will be moved to committed_list.
    * Finally when the commit handler is called, it will become current_fb.
    */
   Eina_List    *waiting_list;   /* buffers which are not committed yet */
   Eina_List    *committed_list; /* buffers which are committed, but not shown on screen yet */
   E_Comp_Wl_Video_Buf *current_fb;     /* buffer which is showing on screen currently */
   Eina_Bool     waiting_vblank;

   /* attributes */
   int tdm_mute_id;

   Eina_Bool  cb_registered;
   Eina_Bool  need_force_render;
   Eina_Bool  follow_topmost_visibility;
   Eina_Bool  allowed_attribute;

   E_Comp_Wl_Hook *hook_subsurf_create;
};

static void      _e_video_destroy(E_Video_Hwc_Windows *evhw);
static void      _e_video_render(E_Video_Hwc_Windows *evhw, const char *func);
static Eina_Bool _e_video_frame_buffer_show(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Video_Buf *vbuf);
static void      _e_video_vblank_handler(tdm_output *output, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void *user_data);

static Eina_Bool
_e_video_parent_is_viewable(E_Video_Hwc_Windows *evhw)
{
   E_Client *topmost_parent;

   if (e_object_is_del(E_OBJECT(evhw->ec))) return EINA_FALSE;

   topmost_parent = e_comp_wl_topmost_parent_get(evhw->ec);

   if (!topmost_parent)
     return EINA_FALSE;

   if (topmost_parent == evhw->ec)
     {
        VDB("There is no video parent surface", evhw->ec);
        return EINA_FALSE;
     }

   if (!topmost_parent->visible)
     {
        VDB("parent(0x%08"PRIxPTR") not viewable",
            evhw->ec, (Ecore_Window)e_client_util_win_get(topmost_parent));
        return EINA_FALSE;
     }

   if (!e_pixmap_resource_get(topmost_parent->pixmap))
     {
        VDB("parent(0x%08"PRIxPTR") no comp buffer",
            evhw->ec, (Ecore_Window)e_client_util_win_get(topmost_parent));
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_video_input_buffer_cb_free(E_Comp_Wl_Video_Buf *vbuf, void *data)
{
   E_Video_Hwc_Windows *evhw = data;
   Eina_Bool need_hide = EINA_FALSE;

   DBG("Buffer(%p) to be free, refcnt(%d)", vbuf, vbuf->ref_cnt);

   evhw->input_buffer_list = eina_list_remove(evhw->input_buffer_list, vbuf);

   if (vbuf->comp_buffer)
     e_comp_wl_buffer_reference(&vbuf->buffer_ref, NULL);

   if (evhw->current_fb == vbuf)
     {
        VIN("current fb destroyed", evhw->ec);
        e_comp_wl_video_buffer_set_use(evhw->current_fb, EINA_FALSE);
        evhw->current_fb = NULL;
        need_hide = EINA_TRUE;
     }

   if (eina_list_data_find(evhw->committed_list, vbuf))
     {
        VIN("committed fb destroyed", evhw->ec);
        evhw->committed_list = eina_list_remove(evhw->committed_list, vbuf);
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        need_hide = EINA_TRUE;
     }

   if (eina_list_data_find(evhw->waiting_list, vbuf))
     {
        VIN("waiting fb destroyed", evhw->ec);
        evhw->waiting_list = eina_list_remove(evhw->waiting_list, vbuf);
     }

   if (need_hide)
     _e_video_frame_buffer_show(evhw, NULL);
}

static Eina_Bool
_e_video_input_buffer_scanout_check(E_Comp_Wl_Video_Buf *vbuf)
{
   tbm_surface_h tbm_surface = NULL;
   tbm_bo bo = NULL;
   int flag;

   tbm_surface = vbuf->tbm_surface;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surface, EINA_FALSE);

   bo = tbm_surface_internal_get_bo(tbm_surface, 0);
   EINA_SAFETY_ON_NULL_RETURN_VAL(bo, EINA_FALSE);

   flag = tbm_bo_get_flags(bo);
   if (flag == TBM_BO_SCANOUT)
      return EINA_TRUE;

   return EINA_FALSE;
}

static E_Comp_Wl_Video_Buf *
_e_video_input_buffer_copy(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Buffer *comp_buf, E_Comp_Wl_Video_Buf *vbuf, Eina_Bool scanout)
{
   E_Comp_Wl_Video_Buf *temp = NULL;
   int aligned_width = ROUNDUP(vbuf->width_from_pitch, evhw->pp_align);

   temp = e_comp_wl_video_buffer_alloc(aligned_width, vbuf->height, vbuf->tbmfmt, scanout);
   EINA_SAFETY_ON_NULL_RETURN_VAL(temp, NULL);

   temp->comp_buffer = comp_buf;

   VDB("copy vbuf(%d,%dx%d) => vbuf(%d,%dx%d)", evhw->ec,
       MSTAMP(vbuf), vbuf->width_from_pitch, vbuf->height,
       MSTAMP(temp), temp->width_from_pitch, temp->height);

   e_comp_wl_video_buffer_copy(vbuf, temp);
   e_comp_wl_video_buffer_unref(vbuf);

   evhw->geo.input_w = vbuf->width_from_pitch;
#ifdef DUMP_BUFFER
   char file[256];
   static int i;
   snprintf(file, sizeof file, "/tmp/dump/%s_%d.png", "cpy", i++);
   tdm_helper_dump_buffer(temp->tbm_surface, file);
#endif

   return temp;
}

static E_Comp_Wl_Video_Buf *
_e_video_input_buffer_get(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Buffer *comp_buffer, Eina_Bool scanout)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_Bool need_pp_scanout = EINA_FALSE;

   vbuf = e_video_hwc_vbuf_find_with_comp_buffer(evhw->input_buffer_list, comp_buffer);
   if (vbuf)
     {
        vbuf->content_r = evhw->geo.input_r;
        return vbuf;
     }

   vbuf = e_comp_wl_video_buffer_create_comp(comp_buffer);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

   if (evhw->pp_scanout)
     {
        Eina_Bool input_buffer_scanout = EINA_FALSE;
        input_buffer_scanout = _e_video_input_buffer_scanout_check(vbuf);
        if (!input_buffer_scanout) need_pp_scanout = EINA_TRUE;
     }

   if (evhw->pp)
     {
        if ((evhw->pp_align != -1 && (vbuf->width_from_pitch % evhw->pp_align)) ||
            need_pp_scanout)
          {
             E_Comp_Wl_Video_Buf *temp;

             if (need_pp_scanout)
               temp = _e_video_input_buffer_copy(evhw, comp_buffer, vbuf, EINA_TRUE);
             else
               temp = _e_video_input_buffer_copy(evhw, comp_buffer, vbuf, scanout);
             if (!temp)
               {
                  e_comp_wl_video_buffer_unref(vbuf);
                  return NULL;
               }
             vbuf = temp;
          }
     }

   vbuf->content_r = evhw->geo.input_r;

   evhw->input_buffer_list = eina_list_append(evhw->input_buffer_list, vbuf);
   e_comp_wl_video_buffer_free_func_add(vbuf, _e_video_input_buffer_cb_free, evhw);

   DBG("Client(%s):PID(%d) RscID(%d), Buffer(%p) created, refcnt:%d"
       " scanout=%d", e_client_util_name_get(evhw->ec) ?: "No Name" ,
       evhw->ec->netwm.pid, wl_resource_get_id(evhw->ec->comp_data->surface), vbuf,
       vbuf->ref_cnt, scanout);

   return vbuf;
}

static void
_e_video_input_buffer_valid(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Buffer *comp_buffer)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;

   EINA_LIST_FOREACH(evhw->input_buffer_list, l, vbuf)
     {
        tbm_surface_h tbm_surf;
        tbm_bo bo;
        uint32_t size = 0, offset = 0, pitch = 0;

        if (!vbuf->comp_buffer) continue;
        if (vbuf->resource == comp_buffer->resource)
          {
             WRN("got wl_buffer@%d twice", wl_resource_get_id(comp_buffer->resource));
             return;
          }

        tbm_surf = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, comp_buffer->resource);
        bo = tbm_surface_internal_get_bo(tbm_surf, 0);
        tbm_surface_internal_get_plane_data(tbm_surf, 0, &size, &offset, &pitch);

        if (vbuf->names[0] == tbm_bo_export(bo) && vbuf->offsets[0] == offset)
          {
             WRN("can tearing: wl_buffer@%d, wl_buffer@%d are same. gem_name(%d)",
                 wl_resource_get_id(vbuf->resource),
                 wl_resource_get_id(comp_buffer->resource), vbuf->names[0]);
             return;
          }
     }
}

static void
_e_video_pp_buffer_cb_free(E_Comp_Wl_Video_Buf *vbuf, void *data)
{
   E_Video_Hwc_Windows *evhw = data;

   e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);

   if (evhw->current_fb == vbuf)
     evhw->current_fb = NULL;

   evhw->committed_list = eina_list_remove(evhw->committed_list, vbuf);

   evhw->waiting_list = eina_list_remove(evhw->waiting_list, vbuf);

   evhw->pp_buffer_list = eina_list_remove(evhw->pp_buffer_list, vbuf);
}

static E_Comp_Wl_Video_Buf *
_e_video_pp_buffer_get(E_Video_Hwc_Windows *evhw, int width, int height)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;
   int i = 0;
   int aligned_width;

   if (evhw->video_align != -1)
     aligned_width = ROUNDUP(width, evhw->video_align);
   else
     aligned_width = width;

   if (evhw->pp_buffer_list)
     {
        vbuf = eina_list_data_get(evhw->pp_buffer_list);
        EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

        /* if we need bigger pp_buffers, destroy all pp_buffers and create */
        if (aligned_width > vbuf->width_from_pitch || height != vbuf->height)
          {
             Eina_List *ll;

             VIN("pp buffer changed: %dx%d => %dx%d", evhw->ec,
                 vbuf->width_from_pitch, vbuf->height,
                 aligned_width, height);

             EINA_LIST_FOREACH_SAFE(evhw->pp_buffer_list, l, ll, vbuf)
               {
                  /* free forcely */
                  e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
                  e_comp_wl_video_buffer_unref(vbuf);
               }
             if (evhw->pp_buffer_list)
               NEVER_GET_HERE();

             if (evhw->waiting_list)
               NEVER_GET_HERE();
          }
     }

   if (!evhw->pp_buffer_list)
     {
        for (i = 0; i < BUFFER_MAX_COUNT; i++)
          {
             vbuf = e_comp_wl_video_buffer_alloc(aligned_width, height, evhw->pp_tbmfmt, EINA_TRUE);
             EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

             e_comp_wl_video_buffer_free_func_add(vbuf, _e_video_pp_buffer_cb_free, evhw);
             evhw->pp_buffer_list = eina_list_append(evhw->pp_buffer_list, vbuf);

          }

        VIN("pp buffer created: %dx%d, %c%c%c%c", evhw->ec,
            vbuf->width_from_pitch, height, FOURCC_STR(evhw->pp_tbmfmt));

        evhw->next_buffer = evhw->pp_buffer_list;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(evhw->pp_buffer_list, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhw->next_buffer, NULL);

   l = evhw->next_buffer;
   while ((vbuf = evhw->next_buffer->data))
     {
        evhw->next_buffer = (evhw->next_buffer->next) ? evhw->next_buffer->next : evhw->pp_buffer_list;

        if (!vbuf->in_use)
          return vbuf;

        if (l == evhw->next_buffer)
          {
             VWR("all video framebuffers in use (max:%d)", evhw->ec, BUFFER_MAX_COUNT);
             return NULL;
          }
     }

   return NULL;
}

static void
_e_video_format_info_get(E_Video_Hwc_Windows *evhw)
{
   E_Comp_Wl_Buffer *comp_buffer;
   tbm_surface_h tbm_surf;

   comp_buffer = e_pixmap_resource_get(evhw->ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN(comp_buffer);

   tbm_surf = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, comp_buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN(tbm_surf);

   evhw->tbmfmt = tbm_surface_get_format(tbm_surf);
}

static Eina_Bool
_e_video_can_commit(E_Video_Hwc_Windows *evhw)
{
   if (e_output_dpms_get(evhw->e_output))
     return EINA_FALSE;

   if (!e_video_hwc_client_visible_get(evhw->ec))
     return EINA_FALSE;

   return EINA_TRUE;
}

static void
_e_video_commit_handler(tdm_layer *layer, unsigned int sequence,
                        unsigned int tv_sec, unsigned int tv_usec,
                        void *user_data)
{
   E_Video_Hwc_Windows *evhw;
   Eina_List *l;
   E_Comp_Wl_Video_Buf *vbuf;

   evhw = user_data;
   if (!evhw) return;
   if (!evhw->committed_list) return;

   if (_e_video_can_commit(evhw))
     {
        tbm_surface_h displaying_buffer = evhw->cur_tsurface;

        EINA_LIST_FOREACH(evhw->committed_list, l, vbuf)
          {
             if (vbuf->tbm_surface == displaying_buffer) break;
          }
        if (!vbuf) return;
     }
   else
     vbuf = eina_list_nth(evhw->committed_list, 0);

   evhw->committed_list = eina_list_remove(evhw->committed_list, vbuf);

   /* client can attachs the same wl_buffer twice. */
   if (evhw->current_fb && VBUF_IS_VALID(evhw->current_fb) && vbuf != evhw->current_fb)
     {
        e_comp_wl_video_buffer_set_use(evhw->current_fb, EINA_FALSE);

        if (evhw->current_fb->comp_buffer)
          e_comp_wl_buffer_reference(&evhw->current_fb->buffer_ref, NULL);
     }

   evhw->current_fb = vbuf;

   VDB("current_fb(%d)", evhw->ec, MSTAMP(evhw->current_fb));

   _e_video_vblank_handler(NULL, sequence, tv_sec, tv_usec, evhw);
}

static void
_e_video_commit_buffer(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Video_Buf *vbuf)
{
   evhw->committed_list = eina_list_append(evhw->committed_list, vbuf);

   if (!_e_video_can_commit(evhw))
     goto no_commit;

   if (!_e_video_frame_buffer_show(evhw, vbuf))
     goto no_commit;

   return;

no_commit:
   _e_video_commit_handler(NULL, 0, 0, 0, evhw);
   _e_video_vblank_handler(NULL, 0, 0, 0, evhw);
}

static void
_e_video_commit_from_waiting_list(E_Video_Hwc_Windows *evhw)
{
   E_Comp_Wl_Video_Buf *vbuf;

   vbuf = eina_list_nth(evhw->waiting_list, 0);
   evhw->waiting_list = eina_list_remove(evhw->waiting_list, vbuf);

   _e_video_commit_buffer(evhw, vbuf);
}

static void
_e_video_vblank_handler(tdm_output *output, unsigned int sequence,
                        unsigned int tv_sec, unsigned int tv_usec,
                        void *user_data)
{
   E_Video_Hwc_Windows *evhw;

   evhw = user_data;
   if (!evhw) return;

   evhw->waiting_vblank = EINA_FALSE;

   if (evhw->waiting_list)
     _e_video_commit_from_waiting_list(evhw);
}

static Eina_Bool
_e_video_frame_buffer_show(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Video_Buf *vbuf)
{
   /* show means that set the information of the buffer and the info of the hwc window */

   if (!vbuf) return EINA_TRUE;

   CLEAR(evhw->info);
   evhw->info.src_config.size.h = vbuf->width_from_pitch;
   evhw->info.src_config.size.v = vbuf->height_from_size;
   evhw->info.src_config.pos.x = vbuf->content_r.x;
   evhw->info.src_config.pos.y = vbuf->content_r.y;
   evhw->info.src_config.pos.w = vbuf->content_r.w;
   evhw->info.src_config.pos.h = vbuf->content_r.h;
   evhw->info.src_config.format = vbuf->tbmfmt;
   evhw->info.dst_pos.x = evhw->geo.tdm.output_r.x;
   evhw->info.dst_pos.y = evhw->geo.tdm.output_r.y;
   evhw->info.dst_pos.w = evhw->geo.tdm.output_r.w;
   evhw->info.dst_pos.h = evhw->geo.tdm.output_r.h;
   evhw->info.transform = vbuf->content_t;

   evhw->cur_tsurface = vbuf->tbm_surface;

   evhw->waiting_vblank = EINA_TRUE;

   // TODO:: this logic move to the hwc windows after hwc commit
#if 1
   E_Client *topmost;

   topmost = e_comp_wl_topmost_parent_get(evhw->ec);
   if (topmost && topmost->argb && !e_comp_object_mask_has(evhw->ec->frame))
     {
        Eina_Bool do_punch = EINA_TRUE;

        /* FIXME: the mask obj can be drawn at the wrong position in the beginnig
         * time. It happens caused by window manager policy.
         */
        if ((topmost->fullscreen || topmost->maximized) &&
            (evhw->geo.output_r.x == 0 || evhw->geo.output_r.y == 0))
          {
             int bw, bh;

             e_pixmap_size_get(topmost->pixmap, &bw, &bh);

             if (bw > 100 && bh > 100 &&
                 evhw->geo.output_r.w < 100 && evhw->geo.output_r.h < 100)
               {
                  VIN("don't punch. (%dx%d, %dx%d)", evhw->ec,
                      bw, bh, evhw->geo.output_r.w, evhw->geo.output_r.h);
                  do_punch = EINA_FALSE;
               }
          }

        if (do_punch)
          {
             e_comp_object_mask_set(evhw->ec->frame, EINA_TRUE);
             VIN("punched", evhw->ec);
          }
     }

   if (e_video_debug_punch_value_get())
     {
        e_comp_object_mask_set(evhw->ec->frame, EINA_TRUE);
        VIN("punched", evhw->ec);
     }
#endif

   DBG("Client(%s):PID(%d) RscID(%d), Buffer(%p, refcnt:%d) is shown."
       "Geometry details are : buffer size(%dx%d) src(%d,%d, %dx%d)"
       " dst(%d,%d, %dx%d), transform(%d)",
       e_client_util_name_get(evhw->ec) ?: "No Name" , evhw->ec->netwm.pid,
       wl_resource_get_id(evhw->ec->comp_data->surface), vbuf, vbuf->ref_cnt,
       evhw->info.src_config.size.h, evhw->info.src_config.size.v, evhw->info.src_config.pos.x,
       evhw->info.src_config.pos.y, evhw->info.src_config.pos.w, evhw->info.src_config.pos.h,
       evhw->info.dst_pos.x, evhw->info.dst_pos.y, evhw->info.dst_pos.w, evhw->info.dst_pos.h, evhw->info.transform);


   return EINA_TRUE;
}

static void
_e_video_buffer_show(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Video_Buf *vbuf, unsigned int transform)
{
   vbuf->content_t = transform;

   e_comp_wl_video_buffer_set_use(vbuf, EINA_TRUE);

   if (vbuf->comp_buffer)
     e_comp_wl_buffer_reference(&vbuf->buffer_ref, vbuf->comp_buffer);

   if (evhw->waiting_vblank)
     {
        evhw->waiting_list = eina_list_append(evhw->waiting_list, vbuf);
        VDB("There are waiting fbs more than 1", evhw->ec);
        return;
     }

   _e_video_commit_buffer(evhw, vbuf);
}

static void
_e_video_cb_evas_resize(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Windows *evhw = data;

   if (e_video_hwc_geometry_map_apply(evhw->ec, &evhw->geo))
     _e_video_render(evhw, __FUNCTION__);
}

static void
_e_video_cb_evas_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Windows *evhw = data;

   if (e_video_hwc_geometry_map_apply(evhw->ec, &evhw->geo))
     _e_video_render(evhw, __FUNCTION__);
}

static void
_e_video_cb_evas_show(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Windows *evhw = data;

   if (e_object_is_del(E_OBJECT(evhw->ec))) return;

   if (!evhw->ec->comp_data->video_client)
     return;

   if (evhw->need_force_render)
     {
        VIN("video forcely rendering..", evhw->ec);
        _e_video_render(evhw, __FUNCTION__);
     }

   /* if stand_alone is true, not show */
   if ((evhw->ec->comp_data->sub.data && evhw->ec->comp_data->sub.data->stand_alone) ||
       (evhw->ec->comp_data->sub.data && evhw->follow_topmost_visibility))
     {
        return;
     }

   VIN("evas show", evhw->ec);
   if (evhw->current_fb)
     _e_video_buffer_show(evhw, evhw->current_fb, evhw->current_fb->content_t);
}

static E_Video_Hwc_Windows *
_e_video_create(E_Client *ec)
{
   E_Video_Hwc_Windows *evhw;
   E_Output *e_output;
   E_Hwc *hwc;
   E_Hwc_Window *hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->zone, EINA_FALSE);

   e_output = e_output_find(ec->zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_output, EINA_FALSE);
   hwc = e_output->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);
   hwc_window = e_hwc_window_new(hwc, ec, E_HWC_WINDOW_STATE_VIDEO);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   evhw = calloc(1, sizeof *evhw);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhw, NULL);

   evhw->ec = ec;
   evhw->pp_align = -1;
   evhw->video_align = -1;
   evhw->e_output = e_output;
   evhw->tdm_mute_id = -1;
   evhw->window = e_client_util_win_get(ec);
   evhw->hwc_window = hwc_window;
   evhw->hwc = hwc;

   /* This ec is a video client now. */
   VIN("video client", evhw->ec);
   ec->comp_data->video_client = 1;

   //TODO: shoud this function be called here?
   e_zone_video_available_size_get(ec->zone, NULL, NULL, NULL, NULL, &evhw->video_align);

   VIN("create. wl_surface@%d", ec, wl_resource_get_id(evhw->ec->comp_data->surface));

   return evhw;
}

static void
_e_video_hide(E_Video_Hwc_Windows *evhw)
{
   E_Comp_Wl_Video_Buf *vbuf;

   if (evhw->current_fb || evhw->committed_list)
     _e_video_frame_buffer_show(evhw, NULL);

   if (evhw->current_fb)
     {
        e_comp_wl_video_buffer_set_use(evhw->current_fb, EINA_FALSE);
        evhw->current_fb = NULL;
     }

   if (evhw->old_comp_buffer)
     evhw->old_comp_buffer = NULL;

   EINA_LIST_FREE(evhw->committed_list, vbuf)
      e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);

   EINA_LIST_FREE(evhw->waiting_list, vbuf)
      e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
}

static void
_e_video_destroy(E_Video_Hwc_Windows *evhw)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l = NULL, *ll = NULL;

   if (!evhw)
     return;

   VIN("destroy", evhw->ec);

   if (evhw->cb_registered)
     {
        evas_object_event_callback_del_full(evhw->ec->frame, EVAS_CALLBACK_RESIZE,
                                            _e_video_cb_evas_resize, evhw);
        evas_object_event_callback_del_full(evhw->ec->frame, EVAS_CALLBACK_MOVE,
                                            _e_video_cb_evas_move, evhw);
     }

   _e_video_hide(evhw);

   /* others */
   EINA_LIST_FOREACH_SAFE(evhw->input_buffer_list, l, ll, vbuf)
     {
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        e_comp_wl_video_buffer_unref(vbuf);
     }

   EINA_LIST_FOREACH_SAFE(evhw->pp_buffer_list, l, ll, vbuf)
     {
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        e_comp_wl_video_buffer_unref(vbuf);
     }

   if (evhw->input_buffer_list)
     NEVER_GET_HERE();
   if (evhw->pp_buffer_list)
     NEVER_GET_HERE();

   /* destroy converter second */
   if (evhw->pp)
     tdm_pp_destroy(evhw->pp);

   e_hwc_window_free(evhw->hwc_window);

   free(evhw);

#if 0
   if (e_comp_wl_video_buffer_list_length() > 0)
     e_comp_wl_video_buffer_list_print(NULL);
#endif
}

static Eina_Bool
_e_video_check_if_pp_needed(E_Video_Hwc_Windows *evhw)
{
   int i, count = 0;
   const tbm_format *formats;
   Eina_Bool found = EINA_FALSE;
   E_Hwc *hwc = evhw->hwc;

   if (hwc->tdm_hwc_video_stream)
     return EINA_FALSE;

   if (!e_comp_screen_available_video_formats_get(&formats, &count))
     return EINA_FALSE;

   for (i = 0; i < count; i++)
     if (formats[i] == evhw->tbmfmt)
       {
          found = EINA_TRUE;
          break;
       }

   if (!found)
     {
        if (formats && count > 0)
          evhw->pp_tbmfmt = formats[0];
        else
          {
             WRN("No layer format information!!!");
             evhw->pp_tbmfmt = TBM_FORMAT_ARGB8888;
          }
        return EINA_TRUE;
     }

   if (hwc->tdm_hwc_video_scanout)
     goto need_pp;

   /* check size */
   if (evhw->geo.input_r.w != evhw->geo.output_r.w || evhw->geo.input_r.h != evhw->geo.output_r.h)
     if (!hwc->tdm_hwc_video_scale)
       goto need_pp;

   /* check rotate */
   if (evhw->geo.transform || e_comp->e_comp_screen->rotation > 0)
     if (!hwc->tdm_hwc_video_transform)
       goto need_pp;

   return EINA_FALSE;

need_pp:
   evhw->pp_tbmfmt = evhw->tbmfmt;

   return EINA_TRUE;
}

static void
_e_video_pp_cb_done(tdm_pp *pp, tbm_surface_h sb, tbm_surface_h db, void *user_data)
{
   E_Video_Hwc_Windows *evhw = (E_Video_Hwc_Windows*)user_data;
   E_Comp_Wl_Video_Buf *input_buffer, *pp_buffer;

   input_buffer = e_video_hwc_vbuf_find(evhw->input_buffer_list, sb);
   if (input_buffer)
     e_comp_wl_video_buffer_unref(input_buffer);

   pp_buffer = e_video_hwc_vbuf_find(evhw->pp_buffer_list, db);
   if (pp_buffer)
     {
        e_comp_wl_video_buffer_set_use(pp_buffer, EINA_FALSE);
        if (!e_video_hwc_client_visible_get(evhw->ec)) return;

        _e_video_buffer_show(evhw, pp_buffer, 0);
     }
   else
     {
        VER("There is no pp_buffer", evhw->ec);
        // there is no way to set in_use flag.
        // This will cause issue when server get available pp_buffer.
     }
}

static void
_e_video_render(E_Video_Hwc_Windows *evhw, const char *func)
{
   E_Comp_Wl_Buffer *comp_buffer;
   E_Comp_Wl_Video_Buf *pp_buffer = NULL;
   E_Comp_Wl_Video_Buf *input_buffer = NULL;
   E_Client *topmost;

   EINA_SAFETY_ON_NULL_RETURN(evhw->ec);

   /* buffer can be NULL when camera/video's mode changed. Do nothing and
    * keep previous frame in this case.
    */
   if (!evhw->ec->pixmap)
     return;

   if (!e_video_hwc_client_visible_get(evhw->ec))
     {
        _e_video_hide(evhw);
        return;
     }

   comp_buffer = e_pixmap_resource_get(evhw->ec->pixmap);
   if (!comp_buffer) return;

   _e_video_format_info_get(evhw);

   /* not interested with other buffer type */
   if (!wayland_tbm_server_get_surface(NULL, comp_buffer->resource))
     return;

   topmost = e_comp_wl_topmost_parent_get(evhw->ec);
   EINA_SAFETY_ON_NULL_RETURN(topmost);

   if(e_comp_wl_viewport_is_changed(topmost))
     {
        VIN("need update viewport: apply topmost", evhw->ec);
        e_comp_wl_viewport_apply(topmost);
     }

   if (!e_video_hwc_geometry_get(evhw->ec, &evhw->geo))
     {
        if(!evhw->need_force_render && !_e_video_parent_is_viewable(evhw))
          {
             VIN("need force render", evhw->ec);
             evhw->need_force_render = EINA_TRUE;
          }
        return;
     }

   DBG("====================================== (%s)", func);
   VDB("old: "GEO_FMT" buf(%p)",
       evhw->ec, GEO_ARG(&evhw->old_geo), evhw->old_comp_buffer);
   VDB("new: "GEO_FMT" buf(%p) %c%c%c%c",
       evhw->ec, GEO_ARG(&evhw->geo), comp_buffer, FOURCC_STR(evhw->tbmfmt));

   if (!memcmp(&evhw->old_geo, &evhw->geo, sizeof evhw->geo) &&
       evhw->old_comp_buffer == comp_buffer)
     return;

   evhw->need_force_render = EINA_FALSE;

   _e_video_input_buffer_valid(evhw, comp_buffer);

   if (!_e_video_check_if_pp_needed(evhw))
     {
        /* 1. non converting case */
        input_buffer = _e_video_input_buffer_get(evhw, comp_buffer, EINA_TRUE);
        EINA_SAFETY_ON_NULL_GOTO(input_buffer, render_fail);

        _e_video_buffer_show(evhw, input_buffer, evhw->geo.tdm.transform);

        evhw->old_geo = evhw->geo;
        evhw->old_comp_buffer = comp_buffer;

        goto done;
     }

   /* 2. converting case */
   if (!evhw->pp)
     {
        tdm_pp_capability pp_cap;
        tdm_error error = TDM_ERROR_NONE;

        evhw->pp = tdm_display_create_pp(e_comp->e_comp_screen->tdisplay, NULL);
        EINA_SAFETY_ON_NULL_GOTO(evhw->pp, render_fail);

        tdm_display_get_pp_available_size(e_comp->e_comp_screen->tdisplay, &evhw->pp_minw, &evhw->pp_minh,
                                          &evhw->pp_maxw, &evhw->pp_maxh, &evhw->pp_align);

        error = tdm_display_get_pp_capabilities(e_comp->e_comp_screen->tdisplay, &pp_cap);
        if (error == TDM_ERROR_NONE)
          {
             if (pp_cap & TDM_PP_CAPABILITY_SCANOUT)
               evhw->pp_scanout = EINA_TRUE;
          }
     }

   if ((evhw->pp_minw > 0 && (evhw->geo.input_r.w < evhw->pp_minw || evhw->geo.tdm.output_r.w < evhw->pp_minw)) ||
       (evhw->pp_minh > 0 && (evhw->geo.input_r.h < evhw->pp_minh || evhw->geo.tdm.output_r.h < evhw->pp_minh)) ||
       (evhw->pp_maxw > 0 && (evhw->geo.input_r.w > evhw->pp_maxw || evhw->geo.tdm.output_r.w > evhw->pp_maxw)) ||
       (evhw->pp_maxh > 0 && (evhw->geo.input_r.h > evhw->pp_maxh || evhw->geo.tdm.output_r.h > evhw->pp_maxh)))
     {
        INF("size(%dx%d, %dx%d) is out of PP range",
            evhw->geo.input_r.w, evhw->geo.input_r.h, evhw->geo.tdm.output_r.w, evhw->geo.tdm.output_r.h);
        goto done;
     }

   input_buffer = _e_video_input_buffer_get(evhw, comp_buffer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_GOTO(input_buffer, render_fail);

   pp_buffer = _e_video_pp_buffer_get(evhw, evhw->geo.tdm.output_r.w, evhw->geo.tdm.output_r.h);
   EINA_SAFETY_ON_NULL_GOTO(pp_buffer, render_fail);

   if (memcmp(&evhw->old_geo, &evhw->geo, sizeof evhw->geo))
     {
        tdm_info_pp info;

        CLEAR(info);
        info.src_config.size.h = input_buffer->width_from_pitch;
        info.src_config.size.v = input_buffer->height_from_size;
        info.src_config.pos.x = evhw->geo.input_r.x;
        info.src_config.pos.y = evhw->geo.input_r.y;
        info.src_config.pos.w = evhw->geo.input_r.w;
        info.src_config.pos.h = evhw->geo.input_r.h;
        info.src_config.format = evhw->tbmfmt;
        info.dst_config.size.h = pp_buffer->width_from_pitch;
        info.dst_config.size.v = pp_buffer->height_from_size;
        info.dst_config.pos.w = evhw->geo.tdm.output_r.w;
        info.dst_config.pos.h = evhw->geo.tdm.output_r.h;
        info.dst_config.format = evhw->pp_tbmfmt;
        info.transform = evhw->geo.tdm.transform;

        if (tdm_pp_set_info(evhw->pp, &info))
          {
             VER("tdm_pp_set_info() failed", evhw->ec);
             goto render_fail;
          }

        if (tdm_pp_set_done_handler(evhw->pp, _e_video_pp_cb_done, evhw))
          {
             VER("tdm_pp_set_done_handler() failed", evhw->ec);
             goto render_fail;
          }

        CLEAR(evhw->pp_r);
        evhw->pp_r.w = info.dst_config.pos.w;
        evhw->pp_r.h = info.dst_config.pos.h;
     }

   pp_buffer->content_r = evhw->pp_r;

   if (tdm_pp_attach(evhw->pp, input_buffer->tbm_surface, pp_buffer->tbm_surface))
     {
        VER("tdm_pp_attach() failed", evhw->ec);
        goto render_fail;
     }

   e_comp_wl_video_buffer_set_use(pp_buffer, EINA_TRUE);

   e_comp_wl_buffer_reference(&input_buffer->buffer_ref, comp_buffer);

   if (tdm_pp_commit(evhw->pp))
     {
        VER("tdm_pp_commit() failed", evhw->ec);
        e_comp_wl_video_buffer_set_use(pp_buffer, EINA_FALSE);
        goto render_fail;
     }

   evhw->old_geo = evhw->geo;
   evhw->old_comp_buffer = comp_buffer;

   goto done;

render_fail:
   if (input_buffer)
     e_comp_wl_video_buffer_unref(input_buffer);

done:
   if (!evhw->cb_registered)
     {
        evas_object_event_callback_add(evhw->ec->frame, EVAS_CALLBACK_RESIZE,
                                       _e_video_cb_evas_resize, evhw);
        evas_object_event_callback_add(evhw->ec->frame, EVAS_CALLBACK_MOVE,
                                       _e_video_cb_evas_move, evhw);
        evhw->cb_registered = EINA_TRUE;
     }
   DBG("======================================.");
}

static Eina_Bool
_e_video_cb_ec_buffer_change(void *data, int type, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;
   E_Video_Hwc_Windows *evhw;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   evhw = data;
   ec = ev->ec;

   if (evhw->ec != ec)
     return ECORE_CALLBACK_PASS_ON;

   if (e_object_is_del(E_OBJECT(ec)))
     return ECORE_CALLBACK_PASS_ON;

   /* not interested with non video_surface */
   if (!evhw->ec->comp_data->video_client)
     return ECORE_CALLBACK_PASS_ON;

   _e_video_render(evhw, __FUNCTION__);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_cb_ec_client_show(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   E_Client *video_ec = NULL;
   E_Video_Hwc_Windows *evhw = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (!ec->comp_data) return ECORE_CALLBACK_PASS_ON;

   video_ec = e_video_hwc_child_client_get(ec);
   if (!video_ec) return ECORE_CALLBACK_PASS_ON;

   evhw = data;
   if (!evhw) return ECORE_CALLBACK_PASS_ON;

   VIN("show: find video child(0x%08"PRIxPTR")",
       evhw->ec, (Ecore_Window)e_client_util_win_get(video_ec));
   if(evhw->old_comp_buffer)
     {
        VIN("video already rendering..", evhw->ec);
        return ECORE_CALLBACK_PASS_ON;
     }

   if (ec == e_comp_wl_topmost_parent_get(evhw->ec))
     {
        VIN("video need rendering..", evhw->ec);
        e_comp_wl_viewport_apply(ec);
        _e_video_render(evhw, __FUNCTION__);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_cb_ec_visibility_change(void *data, int type, void *event)
{
   E_Event_Remote_Surface_Provider *ev;
   E_Client *ec, *offscreen_parent;
   E_Video_Hwc_Windows *evhw;

   evhw = data;
   offscreen_parent = e_video_hwc_client_offscreen_parent_get(evhw->ec);
   if (!offscreen_parent)
     goto end;

   ev = event;
   ec = ev->ec;
   if (offscreen_parent != ec)
     goto end;

   switch (ec->visibility.obscured)
   {
       case E_VISIBILITY_FULLY_OBSCURED:
           evas_object_hide(evhw->ec->frame);
           break;
       case E_VISIBILITY_UNOBSCURED:
           evas_object_show(evhw->ec->frame);
           break;
       default:
           VER("Not implemented", evhw->ec);
           return ECORE_CALLBACK_PASS_ON;
   }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_video_ec_visibility_event_free(void *d EINA_UNUSED, E_Event_Client *ev)
{
   e_object_unref(E_OBJECT(ev->ec));
   free(ev);
}

static void
_e_video_ec_visibility_event_send(E_Client *ec)
{
   E_Event_Client *ev;
   int obscured;

   obscured = ec->visibility.obscured;
   ELOGF("VIDEO <INF>", "Signal visibility change event of video, type %d",
         ec, obscured);

   ev = E_NEW(E_Event_Client, 1);
   if (!ev) return;
   ev->ec = ec;
   e_object_ref(E_OBJECT(ec));
   ecore_event_add(E_EVENT_CLIENT_VISIBILITY_CHANGE, ev,
                   (Ecore_End_Cb)_e_video_ec_visibility_event_free, NULL);
}

static void
_e_video_hwc_windows_ec_visibility_set(E_Client *ec, E_Visibility vis)
{
   if (ec->visibility.obscured == vis)
     return;

   ec->visibility.obscured = vis;
   _e_video_ec_visibility_event_send(ec);
}

static Eina_Bool
_e_video_cb_topmost_ec_visibility_change(void *data, int type, void *event)
{
   E_Video_Hwc_Windows *evhw;
   E_Event_Client *ev;
   E_Client *topmost;

   ev = event;
   evhw = data;
   if (!evhw->follow_topmost_visibility)
       goto end;

   topmost = e_comp_wl_topmost_parent_get(evhw->ec);
   if (!topmost) goto end;
   if (topmost != ev->ec) goto end;
   if (topmost == evhw->ec) goto end;

   /* Update visibility of video client by changing visibility of topmost client */
   _e_video_hwc_windows_ec_visibility_set(evhw->ec, topmost->visibility.obscured);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_video_hwc_windows_ec_event_deinit(E_Video_Hwc_Windows *evhw)
{
   E_Client *ec;

   ec = evhw->ec;

   evas_object_event_callback_del_full(ec->frame, EVAS_CALLBACK_SHOW,
                                       _e_video_cb_evas_show, evhw);

   E_FREE_FUNC(evhw->hook_subsurf_create, e_comp_wl_hook_del);
   E_FREE_LIST(evhw->ec_event_handler, ecore_event_handler_del);
}

const char *
_e_video_hwc_windows_prop_name_get_by_id(E_Video_Hwc_Windows *evhw, unsigned int id)
{
   const tdm_prop *props;
   int i, count = 0;

   e_hwc_windows_get_video_available_properties(evhw->hwc, &props, &count);
   for (i = 0; i < count; i++)
     {
        if (props[i].id == id)
          {
             VDB("check property(%s)", evhw->ec, props[i].name);
             return props[i].name;
          }
     }

   VER("No available property: id %d", evhw->ec, id);

   return NULL;
}

static void
_e_video_hwc_windows_cb_hook_subsurface_create(void *data, E_Client *ec)
{
   E_Video_Hwc_Windows *evhw;
   E_Client *topmost1, *topmost2;

   evhw = data;
   if (!evhw->follow_topmost_visibility)
     return;

   /* This is to raise an 'VISIBILITY_CHANGE' event to video client when its
    * topmost ancestor is changed. The reason why it uses hook handler of
    * creation of subsurface is that there is no event for like parent change,
    * and being created subsurface that has common topmost parent means
    * it implies topmost parent has been possibly changed. */
   topmost1 = e_comp_wl_topmost_parent_get(ec);
   topmost2 = e_comp_wl_topmost_parent_get(evhw->ec);
   if (topmost1 && topmost2)
     {
        if (topmost1 == topmost2)
          _e_video_hwc_windows_ec_visibility_set(evhw->ec, topmost1->visibility.obscured);
     }
}

static void
_e_video_hwc_windows_ec_event_init(E_Video_Hwc_Windows *evhw)
{
   E_Client *ec;

   ec = evhw->ec;

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW,
                                  _e_video_cb_evas_show, evhw);

   evhw->hook_subsurf_create =
      e_comp_wl_hook_add(E_COMP_WL_HOOK_SUBSURFACE_CREATE,
                         _e_video_hwc_windows_cb_hook_subsurface_create, evhw);

   E_LIST_HANDLER_APPEND(evhw->ec_event_handler, E_EVENT_CLIENT_BUFFER_CHANGE,
                         _e_video_cb_ec_buffer_change, evhw);
   E_LIST_HANDLER_APPEND(evhw->ec_event_handler, E_EVENT_CLIENT_SHOW,
                         _e_video_cb_ec_client_show, evhw);
   E_LIST_HANDLER_APPEND(evhw->ec_event_handler, E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE,
                         _e_video_cb_ec_visibility_change, evhw);
   E_LIST_HANDLER_APPEND(evhw->ec_event_handler, E_EVENT_CLIENT_VISIBILITY_CHANGE,
                         _e_video_cb_topmost_ec_visibility_change, evhw);
}

static void
_e_video_hwc_windows_iface_destroy(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   _e_video_hwc_windows_ec_event_deinit(evhw);
   _e_video_destroy(evhw);
}

static Eina_Bool
_e_video_hwc_windows_iface_follow_topmost_visibility(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhw->follow_topmost_visibility = EINA_TRUE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_unfollow_topmost_visibility(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhw->follow_topmost_visibility = EINA_FALSE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_allowed_property(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhw->allowed_attribute = EINA_TRUE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_disallowed_property(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhw->allowed_attribute = EINA_FALSE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_property_get(E_Video_Comp_Iface *iface, unsigned int id, tdm_value *value)
{
   tdm_error ret;

   IFACE_ENTRY;

   ret = tdm_hwc_window_get_property(evhw->hwc_window->thwc_window, id, value);
   if (ret != TDM_ERROR_NONE)
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_property_set(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value)
{
   const char *name;

   IFACE_ENTRY;

   VIN("set_attribute", evhw->ec);

   name = _e_video_hwc_windows_prop_name_get_by_id(evhw, id);
   if (!name)
   {
      VER("_e_video_hwc_windows_prop_name_get_by_id failed", evhw->ec);
      return EINA_FALSE;
   }

   if (evhw->allowed_attribute)
     {
        VIN("set_attribute now : property(%s), value(%d)", evhw->ec, name, value.u32);

        /* set the property on the fly */
        if (!e_hwc_window_set_property(evhw->hwc_window, id, name, value, EINA_TRUE))
          {
             VER("set property failed", evhw->ec);
             return EINA_FALSE;
          }
     }
   else
     {
        VIN("set_attribute at commit : property(%s), value(%d)", evhw->ec, name, value.u32);

        /* set the property before hwc commit */
        if (!e_hwc_window_set_property(evhw->hwc_window, id, name, value, EINA_FALSE))
          {
             VER("set property failed", evhw->ec);
             return EINA_FALSE;
          }
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_available_properties_get(E_Video_Comp_Iface *iface, const tdm_prop **props, int *count)
{
   IFACE_ENTRY;

   if (!e_hwc_windows_get_video_available_properties(evhw->hwc, props, count))
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_info_get(E_Video_Comp_Iface *iface, E_Client_Video_Info *info)
{
   IFACE_ENTRY;

   memcpy(&info->src_config, &evhw->info.src_config, sizeof(tdm_info_config));
   memcpy(&info->dst_pos, &evhw->info.dst_pos, sizeof(tdm_pos));
   info->transform = evhw->info.transform;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_commit_data_release(E_Video_Comp_Iface *iface, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec)
{
   IFACE_ENTRY;

   _e_video_commit_handler(NULL, sequence, tv_sec, tv_usec, evhw);

   return EINA_TRUE;
}

static tbm_surface_h
_e_video_hwc_windows_iface_tbm_surface_get(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   return evhw->cur_tsurface;
}

EINTERN E_Video_Comp_Iface *
e_video_hwc_windows_iface_create(E_Client *ec)
{
   E_Video_Hwc_Windows *evhw;

   INF("Initializing HWC Windows mode");

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   evhw = _e_video_create(ec);
   if (!evhw)
     {
        ERR("Failed to create 'E_Video_Hwc_Windows'");
        return NULL;
     }

   _e_video_hwc_windows_ec_event_init(evhw);

   evhw->base.destroy = _e_video_hwc_windows_iface_destroy;
   evhw->base.follow_topmost_visibility = _e_video_hwc_windows_iface_follow_topmost_visibility;
   evhw->base.unfollow_topmost_visibility = _e_video_hwc_windows_iface_unfollow_topmost_visibility;
   evhw->base.allowed_property = _e_video_hwc_windows_iface_allowed_property;
   evhw->base.disallowed_property = _e_video_hwc_windows_iface_disallowed_property;
   evhw->base.property_get = _e_video_hwc_windows_iface_property_get;
   evhw->base.property_set = _e_video_hwc_windows_iface_property_set;
   evhw->base.property_delay_set = NULL;
   evhw->base.available_properties_get = _e_video_hwc_windows_iface_available_properties_get;
   evhw->base.info_get = _e_video_hwc_windows_iface_info_get;
   evhw->base.commit_data_release = _e_video_hwc_windows_iface_commit_data_release;
   evhw->base.tbm_surface_get = _e_video_hwc_windows_iface_tbm_surface_get;

   return &evhw->base;
}
