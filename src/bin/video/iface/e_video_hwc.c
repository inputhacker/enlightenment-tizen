#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "e_video_internal.h"
#include "e_video_hwc.h"
#include <wayland-tbm-server.h>

#ifdef DUMP_BUFFER
#include <tdm_helper.h>
#endif

#define IFACE_ENTRY                                      \
   E_Video_Hwc *evh;                                    \
   evh = container_of(iface, E_Video_Hwc, iface)

static void _e_video_hwc_render(E_Video_Hwc *evh, const char *func);
static void _e_video_hwc_buffer_show(E_Video_Hwc *evh, E_Comp_Wl_Video_Buf *vbuf, unsigned int transform);
static void _e_video_hwc_buffer_commit(E_Video_Hwc *evh, E_Comp_Wl_Video_Buf *vbuf);

static E_Client *
_e_video_hwc_client_offscreen_parent_get(E_Client *ec)
{
   E_Client *parent = NULL;

   if (!ec->comp_data || !ec->comp_data->sub.data)
     return NULL;

   parent = ec->comp_data->sub.data->parent;
   while (parent)
     {
        if (!parent->comp_data || !parent->comp_data->sub.data)
          return NULL;

        if (parent->comp_data->sub.data->remote_surface.offscreen_parent)
          return parent->comp_data->sub.data->remote_surface.offscreen_parent;

        parent = parent->comp_data->sub.data->parent;
     }

   return NULL;
}

static Eina_Bool
_e_video_hwc_client_visible_get(E_Client *ec)
{
   E_Client *offscreen_parent;

   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   if (!e_pixmap_resource_get(ec->pixmap))
     {
        VDB("no comp buffer", ec);
        return EINA_FALSE;
     }

   if (ec->comp_data->sub.data && ec->comp_data->sub.data->stand_alone)
     return EINA_TRUE;

   offscreen_parent = _e_video_hwc_client_offscreen_parent_get(ec);
   if (offscreen_parent && offscreen_parent->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
     {
        VDB("video surface invisible: offscreen fully obscured", ec);
        return EINA_FALSE;
     }

   if (!evas_object_visible_get(ec->frame))
     {
        VDB("evas obj invisible", ec);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

/* Video Buffer implementation */
static E_Comp_Wl_Video_Buf *
_e_video_hwc_vbuf_find(Eina_List *list, tbm_surface_h buffer)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l = NULL;

   EINA_LIST_FOREACH(list, l, vbuf)
     {
        if (vbuf->tbm_surface == buffer)
          return vbuf;
     }

   return NULL;
}

static E_Comp_Wl_Video_Buf *
_e_video_hwc_vbuf_find_with_comp_buffer(Eina_List *list, E_Comp_Wl_Buffer *comp_buffer)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l = NULL;

   EINA_LIST_FOREACH(list, l, vbuf)
     {
        if (vbuf->comp_buffer == comp_buffer)
          return vbuf;
     }

   return NULL;
}

static E_Comp_Wl_Video_Buf *
_e_video_hwc_buffer_copy(E_Comp_Wl_Video_Buf *vbuf, int aligned_width, Eina_Bool scanout)
{
   E_Comp_Wl_Video_Buf *temp = NULL;

   temp = e_comp_wl_video_buffer_alloc(aligned_width, vbuf->height, vbuf->tbmfmt, scanout);
   EINA_SAFETY_ON_NULL_RETURN_VAL(temp, NULL);

   temp->comp_buffer = vbuf->comp_buffer;

   VDB("copy vbuf(%d,%dx%d) => vbuf(%d,%dx%d)", NULL,
       MSTAMP(vbuf), vbuf->width_from_pitch, vbuf->height,
       MSTAMP(temp), temp->width_from_pitch, temp->height);

   e_comp_wl_video_buffer_copy(vbuf, temp);

#ifdef DUMP_BUFFER
   char file[256];
   static int i;
   snprintf(file, sizeof file, "/tmp/dump/%s_%d.png", "cpy", i++);
   tdm_helper_dump_buffer(temp->tbm_surface, file);
#endif

   return temp;
}

static Eina_Bool
_e_video_hwc_video_buffer_scanout_check(E_Comp_Wl_Video_Buf *vbuf)
{
   tbm_surface_h tbm_surface = NULL;
   tbm_bo bo = NULL;
   int flag;

   tbm_surface = vbuf->tbm_surface;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surface, EINA_FALSE);

   bo = tbm_surface_internal_get_bo(tbm_surface, 0);
   EINA_SAFETY_ON_NULL_RETURN_VAL(bo, EINA_FALSE);

   flag = tbm_bo_get_flags(bo);
   if (flag & TBM_BO_SCANOUT)
     return EINA_TRUE;

   return EINA_FALSE;
}

static void
_e_video_hwc_input_buffer_cb_free(E_Comp_Wl_Video_Buf *vbuf, void *data)
{
   E_Video_Hwc *evh = data;
   Eina_Bool need_hide = EINA_FALSE;

   DBG("Buffer(%p) to be free, refcnt(%d)", vbuf, vbuf->ref_cnt);

   evh->input_buffer_list = eina_list_remove(evh->input_buffer_list, vbuf);

   if (vbuf->comp_buffer)
     e_comp_wl_buffer_reference(&vbuf->buffer_ref, NULL);

   if (evh->current_fb == vbuf)
     {
        VIN("current fb destroyed", evh->ec);
        e_comp_wl_video_buffer_set_use(evh->current_fb, EINA_FALSE);
        evh->current_fb = NULL;
        need_hide = EINA_TRUE;
     }

   if (evh->committed_vbuf == vbuf)
     {
        VIN("committed fb destroyed", evh->ec);
        e_comp_wl_video_buffer_set_use(evh->committed_vbuf, EINA_FALSE);
        evh->committed_vbuf = NULL;
        need_hide = EINA_TRUE;
     }

   if (eina_list_data_find(evh->bqueue, vbuf))
     {
        VIN("waiting fb destroyed", evh->ec);
        evh->bqueue = eina_list_remove(evh->bqueue, vbuf);
     }

   if (need_hide)
     evh->backend.buffer_commit(evh, NULL);
}

static E_Comp_Wl_Video_Buf *
_e_video_hwc_input_buffer_create(E_Comp_Wl_Buffer *comp_buffer, int preferred_align, Eina_Bool scanout)
{
   E_Comp_Wl_Video_Buf *vbuf, *temp;
   int aligned_width;
   Eina_Bool input_buffer_scanout;

   vbuf = e_comp_wl_video_buffer_create_comp(comp_buffer);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

   input_buffer_scanout = _e_video_hwc_video_buffer_scanout_check(vbuf);
   if (((preferred_align != -1) && (vbuf->width_from_pitch % preferred_align != 0)) ||
       ((scanout) && (!input_buffer_scanout)))
     {
        if ((preferred_align != -1) && (vbuf->width_from_pitch % preferred_align != 0))
          aligned_width = ROUNDUP(vbuf->width_from_pitch, preferred_align);
        else
          aligned_width = vbuf->width;

        temp = _e_video_hwc_buffer_copy(vbuf, aligned_width,
                                        (input_buffer_scanout || scanout));
        e_comp_wl_video_buffer_unref(vbuf);

        if (!temp)
          return NULL;

        vbuf = temp;
     }

   DBG("Buffer(%p) created, refcnt:%d scanout:%d", vbuf, vbuf->ref_cnt, scanout);

   return vbuf;
}

static E_Comp_Wl_Video_Buf *
_e_video_hwc_input_buffer_get(E_Video_Hwc *evh, E_Comp_Wl_Buffer *comp_buffer, int preferred_align, Eina_Bool scanout)
{
   E_Comp_Wl_Video_Buf *vbuf;

   vbuf = _e_video_hwc_vbuf_find_with_comp_buffer(evh->input_buffer_list, comp_buffer);
   if (vbuf)
     goto end;

   vbuf = _e_video_hwc_input_buffer_create(comp_buffer, preferred_align, scanout);
   if (!vbuf)
     {
        VER("failed '_e_video_hwc_input_buffer_create()'", evh->ec);
        return NULL;
     }

   evh->input_buffer_list = eina_list_append(evh->input_buffer_list, vbuf);
   e_comp_wl_video_buffer_free_func_add(vbuf, _e_video_hwc_input_buffer_cb_free, evh);

   DBG("Client(%s):PID(%d) RscID(%d), Buffer(%p) created",
       e_client_util_name_get(evh->ec) ?: "No Name" , evh->ec->netwm.pid,
       wl_resource_get_id(evh->ec->comp_data->surface), vbuf);
end:
   vbuf->content_r = evh->geo.input_r;
   return vbuf;
}
/* End of Video Buffer implementation */

/* PP implementation */
static void
_e_video_hwc_pp_cb_done(tdm_pp *pp, tbm_surface_h sb, tbm_surface_h db, void *user_data)
{
   E_Video_Hwc *evh = (E_Video_Hwc *)user_data;
   E_Comp_Wl_Video_Buf *input_buffer, *pp_buffer;

   input_buffer = _e_video_hwc_vbuf_find(evh->input_buffer_list, sb);
   if (input_buffer)
     e_comp_wl_video_buffer_unref(input_buffer);

   pp_buffer = _e_video_hwc_vbuf_find(evh->pp_buffer_list, db);
   if (pp_buffer)
     {
        e_comp_wl_video_buffer_set_use(pp_buffer, EINA_FALSE);
        if (!_e_video_hwc_client_visible_get(evh->ec)) return;

        _e_video_hwc_buffer_show(evh, pp_buffer, 0);
     }
   else
     {
        VER("There is no pp_buffer", evh->ec);
        // there is no way to set in_use flag.
        // This will cause issue when server get available pp_buffer.
     }
}

static void
_e_video_hwc_pp_destroy(E_Video_Hwc_PP *pp)
{
   tdm_pp_destroy(pp->tdm_handle);
   free(pp);
}

static E_Video_Hwc_PP *
_e_video_hwc_pp_create(tdm_display *display, void *user_data)
{
   E_Video_Hwc_PP *pp;
   tdm_pp_capability caps;
   tdm_error err;

   pp = E_NEW(E_Video_Hwc_PP, 1);
   if (!pp)
     return NULL;

   pp->tdm_handle = tdm_display_create_pp(display, NULL);
   if (!pp->tdm_handle)
     {
        VER("Failed 'tdm_display_create_pp()'", NULL);
        free(pp);
        return NULL;
     }

   tdm_display_get_pp_available_size(display,
                                     &pp->minw, &pp->minh,
                                     &pp->maxw, &pp->maxh,
                                     &pp->align);

   err = tdm_display_get_pp_capabilities(display, &caps);
   if (err == TDM_ERROR_NONE)
     {
        if (caps & TDM_PP_CAPABILITY_SCANOUT)
          pp->scanout = EINA_TRUE;
     }

   err = tdm_pp_set_done_handler(pp->tdm_handle, _e_video_hwc_pp_cb_done, user_data);
   if (err != TDM_ERROR_NONE)
     {
        VER("tdm_pp_set_done_handler() failed", NULL);
        tdm_pp_destroy(pp->tdm_handle);
        free(pp);
        return NULL;
     }

   return pp;
}

static Eina_Bool
_e_video_hwc_pp_size_limit_check(E_Video_Hwc_PP *pp, const Eina_Rectangle *input_rect, const Eina_Rectangle *output_rect)
{
   if (pp->minw > 0)
     {
        if ((input_rect->w < pp->minw) ||
            (output_rect->w < pp->minw))
          goto err;
     }

   if (pp->minh > 0)
     {
        if ((input_rect->h < pp->minh) ||
            (output_rect->h < pp->minh))
          goto err;
     }

   if (pp->maxw > 0)
     {
        if ((input_rect->w > pp->maxw) ||
            (output_rect->w > pp->maxw))
          goto err;
     }

   if (pp->maxh > 0)
     {
        if ((input_rect->h > pp->maxh) ||
            (output_rect->h > pp->maxh))
          goto err;
     }

   return EINA_TRUE;
err:
   INF("size(%dx%d, %dx%d) is out of PP range",
       input_rect->w, input_rect->h, output_rect->w, output_rect->h);

   return EINA_FALSE;
}

static void
_e_video_hwc_pp_buffer_cb_free(E_Comp_Wl_Video_Buf *vbuf, void *data)
{
   E_Video_Hwc *evh = data;

   e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);

   if (evh->current_fb == vbuf)
     evh->current_fb = NULL;

   if (evh->committed_vbuf == vbuf)
     evh->committed_vbuf = NULL;

   evh->bqueue = eina_list_remove(evh->bqueue, vbuf);

   evh->pp_buffer_list = eina_list_remove(evh->pp_buffer_list, vbuf);
}

static E_Comp_Wl_Video_Buf *
_e_video_hwc_pp_buffer_get(E_Video_Hwc *evh, int width, int height)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;
   int i = 0;
   int aligned_width;

   if (evh->output_align != -1)
     aligned_width = ROUNDUP(width, evh->output_align);
   else
     aligned_width = width;

   if (evh->pp_buffer_list)
     {
        vbuf = eina_list_data_get(evh->pp_buffer_list);
        EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

        /* if we need bigger pp_buffers, destroy all pp_buffers and create */
        if (aligned_width > vbuf->width_from_pitch || height != vbuf->height)
          {
             Eina_List *ll;

             VIN("pp buffer changed: %dx%d => %dx%d", evh->ec,
                 vbuf->width_from_pitch, vbuf->height,
                 aligned_width, height);

             EINA_LIST_FOREACH_SAFE(evh->pp_buffer_list, l, ll, vbuf)
               {
                  /* free forcely */
                  e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
                  e_comp_wl_video_buffer_unref(vbuf);
               }
             if (evh->pp_buffer_list)
               NEVER_GET_HERE();

             if (evh->bqueue)
               NEVER_GET_HERE();
          }
     }

   if (!evh->pp_buffer_list)
     {
        for (i = 0; i < BUFFER_MAX_COUNT; i++)
          {
             vbuf = e_comp_wl_video_buffer_alloc(aligned_width, height, evh->pp_tbmfmt, EINA_TRUE);
             EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

             e_comp_wl_video_buffer_free_func_add(vbuf, _e_video_hwc_pp_buffer_cb_free, evh);
             evh->pp_buffer_list = eina_list_append(evh->pp_buffer_list, vbuf);

          }

        VIN("pp buffer created: %dx%d, %c%c%c%c", evh->ec,
            vbuf->width_from_pitch, height, FOURCC_STR(evh->pp_tbmfmt));

        evh->next_buffer = evh->pp_buffer_list;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(evh->pp_buffer_list, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evh->next_buffer, NULL);

   l = evh->next_buffer;
   while ((vbuf = evh->next_buffer->data))
     {
        evh->next_buffer = (evh->next_buffer->next) ? evh->next_buffer->next : evh->pp_buffer_list;

        if (!vbuf->in_use)
          {
             vbuf->content_r.w = width;
             vbuf->content_r.h = height;
             return vbuf;
          }

        if (l == evh->next_buffer)
          {
             VWR("all video framebuffers in use (max:%d)", evh->ec, BUFFER_MAX_COUNT);
             return NULL;
          }
     }

   return NULL;
}

static Eina_Bool
_e_video_hwc_pp_commit(E_Video_Hwc_PP *pp, E_Comp_Wl_Video_Buf *input_buffer, E_Comp_Wl_Video_Buf *pp_buffer, unsigned int transform)
{
   tdm_info_pp info;
   tdm_error err;

   CLEAR(info);
   info.src_config.size.h = input_buffer->width_from_pitch;
   info.src_config.size.v = input_buffer->height_from_size;
   info.src_config.pos.x = input_buffer->content_r.x;
   info.src_config.pos.y = input_buffer->content_r.y;
   info.src_config.pos.w = input_buffer->content_r.w;
   info.src_config.pos.h = input_buffer->content_r.h;
   info.src_config.format = input_buffer->tbmfmt;
   info.dst_config.size.h = pp_buffer->width_from_pitch;
   info.dst_config.size.v = pp_buffer->height_from_size;
   info.dst_config.pos.w = pp_buffer->content_r.w;
   info.dst_config.pos.h = pp_buffer->content_r.h;
   info.dst_config.format = pp_buffer->tbmfmt;
   info.transform = transform;

   if (memcmp(&pp->info, &info, sizeof info) != 0)
     {
        memcpy(&pp->info, &info, sizeof info);
        err = tdm_pp_set_info(pp->tdm_handle, &info);
        if (err != TDM_ERROR_NONE)
          {
             VER("tdm_pp_set_info() failed", NULL);
             return EINA_FALSE;
          }
     }

   err = tdm_pp_attach(pp->tdm_handle, input_buffer->tbm_surface, pp_buffer->tbm_surface);
   if (err != TDM_ERROR_NONE)
     {
        VER("tdm_pp_attach() failed", NULL);
        return EINA_FALSE;
     }

   err = tdm_pp_commit(pp->tdm_handle);
   if (err != TDM_ERROR_NONE)
     {
        VER("tdm_pp_commit() failed", NULL);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_pp_render(E_Video_Hwc *evh, E_Comp_Wl_Buffer *comp_buffer)
{
   E_Comp_Wl_Video_Buf *input_buffer, *pp_buffer;
   Eina_Bool res;

   if (!evh->pp)
     {
        evh->pp = _e_video_hwc_pp_create(e_comp->e_comp_screen->tdisplay, evh);
        if (!evh->pp)
          return EINA_FALSE;
     }

   res = _e_video_hwc_pp_size_limit_check(evh->pp,
                                          &evh->geo.input_r, &evh->geo.tdm.output_r);
   if (!res)
     return res;

   input_buffer = _e_video_hwc_input_buffer_get(evh, comp_buffer,
                                                evh->pp->align, evh->pp->scanout);
   if (!input_buffer)
     return EINA_FALSE;

   pp_buffer = _e_video_hwc_pp_buffer_get(evh,
                                          evh->geo.tdm.output_r.w,
                                          evh->geo.tdm.output_r.h);
   if (!pp_buffer)
     goto render_fail;

   res = _e_video_hwc_pp_commit(evh->pp, input_buffer, pp_buffer, evh->geo.tdm.transform);
   if (!res)
     goto render_fail;

   e_comp_wl_video_buffer_set_use(pp_buffer, EINA_TRUE);
   e_comp_wl_buffer_reference(&input_buffer->buffer_ref, input_buffer->comp_buffer);

   return EINA_TRUE;

render_fail:
   e_comp_wl_video_buffer_unref(input_buffer);
   return EINA_FALSE;
}
/* End of PP implementation */

static Eina_Bool
_e_video_hwc_can_commit(E_Video_Hwc *evh)
{
   if (e_output_dpms_get(evh->e_output))
     return EINA_FALSE;

   return _e_video_hwc_client_visible_get(evh->ec);
}

static Eina_Bool
_e_video_hwc_current_fb_update(E_Video_Hwc *evh)
{
   tbm_surface_h displaying_buffer;

   EINA_SAFETY_ON_NULL_RETURN_VAL(evh, EINA_FALSE);

   if (!evh->committed_vbuf)
     return EINA_FALSE;

   if (_e_video_hwc_can_commit(evh))
     {
        displaying_buffer = evh->backend.displaying_buffer_get(evh);

        if (evh->committed_vbuf->tbm_surface != displaying_buffer)
          return EINA_FALSE;
     }

   /* client can attachs the same wl_buffer twice. */
   if ((evh->current_fb) &&
       (VBUF_IS_VALID(evh->current_fb)) &&
       (evh->committed_vbuf != evh->current_fb))
     {
        e_comp_wl_video_buffer_set_use(evh->current_fb, EINA_FALSE);

        if (evh->current_fb->comp_buffer)
          e_comp_wl_buffer_reference(&evh->current_fb->buffer_ref, NULL);
     }

   evh->current_fb = evh->committed_vbuf;
   evh->committed_vbuf = NULL;

   VDB("current_fb(%d)", evh->ec, MSTAMP(evh->current_fb));

   return EINA_TRUE;
}

static void
_e_video_hwc_buffer_enqueue(E_Video_Hwc *evh, E_Comp_Wl_Video_Buf *vbuf)
{
   evh->bqueue = eina_list_append(evh->bqueue, vbuf);
   VDB("There are waiting fbs more than 1", evh->ec);
}

static E_Comp_Wl_Video_Buf *
_e_video_hwc_buffer_dequeue(E_Video_Hwc *evh)
{
   E_Comp_Wl_Video_Buf *vbuf;

   if (!evh->bqueue)
     return NULL;

   vbuf = eina_list_nth(evh->bqueue, 0);
   evh->bqueue = eina_list_remove(evh->bqueue, vbuf);

   return vbuf;
}

static void
_e_video_hwc_wait_buffer_commit(E_Video_Hwc *evh)
{
   E_Comp_Wl_Video_Buf *vbuf;

   vbuf = _e_video_hwc_buffer_dequeue(evh);
   if (!vbuf)
     return;

   if (!evh->backend.commit_available_check(evh))
     return;

   _e_video_hwc_buffer_commit(evh, vbuf);
}

static void
_e_video_hwc_buffer_commit(E_Video_Hwc *evh, E_Comp_Wl_Video_Buf *vbuf)
{
   /* Send a message 'wl_surface.frame', right before commit a buffer to
    * tdm driver. */
   e_pixmap_image_clear(evh->ec->pixmap, EINA_TRUE);

   evh->committed_vbuf = vbuf;

   if (!_e_video_hwc_can_commit(evh))
     goto no_commit;

   if (!evh->backend.buffer_commit(evh, vbuf))
     goto no_commit;

   return;

no_commit:
   _e_video_hwc_current_fb_update(evh);
   _e_video_hwc_wait_buffer_commit(evh);
}

static void
_e_video_hwc_buffer_show(E_Video_Hwc *evh, E_Comp_Wl_Video_Buf *vbuf, unsigned int transform)
{
   vbuf->content_t = transform;

   e_comp_wl_video_buffer_set_use(vbuf, EINA_TRUE);

   if (vbuf->comp_buffer)
     e_comp_wl_buffer_reference(&vbuf->buffer_ref, vbuf->comp_buffer);

   if (!evh->backend.commit_available_check(evh))
     {
        _e_video_hwc_buffer_enqueue(evh, vbuf);
        return;
     }

   _e_video_hwc_buffer_commit(evh, vbuf);
}

static void
_e_video_hwc_hide(E_Video_Hwc *evh)
{
   E_Comp_Wl_Video_Buf *vbuf;

   if (evh->current_fb || evh->committed_vbuf)
     evh->backend.buffer_commit(evh, NULL);

   if (evh->current_fb)
     {
        e_comp_wl_video_buffer_set_use(evh->current_fb, EINA_FALSE);
        evh->current_fb = NULL;
     }

   if (evh->old_comp_buffer)
     evh->old_comp_buffer = NULL;

   if (evh->committed_vbuf)
     {
        e_comp_wl_video_buffer_set_use(evh->committed_vbuf, EINA_FALSE);
        evh->committed_vbuf = NULL;
     }

   EINA_LIST_FREE(evh->bqueue, vbuf)
      e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
}

static void
_e_video_hwc_geometry_tdm_config_update(E_Client *ec, E_Video_Hwc_Geometry *out)
{
   E_Zone *zone;
   E_Comp_Wl_Output *output;
   E_Client *topmost;
   int tran, flip;
   int transform;

   topmost = e_comp_wl_topmost_parent_get(ec);
   EINA_SAFETY_ON_NULL_GOTO(topmost, normal);

   output = e_comp_wl_output_find(topmost);
   EINA_SAFETY_ON_NULL_GOTO(output, normal);

   zone = e_comp_zone_xy_get(topmost->x, topmost->y);
   EINA_SAFETY_ON_NULL_GOTO(zone, normal);

   tran = out->transform & 0x3;
   flip = out->transform & 0x4;
   transform = flip + (tran + output->transform) % 4;
   switch(transform)
     {
      case WL_OUTPUT_TRANSFORM_90:
         out->tdm.transform = TDM_TRANSFORM_270;
         break;
      case WL_OUTPUT_TRANSFORM_180:
         out->tdm.transform = TDM_TRANSFORM_180;
         break;
      case WL_OUTPUT_TRANSFORM_270:
         out->tdm.transform = TDM_TRANSFORM_90;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED:
         out->tdm.transform = TDM_TRANSFORM_FLIPPED;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
         out->tdm.transform = TDM_TRANSFORM_FLIPPED_270;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_180:
         out->tdm.transform = TDM_TRANSFORM_FLIPPED_180;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
         out->tdm.transform = TDM_TRANSFORM_FLIPPED_90;
         break;
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:
         out->tdm.transform = TDM_TRANSFORM_NORMAL;
         break;
     }

   if (output->transform % 2)
     {
        if (out->tdm.transform == TDM_TRANSFORM_FLIPPED)
          out->tdm.transform = TDM_TRANSFORM_FLIPPED_180;
        else if (out->tdm.transform == TDM_TRANSFORM_FLIPPED_90)
          out->tdm.transform = TDM_TRANSFORM_FLIPPED_270;
        else if (out->tdm.transform == TDM_TRANSFORM_FLIPPED_180)
          out->tdm.transform = TDM_TRANSFORM_FLIPPED;
        else if (out->tdm.transform == TDM_TRANSFORM_FLIPPED_270)
          out->tdm.transform = TDM_TRANSFORM_FLIPPED_90;
     }

   if (output->transform == 0)
     out->tdm.output_r = out->output_r;
   else
     e_comp_wl_rect_convert(zone->w, zone->h, output->transform, 1,
                            out->output_r.x, out->output_r.y,
                            out->output_r.w, out->output_r.h,
                            &out->tdm.output_r.x, &out->tdm.output_r.y,
                            &out->tdm.output_r.w, &out->tdm.output_r.h);

   VDB("geomtry: screen(%d,%d %dx%d | %d) => %d => physical(%d,%d %dx%d | %d)",
       ec, EINA_RECTANGLE_ARGS(&out->output_r), out->transform, transform,
       EINA_RECTANGLE_ARGS(&out->tdm.output_r), out->tdm.transform);

   return;
normal:
   out->tdm.output_r = out->output_r;
   out->tdm.transform = out->transform;
}

static Eina_Bool
_e_video_hwc_geometry_map_apply(E_Client *ec, E_Video_Hwc_Geometry *out)
{
   const Evas_Map *m;
   Evas_Coord x1, x2, y1, y2;
   Eina_Rectangle output_r;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->frame, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(out, EINA_FALSE);

   m = evas_object_map_get(ec->frame);
   if (!m) return EINA_TRUE;

   /* If frame has map, it means that ec's geometry is decided by map's geometry.
    * ec->x,y,w,h and ec->client.x,y,w,h is not useful.
    */

   evas_map_point_coord_get(m, 0, &x1, &y1, NULL);
   evas_map_point_coord_get(m, 2, &x2, &y2, NULL);

   output_r.x = x1;
   output_r.y = y1;
   output_r.w = x2 - x1;
   output_r.w = (output_r.w + 1) & ~1;
   output_r.h = y2 - y1;
   output_r.h = (output_r.h + 1) & ~1;

   if (!memcmp(&out->output_r, &output_r, sizeof(Eina_Rectangle)))
     return EINA_FALSE;

   VDB("frame(%p) m(%p) output(%d,%d %dx%d) => (%d,%d %dx%d)", ec, ec->frame, m,
       EINA_RECTANGLE_ARGS(&out->output_r), EINA_RECTANGLE_ARGS(&output_r));

   out->output_r = output_r;

   _e_video_hwc_geometry_tdm_config_update(ec, out);

   return EINA_TRUE;
}

static void
_e_video_hwc_render_job(void *data)
{
   E_Video_Hwc *evh;
   E_Client *topmost;
   Eina_Bool render = EINA_FALSE;

   evh = data;
   if (evh->render.map)
     {
        evh->render.map = EINA_FALSE;
        render = _e_video_hwc_geometry_map_apply(evh->ec, &evh->geo);
     }

   if (evh->render.topmost_viewport)
     {
        evh->render.topmost_viewport = EINA_FALSE;
        topmost = e_comp_wl_topmost_parent_get(evh->ec);
        if (topmost)
          e_comp_wl_viewport_apply(topmost);
        render = EINA_TRUE;
     }

   if ((render) || (evh->render.buffer_change))
     {
        evh->render.buffer_change = EINA_FALSE;
        _e_video_hwc_render(evh, __FUNCTION__);
     }

   evh->render.job = NULL;
}

static void
_e_video_hwc_render_queue(E_Video_Hwc *evh)
{
   if (evh->render.job)
     ecore_job_del(evh->render.job);

   evh->render.job = ecore_job_add(_e_video_hwc_render_job, evh);
}

static void
_e_video_hwc_cb_evas_resize(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Video_Hwc *evh = data;

   evh->render.map = EINA_TRUE;
   _e_video_hwc_render_queue(evh);
}

static void
_e_video_hwc_cb_evas_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc *evh = data;

   evh->render.map = EINA_TRUE;
   _e_video_hwc_render_queue(evh);
}

static void
_e_video_hwc_input_buffer_valid(E_Video_Hwc *evh, E_Comp_Wl_Buffer *comp_buffer)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;

   EINA_LIST_FOREACH(evh->input_buffer_list, l, vbuf)
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

static tbm_format
_e_video_hwc_comp_buffer_tbm_format_get(E_Comp_Wl_Buffer *comp_buffer)
{
   tbm_surface_h tbm_surf;

   tbm_surf = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, comp_buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surf, 0);

   return tbm_surface_get_format(tbm_surf);
}

static tbm_surface_h
_e_video_hwc_client_tbm_surface_get(E_Client *ec)
{
   E_Comp_Wl_Buffer *comp_buffer;
   tbm_surface_h tbmsurf;

   comp_buffer = e_pixmap_resource_get(ec->pixmap);
   if (!comp_buffer)
     {
        /* No comp buffer */
        return NULL;
     }

   tbmsurf = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server,
                                            comp_buffer->resource);

   return tbmsurf;
}

static void
buffer_transform(int width, int height, uint32_t transform, int32_t scale,
                 int sx, int sy, int *dx, int *dy)
{
   switch (transform)
     {
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:
         *dx = sx, *dy = sy;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED:
         *dx = width - sx, *dy = sy;
         break;
      case WL_OUTPUT_TRANSFORM_90:
         *dx = height - sy, *dy = sx;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
         *dx = height - sy, *dy = width - sx;
         break;
      case WL_OUTPUT_TRANSFORM_180:
         *dx = width - sx, *dy = height - sy;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_180:
         *dx = sx, *dy = height - sy;
         break;
      case WL_OUTPUT_TRANSFORM_270:
         *dx = sy, *dy = width - sx;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
         *dx = sy, *dy = sx;
         break;
     }

   *dx *= scale;
   *dy *= scale;
}

static void
_e_video_hwc_geometry_input_rect_get_with_viewport(tbm_surface_h tbm_surf, E_Comp_Wl_Buffer_Viewport *vp, Eina_Rectangle *out)
{
   int bw, bh;
   int x1, y1, x2, y2;
   int tx1, ty1, tx2, ty2;
   int width_from_buffer, height_from_buffer;

   bw = tbm_surface_get_width(tbm_surf);
   bh = tbm_surface_get_height(tbm_surf);
   VDB("TBM buffer size %d %d", NULL, bw, bh);

   switch (vp->buffer.transform)
     {
      case WL_OUTPUT_TRANSFORM_90:
      case WL_OUTPUT_TRANSFORM_270:
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
         width_from_buffer = bh / vp->buffer.scale;
         height_from_buffer = bw / vp->buffer.scale;
         break;
      default:
         width_from_buffer = bw / vp->buffer.scale;
         height_from_buffer = bh / vp->buffer.scale;
         break;
     }

   if (vp->buffer.src_width == wl_fixed_from_int(-1))
     {
        x1 = 0.0;
        y1 = 0.0;
        x2 = width_from_buffer;
        y2 = height_from_buffer;
     }
   else
     {
        x1 = wl_fixed_to_int(vp->buffer.src_x);
        y1 = wl_fixed_to_int(vp->buffer.src_y);
        x2 = wl_fixed_to_int(vp->buffer.src_x + vp->buffer.src_width);
        y2 = wl_fixed_to_int(vp->buffer.src_y + vp->buffer.src_height);
     }

   VDB("transform(%d) scale(%d) buffer(%dx%d) src(%d,%d %d,%d)",
       NULL, vp->buffer.transform, vp->buffer.scale,
       width_from_buffer, height_from_buffer,
       x1, y1, x2 - x1, y2 - y1);

   buffer_transform(width_from_buffer, height_from_buffer,
                    vp->buffer.transform, vp->buffer.scale, x1, y1, &tx1, &ty1);
   buffer_transform(width_from_buffer, height_from_buffer,
                    vp->buffer.transform, vp->buffer.scale, x2, y2, &tx2, &ty2);

   out->x = (tx1 <= tx2) ? tx1 : tx2;
   out->y = (ty1 <= ty2) ? ty1 : ty2;
   out->w = (tx1 <= tx2) ? tx2 - tx1 : tx1 - tx2;
   out->h = (ty1 <= ty2) ? ty2 - ty1 : ty1 - ty2;
}

static E_Comp_Wl_Subsurf_Data *
_e_video_hwc_client_subsurface_data_get(E_Client *ec)
{
   if (ec->comp_data && ec->comp_data->sub.data)
     return ec->comp_data->sub.data;

   return NULL;
}

static void
_e_video_hwc_geometry_output_rect_get(E_Client *ec, Eina_Rectangle *out)
{
   E_Comp_Wl_Subsurf_Data *sdata;

   sdata = _e_video_hwc_client_subsurface_data_get(ec);
   if (sdata)
     {
        if (sdata->parent)
          {
             out->x = sdata->parent->x + sdata->position.x;
             out->y = sdata->parent->y + sdata->position.y;
          }
        else
          {
             out->x = sdata->position.x;
             out->y = sdata->position.y;
          }
     }
   else
     {
        out->x = ec->x;
        out->y = ec->y;
     }

   out->w = ec->comp_data->width_from_viewport;
   out->w = (out->w + 1) & ~1;
   out->h = ec->comp_data->height_from_viewport;

   e_comp_object_frame_xy_unadjust(ec->frame, out->x, out->y, &out->x, &out->y);
   e_comp_object_frame_wh_unadjust(ec->frame, out->w, out->h, &out->w, &out->h);
}

/* convert from logical screen to physical output */
static Eina_Bool
_e_video_hwc_geometry_viewport_apply(E_Client *ec, E_Video_Hwc_Geometry *out)
{
   E_Comp_Wl_Buffer_Viewport *vp;
   tbm_surface_h tbm_surf;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(out, EINA_FALSE);

   tbm_surf = _e_video_hwc_client_tbm_surface_get(ec);
   if (!tbm_surf)
     {
        /* No tbm_surface */
        return EINA_FALSE;
     }

   vp = &ec->comp_data->scaler.buffer_viewport;
   _e_video_hwc_geometry_input_rect_get_with_viewport(tbm_surf, vp, &out->input_r);

   _e_video_hwc_geometry_output_rect_get(ec, &out->output_r);
   out->transform = vp->buffer.transform;

   _e_video_hwc_geometry_tdm_config_update(ec, out);

   VDB("geometry(%d,%d %dx%d  %d,%d %dx%d  %d)", ec,
       EINA_RECTANGLE_ARGS(&out->input_r),EINA_RECTANGLE_ARGS(&out->output_r),
       out->transform);

   return EINA_TRUE;
}

static void
_e_video_hwc_geometry_cal_to_input(int output_w, int output_h, int input_w, int input_h,
                                   uint32_t trasnform, int ox, int oy, int *ix, int *iy)
{
   float ratio_w, ratio_h;

   switch(trasnform)
     {
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:
         *ix = ox, *iy = oy;
         break;
      case WL_OUTPUT_TRANSFORM_270:
         *ix = oy, *iy = output_w - ox;
         break;
      case WL_OUTPUT_TRANSFORM_180:
         *ix = output_w - ox, *iy = output_h - oy;
         break;
      case WL_OUTPUT_TRANSFORM_90:
         *ix = output_h - oy, *iy = ox;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED:
         *ix = output_w - ox, *iy = oy;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
         *ix = oy, *iy = ox;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_180:
         *ix = ox, *iy = output_h - oy;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
         *ix = output_h - oy, *iy = output_w - ox;
         break;
     }
   if (trasnform & 0x1)
     {
        ratio_w = (float)input_w / output_h;
        ratio_h = (float)input_h / output_w;
     }
   else
     {
        ratio_w = (float)input_w / output_w;
        ratio_h = (float)input_h / output_h;
     }
   *ix *= ratio_w;
   *iy *= ratio_h;
}

static void
_e_video_hwc_geometry_cal_to_input_rect(E_Video_Hwc_Geometry *geo, Eina_Rectangle *srect, Eina_Rectangle *drect)
{
   int xf1, yf1, xf2, yf2;

   /* first transform box coordinates if the scaler is set */

   xf1 = srect->x;
   yf1 = srect->y;
   xf2 = srect->x + srect->w;
   yf2 = srect->y + srect->h;

   _e_video_hwc_geometry_cal_to_input(geo->output_r.w, geo->output_r.h,
                                      geo->input_r.w, geo->input_r.h,
                                      geo->transform, xf1, yf1, &xf1, &yf1);
   _e_video_hwc_geometry_cal_to_input(geo->output_r.w, geo->output_r.h,
                                      geo->input_r.w, geo->input_r.h,
                                      geo->transform, xf2, yf2, &xf2, &yf2);

   drect->x = MIN(xf1, xf2);
   drect->y = MIN(yf1, yf2);
   drect->w = MAX(xf1, xf2) - drect->x;
   drect->h = MAX(yf1, yf2) - drect->y;
}

static Eina_Bool
_e_video_hwc_geometry_get(E_Client *ec, E_Video_Hwc_Geometry *out)
{
   E_Zone *zone;
   E_Client *topmost;
   Eina_Rectangle screen = {0,};
   Eina_Rectangle output_r = {0,}, input_r = {0,};

   /* get geometry information with buffer scale, transform and viewport. */
   if (!_e_video_hwc_geometry_viewport_apply(ec, out))
     return EINA_FALSE;

   _e_video_hwc_geometry_map_apply(ec, out);

   topmost = e_comp_wl_topmost_parent_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(topmost, EINA_FALSE);

   zone = e_comp_zone_xy_get(topmost->x, topmost->y);
   EINA_SAFETY_ON_NULL_RETURN_VAL(zone, EINA_FALSE);

   screen.w = zone->w;
   screen.h = zone->h;

   e_comp_wl_video_buffer_size_get(ec, &input_r.w, &input_r.h);
   // when topmost is not mapped, input size can be abnormal.
   // in this case, it will be render by topmost showing.
   if (!eina_rectangle_intersection(&out->input_r, &input_r) || (out->input_r.w <= 10 || out->input_r.h <= 10))
     {
        VER("input area is empty", ec);
        return EINA_FALSE;
     }

   if (out->output_r.x >= 0 && out->output_r.y >= 0 &&
       (out->output_r.x + out->output_r.w) <= screen.w &&
       (out->output_r.y + out->output_r.h) <= screen.h)
     return EINA_TRUE;

   /* TODO: need to improve */

   output_r = out->output_r;
   if (!eina_rectangle_intersection(&output_r, &screen))
     {
        VER("output_r(%d,%d %dx%d) screen(%d,%d %dx%d) => intersect(%d,%d %dx%d)",
            ec, EINA_RECTANGLE_ARGS(&out->output_r),
            EINA_RECTANGLE_ARGS(&screen), EINA_RECTANGLE_ARGS(&output_r));
        return EINA_TRUE;
     }

   output_r.x -= out->output_r.x;
   output_r.y -= out->output_r.y;

   if (output_r.w <= 0 || output_r.h <= 0)
     {
        VER("output area is empty", ec);
        return EINA_FALSE;
     }

   VDB("output(%d,%d %dx%d) input(%d,%d %dx%d)",
       ec, EINA_RECTANGLE_ARGS(&output_r), EINA_RECTANGLE_ARGS(&input_r));

   _e_video_hwc_geometry_cal_to_input_rect(out, &output_r, &input_r);

   VDB("output(%d,%d %dx%d) input(%d,%d %dx%d)",
       ec, EINA_RECTANGLE_ARGS(&output_r), EINA_RECTANGLE_ARGS(&input_r));

   output_r.x += out->output_r.x;
   output_r.y += out->output_r.y;

   input_r.x += out->input_r.x;
   input_r.y += out->input_r.y;

   output_r.x = output_r.x & ~1;
   output_r.w = (output_r.w + 1) & ~1;

   input_r.x = input_r.x & ~1;
   input_r.w = (input_r.w + 1) & ~1;

   out->output_r = output_r;
   out->input_r = input_r;

   _e_video_hwc_geometry_tdm_config_update(ec, out);

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_client_parent_viewable_get(E_Client *ec)
{
   E_Client *topmost_parent;

   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   topmost_parent = e_comp_wl_topmost_parent_get(ec);

   if (!topmost_parent)
     return EINA_FALSE;

   if (topmost_parent == ec)
     {
        VDB("There is no video parent surface", ec);
        return EINA_FALSE;
     }

   if (!topmost_parent->visible)
     {
        VDB("parent(0x%08"PRIxPTR") not viewable", ec,
            (Ecore_Window)e_client_util_win_get(topmost_parent));
        return EINA_FALSE;
     }

   if (!e_pixmap_resource_get(topmost_parent->pixmap))
     {
        VDB("parent(0x%08"PRIxPTR") no comp buffer", ec,
            (Ecore_Window)e_client_util_win_get(topmost_parent));
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_video_hwc_render(E_Video_Hwc *evh, const char *func)
{
   E_Comp_Wl_Buffer *comp_buffer;
   E_Comp_Wl_Video_Buf *input_buffer = NULL;
   E_Client *topmost;

   EINA_SAFETY_ON_NULL_RETURN(evh->ec);

   /* buffer can be NULL when camera/video's mode changed. Do nothing and
    * keep previous frame in this case.
    */
   if (!evh->ec->pixmap)
     return;

   if (!_e_video_hwc_client_visible_get(evh->ec))
     {
        evh->need_force_render = EINA_TRUE;
        _e_video_hwc_hide(evh);
        return;
     }

   comp_buffer = e_pixmap_resource_get(evh->ec->pixmap);
   if (!comp_buffer) return;

   evh->tbmfmt = _e_video_hwc_comp_buffer_tbm_format_get(comp_buffer);

   topmost = e_comp_wl_topmost_parent_get(evh->ec);
   EINA_SAFETY_ON_NULL_RETURN(topmost);

   if(e_comp_wl_viewport_is_changed(topmost))
     {
        VIN("need update viewport: apply topmost", evh->ec);
        e_comp_wl_viewport_apply(topmost);
     }

   if (!_e_video_hwc_geometry_get(evh->ec, &evh->geo))
     {
        if(!evh->need_force_render && !_e_video_hwc_client_parent_viewable_get(evh->ec))
          {
             VIN("need force render", evh->ec);
             evh->need_force_render = EINA_TRUE;
          }
        return;
     }

   DBG("====================================== (%s)", func);
   VDB("old: "GEO_FMT" buf(%p)", evh->ec, GEO_ARG(&evh->old_geo), evh->old_comp_buffer);
   VDB("new: "GEO_FMT" buf(%p) %c%c%c%c", evh->ec, GEO_ARG(&evh->geo), comp_buffer, FOURCC_STR(evh->tbmfmt));

   if (!memcmp(&evh->old_geo, &evh->geo, sizeof evh->geo) &&
       evh->old_comp_buffer == comp_buffer)
     return;

   evh->need_force_render = EINA_FALSE;

   _e_video_hwc_input_buffer_valid(evh, comp_buffer);

   if (!evh->backend.check_if_pp_needed(evh))
     {
        /* 1. non converting case */
        input_buffer = _e_video_hwc_input_buffer_get(evh, comp_buffer,
                                                     evh->output_align, EINA_TRUE);
        if (!input_buffer)
          return;

        _e_video_hwc_buffer_show(evh, input_buffer, evh->geo.tdm.transform);
     }
   else
     {
        if (!_e_video_hwc_pp_render(evh, comp_buffer))
          return;
     }

   evh->old_geo = evh->geo;
   evh->old_comp_buffer = comp_buffer;

   DBG("======================================.");
}

static E_Client *
_e_video_hwc_child_client_get(E_Client *ec)
{
   E_Client *subc = NULL;
   Eina_List *l;
   if (!ec) return NULL;
   if (e_object_is_del(E_OBJECT(ec))) return NULL;
   if (!ec->comp_data) return NULL;

   if (e_client_video_hw_composition_check(ec)) return ec;

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        E_Client *temp= NULL;
        if (!subc->comp_data || e_object_is_del(E_OBJECT(subc))) continue;
        temp = _e_video_hwc_child_client_get(subc);
        if(temp) return temp;
     }

   return NULL;
}

static Eina_Bool
_e_video_hwc_cb_client_show(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   E_Client *video_ec = NULL;
   E_Video_Hwc *evh = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (!ec->comp_data) return ECORE_CALLBACK_PASS_ON;

   video_ec = _e_video_hwc_child_client_get(ec);
   if (!video_ec) return ECORE_CALLBACK_PASS_ON;

   evh = data;
   if (!evh) return ECORE_CALLBACK_PASS_ON;

   if (evh->old_comp_buffer)
     {
        VIN("video already rendering..", evh->ec);
        return ECORE_CALLBACK_PASS_ON;
     }

   if (ec == e_comp_wl_topmost_parent_get(evh->ec))
     {
        VIN("video need rendering..", evh->ec);
        evh->render.topmost_viewport = EINA_TRUE;
        _e_video_hwc_render_queue(evh);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_hwc_cb_client_buffer_change(void *data, int type, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;
   E_Video_Hwc *evh;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   evh = data;
   ec = ev->ec;

   if (evh->ec != ec)
     return ECORE_CALLBACK_PASS_ON;

   if (e_object_is_del(E_OBJECT(ec)))
     return ECORE_CALLBACK_PASS_ON;

   evh->render.buffer_change = EINA_TRUE;
   _e_video_hwc_render_queue(evh);

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_video_hwc_cb_evas_show(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc *evh;
   E_Client *ec;

   evh = data;
   ec = evh->ec;
   if (e_object_is_del(E_OBJECT(ec)))
     return;

   if (evh->need_force_render)
     {
        VIN("video forcely rendering..", evh->ec);
        _e_video_hwc_render(evh, __FUNCTION__);
     }

   /* if stand_alone is true, not show */
   if (ec->comp_data->sub.data && ec->comp_data->sub.data->stand_alone)
     return;

   /* FIXME It seems unnecessary. */
   if (evh->hwc_policy == E_HWC_POLICY_PLANES)
     {
        if (!e_video_hwc_planes_properties_commit(evh))
          return;
     }

   if (evh->current_fb)
     _e_video_hwc_buffer_show(evh, evh->current_fb, evh->current_fb->content_t);

}

static void
_e_video_hwc_client_event_init(E_Video_Hwc *evh)
{
   evas_object_event_callback_add(evh->ec->frame, EVAS_CALLBACK_SHOW,
                                  _e_video_hwc_cb_evas_show, evh);
   evas_object_event_callback_add(evh->ec->frame, EVAS_CALLBACK_RESIZE,
                                  _e_video_hwc_cb_evas_resize, evh);
   evas_object_event_callback_add(evh->ec->frame, EVAS_CALLBACK_MOVE,
                                  _e_video_hwc_cb_evas_move, evh);

   E_LIST_HANDLER_APPEND(evh->ec_event_handler, E_EVENT_CLIENT_SHOW,
                         _e_video_hwc_cb_client_show, evh);
   E_LIST_HANDLER_APPEND(evh->ec_event_handler, E_EVENT_CLIENT_BUFFER_CHANGE,
                         _e_video_hwc_cb_client_buffer_change, evh);
}

static void
_e_video_hwc_client_event_deinit(E_Video_Hwc *evh)
{
   evas_object_event_callback_del_full(evh->ec->frame, EVAS_CALLBACK_SHOW,
                                       _e_video_hwc_cb_evas_show, evh);
   evas_object_event_callback_del_full(evh->ec->frame, EVAS_CALLBACK_RESIZE,
                                       _e_video_hwc_cb_evas_resize, evh);
   evas_object_event_callback_del_full(evh->ec->frame, EVAS_CALLBACK_MOVE,
                                       _e_video_hwc_cb_evas_move, evh);

   E_FREE_LIST(evh->ec_event_handler, ecore_event_handler_del);
}

static void
_e_video_hwc_iface_destroy(E_Video_Comp_Iface *iface)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l = NULL, *ll = NULL;

   IFACE_ENTRY;

   _e_video_hwc_hide(evh);

   EINA_LIST_FOREACH_SAFE(evh->input_buffer_list, l, ll, vbuf)
     {
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        e_comp_wl_video_buffer_unref(vbuf);
     }

   EINA_LIST_FOREACH_SAFE(evh->pp_buffer_list, l, ll, vbuf)
     {
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        e_comp_wl_video_buffer_unref(vbuf);
     }

   if (evh->input_buffer_list)
     NEVER_GET_HERE();
   if (evh->pp_buffer_list)
     NEVER_GET_HERE();

   /* destroy converter second */
   E_FREE_FUNC(evh->pp, _e_video_hwc_pp_destroy);

   if (e_comp_object_mask_has(evh->ec->frame))
     e_comp_object_mask_set(evh->ec->frame, EINA_FALSE);

   _e_video_hwc_client_event_deinit(evh);

   E_FREE_FUNC(evh->render.job, ecore_job_del);

   evh->backend.destroy(evh);
}

static Eina_Bool
_e_video_hwc_iface_property_get(E_Video_Comp_Iface *iface, unsigned int id, tdm_value *value)
{
   IFACE_ENTRY;

   return evh->backend.property_get(evh, id, value);
}

static Eina_Bool
_e_video_hwc_iface_property_set(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value)
{
   IFACE_ENTRY;

   return evh->backend.property_set(evh, id, value);
}

static Eina_Bool
_e_video_hwc_iface_property_delay_set(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value)
{
   IFACE_ENTRY;

   if (evh->hwc_policy != E_HWC_POLICY_PLANES)
     return EINA_FALSE;
   return e_video_hwc_planes_property_delay_set(evh, id, value);
}

static Eina_Bool
_e_video_hwc_iface_available_properties_get(E_Video_Comp_Iface *iface, const tdm_prop **props, int *count)
{
   IFACE_ENTRY;

   return evh->backend.available_properties_get(evh, props, count);
}

static Eina_Bool
_e_video_hwc_iface_info_get(E_Video_Comp_Iface *iface, E_Client_Video_Info *info)
{
   IFACE_ENTRY;

   if (evh->hwc_policy != E_HWC_POLICY_WINDOWS)
     return EINA_FALSE;
   return e_video_hwc_windows_info_get(evh, info);
}

static Eina_Bool
_e_video_hwc_iface_commit_data_release(E_Video_Comp_Iface *iface, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec)
{
   IFACE_ENTRY;

   if (evh->hwc_policy != E_HWC_POLICY_WINDOWS)
     return EINA_FALSE;
   return e_video_hwc_windows_commit_data_release(evh, sequence, tv_sec, tv_usec);
}

static tbm_surface_h
_e_video_hwc_iface_tbm_surface_get(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   if (evh->hwc_policy != E_HWC_POLICY_WINDOWS)
     return NULL;
   return e_video_hwc_windows_tbm_surface_get(evh);
}

static E_Video_Hwc *
_e_video_hwc_create(E_Client *ec)
{
   E_Video_Hwc *evh;
   E_Hwc_Policy hwc_policy;
   E_Output *output;

   output = e_output_find(ec->zone->output_id);
   if (!output)
     {
        VER("Failed to find 'E_Output': id %d", ec, ec->zone->output_id);
        return NULL;
     }

   hwc_policy = e_zone_video_hwc_policy_get(ec->zone);
   if (hwc_policy == E_HWC_POLICY_PLANES)
     evh = e_video_hwc_planes_create(output, ec);
   else if (hwc_policy == E_HWC_POLICY_WINDOWS)
     evh = e_video_hwc_windows_create(output, ec);
   else
     {
        VER("Unknown HWC mode %d", ec, hwc_policy);
        return NULL;
     }

   if (!evh)
     {
        VER("Failed to create 'E_Video_Hwc'", ec);
        return NULL;
     }

   evh->hwc_policy = hwc_policy;
   evh->e_output = output;
   evh->ec = ec;

   //TODO: shoud this function be called here?
   tdm_output_get_available_size(output->toutput, NULL, NULL, NULL, NULL,
                                 &evh->output_align);

   return evh;
}

EINTERN E_Video_Comp_Iface *
e_video_hwc_iface_create(E_Client_Video *ecv)
{
   E_Video_Hwc *evh;
   E_Client *ec;

   ec = e_client_video_ec_get(ecv);

   VIN("Create HWC interface", ec);

   evh = _e_video_hwc_create(ec);
   if (!evh)
     return NULL;

   _e_video_hwc_client_event_init(evh);

   evh->ecv = ecv;

   evh->iface.destroy = _e_video_hwc_iface_destroy;
   evh->iface.property_get = _e_video_hwc_iface_property_get;
   evh->iface.property_set = _e_video_hwc_iface_property_set;
   evh->iface.property_delay_set = _e_video_hwc_iface_property_delay_set;
   evh->iface.available_properties_get = _e_video_hwc_iface_available_properties_get;
   evh->iface.info_get = _e_video_hwc_iface_info_get;
   evh->iface.commit_data_release = _e_video_hwc_iface_commit_data_release;
   evh->iface.tbm_surface_get = _e_video_hwc_iface_tbm_surface_get;

   /* This ec is a video client now. */
   e_client_video_hw_composition_set(ecv);

   return &evh->iface;
}

EINTERN Eina_Bool
e_video_hwc_current_fb_update(E_Video_Hwc *evh)
{
   return _e_video_hwc_current_fb_update(evh);
}

EINTERN void
e_video_hwc_wait_buffer_commit(E_Video_Hwc *evh)
{
   _e_video_hwc_wait_buffer_commit(evh);
}

EINTERN void
e_video_hwc_client_mask_update(E_Video_Hwc *evh)
{
   E_Client *topmost;
   Eina_Bool punch = EINA_FALSE;
   int bw, bh;

   if (e_video_debug_punch_value_get())
     punch = EINA_TRUE;
   else if ((topmost = e_comp_wl_topmost_parent_get(evh->ec)))
     {
        /* if it's laid above main surface */
        if (eina_list_data_find(topmost->comp_data->sub.list, evh->ec))
          punch = EINA_TRUE;
        /* if it's laid under main surface and main surface is transparent */
        else if (topmost->argb)
          {
             /* FIXME: the mask obj can be drawn at the wrong position in the beginnig
              * time. It happens caused by window manager policy.
              */
             if ((topmost->fullscreen || topmost->maximized) &&
                 (evh->geo.output_r.x == 0 || evh->geo.output_r.y == 0))
               {
                  e_pixmap_size_get(topmost->pixmap, &bw, &bh);

                  if (bw > 100 && bh > 100 &&
                      evh->geo.output_r.w < 100 && evh->geo.output_r.h < 100)
                    {
                       VIN("don't punch. (%dx%d, %dx%d)", evh->ec,
                           bw, bh, evh->geo.output_r.w, evh->geo.output_r.h);
                       return;
                    }
               }

             punch = EINA_TRUE;
          }
     }

   if (punch)
     {
        if (!e_comp_object_mask_has(evh->ec->frame))
          {
             e_comp_object_mask_set(evh->ec->frame, EINA_TRUE);
             VIN("punched", evh->ec);
          }
     }
   else
     {
        if (e_comp_object_mask_has(evh->ec->frame))
          {
             e_comp_object_mask_set(evh->ec->frame, EINA_FALSE);
             VIN("Un-punched", evh->ec);
          }
     }
}
