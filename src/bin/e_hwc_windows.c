#include "e.h"
#include "services/e_service_quickpanel.h"

#define DBG_EVALUATE 1

#define ZPOS_NONE -999

#define EHWSINF(f, ec, x...)                                \
   do                                                       \
     {                                                      \
        if (!ec)                                            \
          INF("EWL|%20.20s|              |             |"f, \
              "HWC-WINS", ##x);                             \
        else                                                \
          INF("EWL|%20.20s|win:0x%08x|ec:0x%08x|"f,         \
              "HWC-WINS",                                   \
              (unsigned int)(e_client_util_win_get(ec)),    \
              (unsigned int)(ec),                           \
              ##x);                                         \
     }                                                      \
   while (0)

#define EHWSTRACE(f, ec, x...)                              \
   do                                                            \
     {                                                           \
        if (ehws_trace)                                          \
          {                                                      \
             if (!ec)                                            \
               INF("EWL|%20.20s|              |             |"f, \
                   "HWC-WINS", ##x);                             \
             else                                                \
               INF("EWL|%20.20s|win:0x%08x|ec:0x%08x|"f,         \
                   "HWC-WINS",                                   \
                   (unsigned int)(e_client_util_win_get(ec)),    \
                   (unsigned int)(ec),                           \
                   ##x);                                         \
          }                                                      \
     }                                                           \
   while (0)

static Eina_Bool ehws_trace = EINA_TRUE;

static Eina_Bool _e_hwc_windows_pp_output_data_commit(E_Hwc *hwc, E_Hwc_Window_Commit_Data *data);
static Eina_Bool _e_hwc_windows_pp_window_commit(E_Hwc *hwc, E_Hwc_Window *hwc_window);

static E_Hwc_Mode
_e_hwc_windows_hwc_mode_update(E_Hwc *hwc, int num_client, int num_device, int num_video)
{
   E_Hwc_Mode hwc_mode = E_HWC_MODE_NONE;
   int num_visible = hwc->num_visible_windows;

   if (!num_visible || (!num_device && !num_video))
     hwc_mode = E_HWC_MODE_NONE;
   else if (!num_client && (num_device || num_video))
     hwc_mode = E_HWC_MODE_FULL;
   else
     hwc_mode = E_HWC_MODE_HYBRID;

   if (hwc->hwc_mode != hwc_mode)
     {
        if (hwc_mode == E_HWC_MODE_HYBRID || hwc_mode == E_HWC_MODE_NONE)
          ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
        else
          ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);

        hwc->hwc_mode  = hwc_mode;
     }

   return hwc_mode;
}

static int
_e_hwc_windows_sort_cb(const void *d1, const void *d2)
{
   E_Hwc_Window *hwc_window_1 = (E_Hwc_Window *)d1;
   E_Hwc_Window *hwc_window_2 = (E_Hwc_Window *)d2;

   if (!hwc_window_1) return(-1);
   if (!hwc_window_2) return(1);

   return (hwc_window_2->zpos - hwc_window_1->zpos);
}

static unsigned int
_e_hwc_windows_aligned_width_get(tbm_surface_h tsurface)
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
_e_hwc_windows_commit_data_release(E_Hwc *hwc, int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec)
{
   const Eina_List *l;
   E_Hwc_Window *hwc_window;

   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
         if (!hwc_window->commit_data) continue;
         if (e_hwc_window_is_video(hwc_window))
           e_comp_wl_video_hwc_window_commit_data_release(hwc_window, sequence, tv_sec, tv_usec);

         if (!e_hwc_window_commit_data_release(hwc_window)) continue;
     }
}

static Eina_Bool
_e_hwc_windows_commit_data_aquire(E_Hwc *hwc)
{
   const Eina_List *l;
   E_Hwc_Window *hwc_window;
   Eina_Bool ret = EINA_FALSE;

   /* return TRUE when the number of the commit data is more than one */
   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        if (!e_hwc_window_commit_data_acquire(hwc_window)) continue;

        /* send frame event enlightenment doesn't send frame event in nocomp */
        if (hwc_window->ec)
          e_pixmap_image_clear(hwc_window->ec->pixmap, 1);

        if (!ret) ret = EINA_TRUE;
     }

   return ret;
}

static void
_e_hwc_windows_commit_handler(tdm_hwc *thwc, unsigned int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec,
                                  void *user_data)
{
   E_Hwc *hwc = (E_Hwc *)user_data;
   EINA_SAFETY_ON_NULL_RETURN(hwc);

   if (hwc->pp_tsurface && !hwc->output->zoom_set)
     {
        tbm_surface_internal_unref(hwc->pp_tsurface);
        hwc->pp_tsurface = NULL;
     }

   _e_hwc_windows_commit_data_release(hwc, sequence, tv_sec, tv_usec);

   EHWSTRACE("!!!!!!!! Output Commit Handler !!!!!!!!", NULL);

   /* 'wait_commit' is mechanism to make 'fetch and commit' no more than one time per a frame;
    * a 'page flip' happened so it's time to allow to make 'fetch and commit' for the e_output */
   hwc->wait_commit = EINA_FALSE;
}

static void
_e_hwc_windows_offscreen_commit(E_Hwc *hwc)
{
   E_Hwc_Window *hwc_window = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        if (!e_hwc_window_commit_data_acquire(hwc_window)) continue;

        /* send frame event enlightenment doesn't send frame event in nocomp */
        if (hwc_window->ec)
          e_pixmap_image_clear(hwc_window->ec->pixmap, 1);

        e_hwc_window_commit_data_release(hwc_window);
     }
}

static E_Hwc_Window *
_e_hwc_windows_pp_window_get(E_Hwc *hwc, tbm_surface_h tsurface)
{
   Eina_List *l;
   E_Hwc_Window *hwc_window = NULL;

   EINA_LIST_FOREACH(hwc->pp_hwc_window_list, l, hwc_window)
     {
        if (!hwc_window) continue;
        if (!hwc_window->commit_data) continue;

        if (hwc_window->commit_data->buffer.tsurface == tsurface)
          return hwc_window;
     }

   return NULL;
}

static void
_e_hwc_windows_pp_pending_data_remove(E_Hwc *hwc)
{
   E_Hwc_Window_Commit_Data *data = NULL;
   Eina_List *l = NULL, *ll = NULL;

   if (eina_list_count(hwc->pending_pp_commit_data_list) != 0)
     {
        EINA_LIST_FOREACH_SAFE(hwc->pending_pp_commit_data_list, l, ll, data)
          {
             if (!data) continue;
             hwc->pending_pp_commit_data_list = eina_list_remove_list(hwc->pending_pp_commit_data_list, l);
             tbm_surface_queue_release(hwc->pp_tqueue, data->buffer.tsurface);
             tbm_surface_internal_unref(data->buffer.tsurface);
             E_FREE(data);
          }
     }
   eina_list_free(hwc->pending_pp_commit_data_list);
   hwc->pending_pp_commit_data_list = NULL;

   if (eina_list_count(hwc->pending_pp_hwc_window_list) != 0)
     {
        E_Hwc_Window *hwc_window;
        EINA_LIST_FOREACH_SAFE(hwc->pending_pp_hwc_window_list, l, ll, hwc_window)
          {
             if (!hwc_window) continue;
             hwc->pending_pp_hwc_window_list = eina_list_remove_list(hwc->pending_pp_hwc_window_list, l);

             if (hwc_window->ec) e_pixmap_image_clear(hwc_window->ec->pixmap, 1);
             e_hwc_window_commit_data_release(hwc_window);
          }
     }
   eina_list_free(hwc->pending_pp_hwc_window_list);
   hwc->pending_pp_hwc_window_list = NULL;
}

static void
_e_hwc_windows_pp_output_commit_handler(tdm_output *toutput, unsigned int sequence,
                                              unsigned int tv_sec, unsigned int tv_usec,
                                              void *user_data)
{
   E_Hwc *hwc;
   E_Hwc_Window_Commit_Data *data = NULL;
   E_Output *output = NULL;
   const Eina_List *l;
   E_Hwc_Window *window;

   EINA_SAFETY_ON_NULL_RETURN(user_data);

   hwc = user_data;

   hwc->pp_output_commit = EINA_FALSE;

   EINA_LIST_FOREACH(hwc->hwc_windows, l, window)
     {
        if (window->commit_data && !window->commit_data->buffer.tsurface)
          e_hwc_window_commit_data_release(window);
     }

   /* layer already resetted */
   if (hwc->pp_output_commit_data)
     {
        data = hwc->pp_output_commit_data;
        hwc->pp_output_commit_data = NULL;

        /* if pp_set is false, do not deal with pending list */
        if (!hwc->pp_set)
          {
             if (hwc->pp_tsurface)
               tbm_surface_internal_unref(hwc->pp_tsurface);

             hwc->pp_tsurface = data->buffer.tsurface;
             hwc->wait_commit = EINA_FALSE;

             E_FREE(data);

             return;
          }

        if (hwc->pp_tqueue && hwc->pp_tsurface)
          {
             /* release and unref the current pp surface on the plane */
             tbm_surface_queue_release(hwc->pp_tqueue, hwc->pp_tsurface);
             tbm_surface_internal_unref(hwc->pp_tsurface);
          }

        /* set the new pp surface to the plane */
        hwc->pp_tsurface = data->buffer.tsurface;

        E_FREE(data);
     }

   EHWSTRACE("PP Output Commit Handler hwc(%p)", NULL, hwc);

   output = hwc->output;
   if (e_output_dpms_get(output))
     {
        _e_hwc_windows_pp_pending_data_remove(hwc);
        return;
     }

   /* deal with the pending layer commit */
   if (eina_list_count(hwc->pending_pp_commit_data_list) != 0)
     {
        data = eina_list_nth(hwc->pending_pp_commit_data_list, 0);
        if (data)
          {
             hwc->pending_pp_commit_data_list = eina_list_remove(hwc->pending_pp_commit_data_list, data);

             EHWSTRACE("PP Output Commit Handler start pending commit data(%p) tsurface(%p)", NULL, data, data->buffer.tsurface);

             if (!_e_hwc_windows_pp_output_data_commit(hwc, data))
               {
                  ERR("fail to _e_hwc_windows_pp_output_data_commit");
                  return;
               }
          }
     }

   /* deal with the pending pp commit */
   if (eina_list_count(hwc->pending_pp_hwc_window_list) != 0)
     {
        E_Hwc_Window *hwc_window;

        hwc_window = eina_list_nth(hwc->pending_pp_hwc_window_list, 0);
        if (hwc_window)
          {
             if (!tbm_surface_queue_can_dequeue(hwc->pp_tqueue, 0))
               return;

             hwc->pending_pp_hwc_window_list = eina_list_remove(hwc->pending_pp_hwc_window_list, hwc_window);

             if (data)
               EHWSTRACE("PP Layer Commit Handler start pending pp data(%p) tsurface(%p)", NULL, data, data->buffer.tsurface);
             else
               EHWSTRACE("PP Layer Commit Handler start pending pp data(%p) tsurface(%p)", NULL, NULL, NULL);

             if (!_e_hwc_windows_pp_window_commit(hwc, hwc_window))
               {
                  ERR("fail _e_hwc_windows_pp_data_commit");
                  e_hwc_window_commit_data_release(hwc_window);
                  return;
               }
          }
     }
}

static Eina_Bool
_e_hwc_windows_pp_output_data_commit(E_Hwc *hwc, E_Hwc_Window_Commit_Data *data)
{
   tdm_error terror;
   tdm_region fb_damage;
   E_Output *output;

   /* the damage isn't supported by hwc extension yet */
   memset(&fb_damage, 0, sizeof(fb_damage));

   EINA_SAFETY_ON_NULL_RETURN_VAL(data, EINA_FALSE);

   output = hwc->output;

   if (e_output_dpms_get(output))
     {
        _e_hwc_windows_pp_pending_data_remove(hwc);
        goto fail;
     }

   /* no need to pass composited_wnds list because smooth transition isn't
    * used is this case */
   terror = tdm_hwc_set_client_target_buffer(hwc->thwc, data->buffer.tsurface, fb_damage);
   if (terror != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_hwc_set_client_target_buffer");
        goto fail;
     }

   terror = tdm_hwc_commit(hwc->thwc, 0, _e_hwc_windows_pp_output_commit_handler, hwc);

   if (terror != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_output_commit hwc:%p", hwc);
        goto fail;
     }

   hwc->pp_output_commit = EINA_TRUE;
   hwc->pp_output_commit_data = data;

   return EINA_TRUE;

fail:
   tbm_surface_internal_unref(data->buffer.tsurface);
   tbm_surface_queue_release(hwc->pp_tqueue, data->buffer.tsurface);
   E_FREE(data);

   return EINA_FALSE;
}

static Eina_Bool
_e_hwc_windows_pp_output_commit(E_Hwc *hwc, tbm_surface_h tsurface)
{
   tbm_surface_h pp_tsurface = NULL;
   tbm_error_e tbm_err;
   E_Hwc_Window_Commit_Data *data = NULL;

   EHWSTRACE("PP Layer Commit  hwc(%p)     pp_tsurface(%p)", NULL, hwc, tsurface);

   tbm_err = tbm_surface_queue_enqueue(hwc->pp_tqueue, tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("fail tbm_surface_queue_enqueue");
        goto fail;
     }

   tbm_err = tbm_surface_queue_acquire(hwc->pp_tqueue, &pp_tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("fail tbm_surface_queue_acquire");
        goto fail;
     }

   data = E_NEW(E_Hwc_Window_Commit_Data, 1);
   if (!data) goto fail;
   data->buffer.tsurface = pp_tsurface;
   tbm_surface_internal_ref(data->buffer.tsurface);

   if (hwc->pp_output_commit)
     {
        hwc->pending_pp_commit_data_list = eina_list_append(hwc->pending_pp_commit_data_list, data);
        return EINA_TRUE;
     }

   if (!_e_hwc_windows_pp_output_data_commit(hwc, data))
     {
        ERR("fail to _e_hwc_windows_pp_output_data_commit");
        return EINA_FALSE;
     }

   return EINA_TRUE;

fail:
   tbm_surface_queue_release(hwc->pp_tqueue, tsurface);
   if (pp_tsurface && pp_tsurface != tsurface)
     tbm_surface_queue_release(hwc->pp_tqueue, pp_tsurface);

   return EINA_FALSE;
}

static void
_e_hwc_windows_pp_commit_handler(tdm_pp *pp, tbm_surface_h tsurface_src, tbm_surface_h tsurface_dst, void *user_data)
{
   E_Output *output = NULL;
   E_Hwc *hwc = NULL;
   E_Hwc_Window *hwc_window = NULL;

   hwc = (E_Hwc *)user_data;
   EINA_SAFETY_ON_NULL_RETURN(hwc);
   hwc_window = _e_hwc_windows_pp_window_get(hwc, tsurface_src);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   hwc->pp_hwc_window_list = eina_list_remove(hwc->pp_hwc_window_list, hwc_window);

   if (hwc_window->ec) e_pixmap_image_clear(hwc_window->ec->pixmap, 1);
   e_hwc_window_commit_data_release(hwc_window);

   if (eina_list_count(hwc->pending_pp_hwc_window_list) == 0)
     {
        hwc->wait_commit = EINA_FALSE;
        hwc->pp_commit = EINA_FALSE;
     }

   EHWSTRACE("PP Commit Handler hwc(%p) tsurface src(%p) dst(%p)",
             NULL, hwc, tsurface_src, tsurface_dst);

   /* if pp_set is false, skip the commit */
   if (!hwc->pp_set)
     {
        if (hwc->tpp)
          {
             tdm_pp_destroy(hwc->tpp);
             hwc->tpp = NULL;
          }
        goto done;
     }

   output = hwc->output;
   if (e_output_dpms_get(output))
     {
        _e_hwc_windows_pp_pending_data_remove(hwc);
        tbm_surface_queue_release(hwc->pp_tqueue, tsurface_dst);

        goto done;
     }

   if (!_e_hwc_windows_pp_output_commit(hwc, tsurface_dst))
     ERR("fail to _e_hwc_windows_pp_output_commit");

done:
   tbm_surface_internal_unref(tsurface_src);
   tbm_surface_internal_unref(tsurface_dst);
}

static Eina_Bool
_e_hwc_pp_windows_info_set(E_Hwc *hwc, E_Hwc_Window *hwc_window,
                                  tbm_surface_h dst_tsurface)
{
   tdm_info_pp pp_info;
   tdm_error ret = TDM_ERROR_NONE;
   unsigned int aligned_width_src = 0, aligned_width_dst = 0;
   tbm_surface_info_s surf_info_src, surf_info_dst;
   tbm_surface_h src_tsurface = hwc_window->commit_data->buffer.tsurface;

   /* when the pp_set_info is true, change the pp set_info */
   if (!hwc->pp_set_info) return EINA_TRUE;
   hwc->pp_set_info = EINA_FALSE;

   tbm_surface_get_info(src_tsurface, &surf_info_src);

   aligned_width_src = _e_hwc_windows_aligned_width_get(src_tsurface);
   if (aligned_width_src == 0) return EINA_FALSE;

   tbm_surface_get_info(dst_tsurface, &surf_info_dst);

   aligned_width_dst = _e_hwc_windows_aligned_width_get(dst_tsurface);
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

   pp_info.src_config.pos.x = hwc->pp_rect.x;
   pp_info.src_config.pos.y = hwc->pp_rect.y;
   pp_info.src_config.pos.w = hwc->pp_rect.w;
   pp_info.src_config.pos.h = hwc->pp_rect.h;
   pp_info.dst_config.pos.x = 0;
   pp_info.dst_config.pos.y = 0;
   pp_info.dst_config.pos.w = surf_info_dst.width;
   pp_info.dst_config.pos.h = surf_info_dst.height;

   ret = tdm_pp_set_info(hwc->tpp, &pp_info);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   EHWSTRACE("PP Info  Hwc(%p) src_rect(%d,%d),(%d,%d), dst_rect(%d,%d),(%d,%d)",
             NULL, hwc,
             pp_info.src_config.pos.x, pp_info.src_config.pos.y, pp_info.src_config.pos.w, pp_info.src_config.pos.h,
             pp_info.dst_config.pos.x, pp_info.dst_config.pos.y, pp_info.dst_config.pos.w, pp_info.dst_config.pos.h);

   return EINA_TRUE;
}

static Eina_Bool
_e_hwc_windows_pp_window_commit(E_Hwc *hwc, E_Hwc_Window *hwc_window)
{
   E_Output *output = NULL;
   tbm_surface_h pp_tsurface = NULL;
   tbm_error_e tbm_err = TBM_ERROR_NONE;
   tdm_error terror = TDM_ERROR_NONE;
   E_Hwc_Window_Commit_Data *commit_data = hwc_window->commit_data;
   EINA_SAFETY_ON_FALSE_RETURN_VAL(commit_data, EINA_FALSE);

   tbm_surface_h tsurface = commit_data->buffer.tsurface;

   EHWSTRACE("PP Commit  Hwc(%p)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
             NULL, hwc, commit_data->buffer.tsurface, hwc->pp_tqueue,
             commit_data->buffer_ref.buffer ? commit_data->buffer_ref.buffer->resource : NULL, commit_data);

   output = hwc->output;
   if (e_output_dpms_get(output))
     {
        _e_hwc_windows_pp_pending_data_remove(hwc);
        return EINA_FALSE;
     }

   tbm_err = tbm_surface_queue_dequeue(hwc->pp_tqueue, &pp_tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("fail tbm_surface_queue_dequeue");
        return EINA_FALSE;
     }

   if (!_e_hwc_pp_windows_info_set(hwc, hwc_window, pp_tsurface))
     {
        ERR("fail _e_hwc_windows_info_set");
        goto pp_fail;
     }

   terror = tdm_pp_set_done_handler(hwc->tpp, _e_hwc_windows_pp_commit_handler, hwc);
   EINA_SAFETY_ON_FALSE_GOTO(terror == TDM_ERROR_NONE, pp_fail);

   tbm_surface_internal_ref(pp_tsurface);
   tbm_surface_internal_ref(commit_data->buffer.tsurface);
   terror = tdm_pp_attach(hwc->tpp, commit_data->buffer.tsurface, pp_tsurface);
   EINA_SAFETY_ON_FALSE_GOTO(terror == TDM_ERROR_NONE, attach_fail);

   hwc->pp_hwc_window_list = eina_list_append(hwc->pp_hwc_window_list, hwc_window);

   terror = tdm_pp_commit(hwc->tpp);
   EINA_SAFETY_ON_FALSE_GOTO(terror == TDM_ERROR_NONE, commit_fail);

   hwc->wait_commit = EINA_TRUE;
   hwc->pp_commit = EINA_TRUE;

   return EINA_TRUE;

commit_fail:
   hwc->pp_hwc_window_list = eina_list_remove(hwc->pp_hwc_window_list, hwc_window);
attach_fail:
   tbm_surface_internal_unref(pp_tsurface);
   tbm_surface_internal_unref(tsurface);
pp_fail:
   tbm_surface_queue_release(hwc->pp_tqueue, pp_tsurface);

   ERR("failed _e_hwc_windows_pp_data_commit");

   return EINA_FALSE;
}

static E_Hwc_Window *
_e_hwc_windows_pp_get_hwc_window_for_zoom(E_Hwc *hwc)
{
   const Eina_List *l;
   E_Hwc_Window *hwc_window = NULL;
   E_Hwc_Window *hwc_window_for_zoom = NULL;
   int num = 0;
   int w, h;

   e_output_size_get(hwc->output, &w, &h);

   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
   {
      if (!e_hwc_window_is_on_hw_overlay(hwc_window)) continue;

      hwc_window_for_zoom = hwc_window;
      num++;
   }

   if (num != 1) return NULL;
   if (!hwc_window_for_zoom->buffer.tsurface) return NULL;
   if (tbm_surface_get_width(hwc_window_for_zoom->buffer.tsurface) != w ||
       tbm_surface_get_height(hwc_window_for_zoom->buffer.tsurface) != h)
     return NULL;

   return hwc_window_for_zoom;
}

static Eina_Bool
_e_hwc_windows_pp_commit(E_Hwc *hwc)
{
   E_Hwc_Window_Commit_Data *commit_data = NULL;
   E_Hwc_Window *hwc_window = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc->pp_tqueue, EINA_FALSE);

   hwc_window = _e_hwc_windows_pp_get_hwc_window_for_zoom(hwc);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   commit_data = hwc_window->commit_data;
   if (!commit_data) return EINA_TRUE;
   if (!commit_data->buffer.tsurface) return EINA_TRUE;

   if (!tbm_surface_queue_can_dequeue(hwc->pp_tqueue, 0))
     {
        EHWSTRACE("PP Commit  Can Dequeue failed Hwc(%p)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
                  NULL, hwc, commit_data->buffer.tsurface, hwc->pp_tqueue,
                  commit_data->buffer_ref.buffer ? commit_data->buffer_ref.buffer->resource : NULL, commit_data);
        hwc->pending_pp_hwc_window_list = eina_list_append(hwc->pending_pp_hwc_window_list, hwc_window);

        hwc->wait_commit = EINA_TRUE;

        return EINA_TRUE;
     }

   if (eina_list_count(hwc->pending_pp_hwc_window_list) != 0)
     {
        EHWSTRACE("PP Commit  Pending pp data remained Hwc(%p)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
                  NULL, hwc, commit_data->buffer.tsurface, hwc->pp_tqueue,
                  commit_data->buffer_ref.buffer ? commit_data->buffer_ref.buffer->resource : NULL, commit_data);
        hwc->pending_pp_hwc_window_list = eina_list_append(hwc->pending_pp_hwc_window_list, hwc_window);

        hwc->wait_commit = EINA_TRUE;

        return EINA_TRUE;
     }

   if (!_e_hwc_windows_pp_window_commit(hwc, hwc_window))
     {
        ERR("fail _e_hwc_windows_pp_data_commit");
        e_hwc_window_commit_data_release(hwc_window);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_hwc_windows_status_print(E_Hwc *hwc, Eina_Bool with_target)
{
    const Eina_List *l;
    Eina_List *sort_wnds;
    E_Hwc_Window *hwc_window;

    sort_wnds = eina_list_clone(hwc->hwc_windows);
    sort_wnds = eina_list_sort(sort_wnds, eina_list_count(sort_wnds), _e_hwc_windows_sort_cb);

    EINA_LIST_FOREACH(sort_wnds, l, hwc_window)
      {
         if (hwc_window->state == E_HWC_WINDOW_STATE_NONE) continue;

         if (e_hwc_window_is_target(hwc_window))
           {
              if (!with_target) continue;

              EHWSTRACE("  ehw:%p ts:%p -- {%25s}, state:%s",
                        NULL, hwc_window,
                        hwc_window->buffer.tsurface, "@TARGET WINDOW@",
                        e_hwc_window_state_string_get(hwc_window->state));
              continue;
           }

         EHWSTRACE("  ehw:%p ts:%p -- {%25s}, state:%s, zpos:%d, deleted:%s",
                   hwc_window->ec, hwc_window,
                   hwc_window->buffer.tsurface, e_hwc_window_name_get(hwc_window),
                   e_hwc_window_state_string_get(hwc_window->state),
                   hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
      }

    eina_list_free(sort_wnds);
}

static E_Hwc_Window *
_e_hwc_windows_window_find_by_twin(E_Hwc *hwc, tdm_hwc_window *hwc_win)
{
   Eina_List *l;
   E_Hwc_Window *hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_win, NULL);

   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        if (hwc_window->thwc_window == hwc_win) return hwc_window;
     }

   return NULL;
}

static E_Hwc_Window_State
_e_hwc_windows_window_state_get(tdm_hwc_window_composition composition_type)
{
   E_Hwc_Window_State state = E_HWC_WINDOW_STATE_NONE;

   switch (composition_type)
     {
      case TDM_COMPOSITION_NONE:
        state = E_HWC_WINDOW_STATE_NONE;
        break;
      case TDM_COMPOSITION_CLIENT:
        state = E_HWC_WINDOW_STATE_CLIENT;
        break;
      case TDM_COMPOSITION_DEVICE:
        state = E_HWC_WINDOW_STATE_DEVICE;
        break;
      case TDM_COMPOSITION_CURSOR:
        state = E_HWC_WINDOW_STATE_CURSOR;
        break;
      case TDM_COMPOSITION_VIDEO:
        state = E_HWC_WINDOW_STATE_VIDEO;
        break;
      default:
        state = E_HWC_WINDOW_STATE_NONE;
        ERR("HWC-WINS: unknown state of hwc_window.");
     }

   return state;
}

static Eina_Bool
_e_hwc_windows_transition_check(E_Hwc *hwc)
{
   E_Hwc_Window *hwc_window = NULL;

   Eina_Bool transition = EINA_FALSE;
   const Eina_List *l;

   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        if (e_hwc_window_is_target(hwc_window)) continue;
        if (e_hwc_window_is_video(hwc_window)) continue;

        if (hwc_window->state == hwc_window->accepted_state) continue;

        /* DEVICE -> CLIENT */
        if (hwc_window->state == E_HWC_WINDOW_STATE_CLIENT &&
            hwc_window->accepted_state == E_HWC_WINDOW_STATE_DEVICE)
          {
             if (!e_hwc_window_is_on_target_window(hwc_window))
               {
                  hwc_window->transition = E_HWC_WINDOW_TRANSITION_DEVICE_TO_CLIENT;
                  transition = EINA_TRUE;
               }
          }
        /* CURSOR -> CLIENT */
        else if (hwc_window->state == E_HWC_WINDOW_STATE_CLIENT &&
                 hwc_window->accepted_state == E_HWC_WINDOW_STATE_CURSOR)
          {
             if (!e_hwc_window_is_on_target_window(hwc_window))
               {
                  hwc_window->transition = E_HWC_WINDOW_TRANSITION_CURSOR_TO_CLIENT;
                  transition = EINA_TRUE;
               }
          }
        /* DEVICE -> NONE */
        else if (hwc_window->state == E_HWC_WINDOW_STATE_DEVICE &&
                 hwc_window->accepted_state == E_HWC_WINDOW_STATE_NONE)
          {
             if (e_hwc_window_is_on_target_window(hwc_window))
               {
                  hwc_window->transition = E_HWC_WINDOW_TRANSITION_CLIENT_TO_NONE;
                  transition = EINA_TRUE;
               }
          }
        /* CURSOR -> NONE */
        else if (hwc_window->state == E_HWC_WINDOW_STATE_CURSOR &&
                 hwc_window->accepted_state == E_HWC_WINDOW_STATE_NONE)
          {
             if (e_hwc_window_is_on_target_window(hwc_window))
               {
                  hwc_window->transition = E_HWC_WINDOW_TRANSITION_CURSOR_TO_NONE;
                  transition = EINA_TRUE;
               }
          }
        /* CLIENT -> DEVICE */
        else if (hwc_window->state == E_HWC_WINDOW_STATE_DEVICE &&
                 hwc_window->accepted_state == E_HWC_WINDOW_STATE_CLIENT)
          {
             if (e_hwc_window_is_on_target_window(hwc_window))
               {
                  hwc_window->transition = E_HWC_WINDOW_TRANSITION_CLIENT_TO_DEVICE;
                  transition = EINA_TRUE;
               }
          }
        /* CLIENT -> CURSOR */
        else if (hwc_window->state == E_HWC_WINDOW_STATE_CURSOR &&
                 hwc_window->accepted_state == E_HWC_WINDOW_STATE_CLIENT)
          {
             if (e_hwc_window_is_on_target_window(hwc_window))
               {
                  hwc_window->transition = E_HWC_WINDOW_TRANSITION_CLIENT_TO_CURSOR;
                  transition = EINA_TRUE;
               }
          }
     }

    return transition;
}

static Eina_Bool
_e_hwc_windows_validated_changes_update(E_Hwc *hwc, uint32_t num_changes)
{
   E_Hwc_Window *hwc_window;
   E_Hwc_Window_Target *target_hwc_window;
   E_Hwc_Window_State state;
   tdm_error terror;
   tdm_hwc_window **changed_hwc_window = NULL;
   tdm_hwc_window_composition *composition_types = NULL;
   int i;

   changed_hwc_window = E_NEW(tdm_hwc_window *, num_changes);
   EINA_SAFETY_ON_NULL_GOTO(changed_hwc_window, fail);

   composition_types = E_NEW(tdm_hwc_window_composition, num_changes);
   EINA_SAFETY_ON_NULL_GOTO(composition_types, fail);

   target_hwc_window = hwc->target_hwc_window;
   EINA_SAFETY_ON_NULL_GOTO(target_hwc_window, fail);

   terror = tdm_hwc_get_changed_composition_types(hwc->thwc,
                                                  &num_changes, changed_hwc_window,
                                                  composition_types);
   if (terror != TDM_ERROR_NONE)
     {
        ERR("HWC-WINS: failed to get changed composition types");
        goto fail;
     }

   EHWSTRACE("Changes NUM : %d", NULL, num_changes);

   for (i = 0; i < num_changes; ++i)
     {
        hwc_window = _e_hwc_windows_window_find_by_twin(hwc, changed_hwc_window[i]);
        if (!hwc_window)
          {
             ERR("HWC-WINS: cannot find the E_Hwc_Window by hwc hwc_window");
             goto fail;
          }

        /* update the state with the changed compsition */
        state = _e_hwc_windows_window_state_get(composition_types[i]);
        e_hwc_window_state_set(hwc_window, state, EINA_TRUE);
     }

#if DBG_EVALUATE
   EHWSTRACE(" Modified after HWC Validation:", NULL);
   _e_hwc_windows_status_print(hwc, EINA_FALSE);
#endif

   free(changed_hwc_window);
   free(composition_types);

   return EINA_TRUE;

fail:
   if (changed_hwc_window) free(changed_hwc_window);
   if (composition_types) free(composition_types);

   return EINA_FALSE;
}

static Eina_Bool
_e_hwc_windows_accept(E_Hwc *hwc)
{
   E_Hwc_Window *hwc_window;
   E_Hwc_Window_State state;
   tdm_error terror;
   const Eina_List *l;

   /* _e_hwc_windows_accept */
   EHWSTRACE("HWC Accept", NULL);

   /* accept changes */
   terror = tdm_hwc_accept_changes(hwc->thwc);
   if (terror != TDM_ERROR_NONE)
     {
        ERR("HWC-WINS: failed to accept changes.");
        return EINA_FALSE;
     }

   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        if (hwc_window->is_deleted) continue;
        if (e_hwc_window_is_target(hwc_window)) continue;

        /* update the accepted_state */
        state = e_hwc_window_state_get(hwc_window);
        e_hwc_window_accepted_state_set(hwc_window, state);

        /* notify the hwc_window that it will be displayed on hw layer */
        if (!hwc_window->queue &&
            !e_hwc_window_is_video(hwc_window) &&
            e_hwc_window_is_on_hw_overlay(hwc_window))
          e_hwc_window_activate(hwc_window, NULL);
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_hwc_windows_validate(E_Hwc *hwc, uint32_t *num_changes)
{
   E_Output *output = hwc->output;
   tdm_error terror;
   tdm_output *toutput = output->toutput;
   tdm_hwc_window **thwc_windows = NULL;
   int i, n_thw;
   E_Hwc_Window *hwc_window;
   const Eina_List *l;
   Eina_List *visible_windows = hwc->visible_windows;

#if DBG_EVALUATE
   EHWSTRACE(" Request HWC Validation to TDM HWC:", NULL);
   _e_hwc_windows_status_print(hwc, EINA_FALSE);
#endif

   n_thw = eina_list_count(visible_windows);
   if (n_thw)
     {
        thwc_windows = E_NEW(tdm_hwc_window *, n_thw);
        EINA_SAFETY_ON_NULL_GOTO(thwc_windows, error);

        i = 0;
        EINA_LIST_FOREACH(visible_windows, l, hwc_window)
          thwc_windows[i++] = hwc_window->thwc_window;
     }

   /* make hwc extension choose which clients will own hw overlays */
   terror = tdm_hwc_validate(hwc->thwc, thwc_windows, n_thw, num_changes);
   if (terror != TDM_ERROR_NONE) goto error;

   E_FREE(thwc_windows);

   return EINA_TRUE;

error:
   ERR("HWC-WINS: failed to validate the output(%p)", toutput);
   E_FREE(thwc_windows);

   return EINA_FALSE;
}

static Eina_Bool
_e_hwc_windows_target_window_render(E_Output *output, E_Hwc_Window_Target *target_hwc_window)
{
   if (target_hwc_window->hwc_window.state == E_HWC_WINDOW_STATE_NONE) return EINA_TRUE;

   if (e_comp_canvas_norender_get() > 0)
     {
        EHWSTRACE(" NoRender get. Do not ecore_evas_manual_render.", NULL);
        return EINA_TRUE;
     }

   if (e_hwc_window_target_can_render(target_hwc_window))
     {
        TRACE_DS_BEGIN(MANUAL RENDER);
        target_hwc_window->is_rendering = EINA_TRUE;
        ecore_evas_manual_render(target_hwc_window->ee);
        target_hwc_window->is_rendering = EINA_FALSE;
        TRACE_DS_END();
     }

   return EINA_TRUE;
}

static Eina_List *
_e_hwc_windows_visible_windows_list_get(E_Hwc *hwc)
{
   Eina_List *windows_list = NULL;
   E_Hwc_Window *hwc_window;
   E_Client  *ec;
   Evas_Object *o;
   int scr_w, scr_h;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (!ec->hwc_window) continue;

        hwc_window = ec->hwc_window;

        if (e_object_is_del(E_OBJECT(ec)))
          {
             e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_NONE, EINA_TRUE);
             continue;
          }

        // check clients to skip composite
        if (e_client_util_ignored_get(ec))
          {
             e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_NONE, EINA_TRUE);
             continue;
          }

        // check clients to skip composite
        if (!evas_object_visible_get(ec->frame))
          {
             e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_NONE, EINA_TRUE);
             continue;
          }

        // check geometry if located out of screen such as quick panel
        ecore_evas_geometry_get(e_comp->ee, NULL, NULL, &scr_w, &scr_h);
        if (!E_INTERSECTS(0, 0, scr_w, scr_h, ec->client.x, ec->client.y, ec->client.w, ec->client.h))
          {
             e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_NONE, EINA_TRUE);
             continue;
          }

        if (evas_object_data_get(ec->frame, "comp_skip"))
          {
             e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_NONE, EINA_TRUE);
             continue;
          }

        /* skip all small clients except the video clients */
        if ((ec->w == 1 || ec->h == 1) && !e_hwc_window_is_video(hwc_window))
          {
             e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_NONE, EINA_TRUE);
             continue;
          }

        if (e_hwc_window_is_video(hwc_window))
          {
            if (!e_comp_wl_video_hwc_widow_surface_get(hwc_window))
              continue;

            e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_VIDEO, EINA_TRUE);
          }

        windows_list = eina_list_append(windows_list, hwc_window);
     }

   return windows_list;
}

static Eina_Bool
_e_hwc_windows_all_client_states_available_check(E_Hwc *hwc)
{
   Eina_List *l;
   E_Client *ec;
   E_Hwc_Window *hwc_window = NULL;
   Eina_List *visible_windows = hwc->visible_windows;

   /* make the full_gl_composite when the zoom is enabled */
   if (hwc->output->zoom_set) return EINA_TRUE;

   /* full composite is forced to be set */
   if (e_hwc_deactive_get(hwc)) return EINA_TRUE;

   /* hwc_window manager required full GLES composition */
   if (e_comp->nocomp_override > 0)
     {
        EHWSTRACE("  HWC_MODE_NONE due to nocomp_override > 0.", NULL);
        return EINA_TRUE;
     }

   EINA_LIST_FOREACH(visible_windows, l, hwc_window)
     {
        ec = hwc_window->ec;

        if (e_hwc_window_is_video(hwc_window)) continue;

        // if there is a ec which is lower than quickpanel and quickpanel is opened.
        if (E_POLICY_QUICKPANEL_LAYER >= evas_object_layer_get(ec->frame))
          {
             // check whether quickpanel is open than break
             if (e_config->use_desk_smart_obj && e_qp_visible_get())
               {
                   EHWSTRACE("    HWC_MODE_NONE due to quickpanel is opened.{%25s}.",
                             ec, ec->icccm.title);
                   return EINA_TRUE;
               }
          }

        // if ec->frame is not for client buffer (e.g. launchscreen)
        if (e_comp_object_content_type_get(ec->frame) != E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE)
          {
             EHWSTRACE("  HWC_MODE_NONE due to E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE{%25s}.",
                       ec, ec->icccm.title);
             return EINA_TRUE;
          }

        // if there is UI subfrace, it means need to composite
        if (e_client_normal_client_has(ec))
          {
            EHWSTRACE("  HWC_MODE_NONE due to UI subfrace{%25s}.",
                      ec, ec->icccm.title);
            return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

static void
_e_hwc_windows_visible_windows_states_update(E_Hwc *hwc)
{
   Eina_List *visible_windows = NULL;
   Eina_List *l;
   E_Hwc_Window *hwc_window = NULL;

   /* get the visible ecs */
   visible_windows = hwc->visible_windows;

   /* check if e20 forces to set that all window has TDM_COMPOSITION_CLIENT types */
   if (_e_hwc_windows_all_client_states_available_check(hwc))
     {
        EINA_LIST_FOREACH(visible_windows, l, hwc_window)
          {
             /* The video window set the TDM_COMPOSITION_VIDEO type. */
             if (e_hwc_window_is_video(hwc_window))
               {
                  if (!e_hwc_window_composition_update(hwc_window))
                    ERR("HWC-WINS: cannot update E_Hwc_Window(%p)", hwc_window);
                  continue;
               }

             e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_CLIENT, EINA_TRUE);

             EHWSTRACE("   ehw:%p -- {%25s} is NOT hwc_acceptable.",
                     hwc_window->ec, hwc_window, hwc_window->ec->icccm.title);
          }
     }
   else
     {
        /* check clients are able to use hwc */
        EINA_LIST_FOREACH(visible_windows, l, hwc_window)
          {
             /* The video window set the TDM_COMPOSITION_VIDEO type. */
             if (e_hwc_window_is_video(hwc_window))
               {
                  if (!e_hwc_window_composition_update(hwc_window))
                    ERR("HWC-WINS: cannot update E_Hwc_Window(%p)", hwc_window);
                  continue;
               }

             /* filter the visible clients which e20 prevent to shown by hw directly
                by demand of e20 */
             if (e_hwc_window_device_state_available_check(hwc_window))
               e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_DEVICE, EINA_TRUE);
             else
               e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_CLIENT, EINA_TRUE);
          }
     }
}

static Eina_Bool
_e_hwc_windows_visible_windows_changed_check(E_Hwc *hwc, Eina_List *visible_windows, int visible_num)
{
   Eina_List *prev_visible_windows = NULL;
   E_Hwc_Window *hw1, *hw2;
   int i;

   prev_visible_windows = hwc->visible_windows;

   if (!prev_visible_windows) return EINA_TRUE;

   if (eina_list_count(prev_visible_windows) != visible_num)
     return EINA_TRUE;

   for (i = 0; i < visible_num; i++)
     {
        hw1 = eina_list_nth(prev_visible_windows, i);
        hw2 = eina_list_nth(visible_windows, i);
        if (hw1 != hw2) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_hwc_windows_visible_windows_update(E_Hwc *hwc)
{
   E_Hwc_Window *hwc_window;
   Eina_List *l;
   Eina_List *visible_windows;
   int visible_num = 0;
   int zpos = 0;

   /* get the visibile windows */
   visible_windows = _e_hwc_windows_visible_windows_list_get(hwc);
   if (!visible_windows && !hwc->visible_windows)
     return EINA_FALSE;

   visible_num = eina_list_count(visible_windows);

   if (!_e_hwc_windows_visible_windows_changed_check(hwc, visible_windows, visible_num))
     return EINA_FALSE;

   EINA_LIST_FREE(hwc->visible_windows, hwc_window)
     e_object_unref(E_OBJECT(hwc_window));

   /* store the current visible windows and the number of them */
   hwc->visible_windows = eina_list_clone(visible_windows);
   hwc->num_visible_windows = visible_num;

   /* use the reverse iteration for assgining the zpos */
   EINA_LIST_REVERSE_FOREACH(hwc->visible_windows, l, hwc_window)
     {
        /* assign zpos */
        e_hwc_window_zpos_set(hwc_window, zpos++);
        e_object_ref(E_OBJECT(hwc_window));
     }

   return EINA_TRUE;
}

/* check if there is a need to update the output */
static Eina_Bool
_e_hwc_windows_changes_update(E_Hwc *hwc)
{
   E_Hwc_Window *hwc_window = NULL;
   Eina_Bool update_changes = EINA_FALSE;
   const Eina_List *l;

   /* fetch the target buffer */
   if (e_hwc_window_target_buffer_fetch(hwc->target_hwc_window)) // try aquire
     update_changes = EINA_TRUE;

   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        if (e_hwc_window_is_target(hwc_window)) continue;

        /* fetch the window buffer */
        if (e_hwc_window_buffer_fetch(hwc_window))
          update_changes = EINA_TRUE;
        else
          {
             /* sometimes client add frame cb without buffer attach */
             if (hwc_window->ec &&
                 hwc_window->accepted_state == E_HWC_WINDOW_STATE_DEVICE)
               e_pixmap_image_clear(hwc_window->ec->pixmap, 1);
          }

        /* update the window's info */
        if (e_hwc_window_info_update(hwc_window))
          update_changes = EINA_TRUE;
     }

   /* update the the visible windows */
   if (_e_hwc_windows_visible_windows_update(hwc))
     update_changes = EINA_TRUE;

   /* update the states of the visible windows when there is something to update */
   if (update_changes)
     _e_hwc_windows_visible_windows_states_update(hwc);

   return update_changes;
}

static void
_e_hwc_windows_target_state_set(E_Hwc_Window_Target *target_hwc_window, E_Hwc_Window_State state)
{
   E_Hwc_Window *target_window = (E_Hwc_Window *)target_hwc_window;

   if (target_window->state != state)
     e_hwc_window_state_set(target_window, state, EINA_FALSE);

   if (target_window->accepted_state != state)
     e_hwc_window_accepted_state_set(target_window, state);
}

/* evaluate the hwc_windows */
static Eina_Bool
_e_hwc_windows_evaluate(E_Hwc *hwc)
{
   E_Hwc_Mode hwc_mode = E_HWC_MODE_NONE;
   E_Hwc_Window *hwc_window = NULL;
   const Eina_List *l;
   uint32_t num_changes;
   int num_client = 0, num_device = 0, num_video = 0;

   /* validate the visible hwc_windows' states*/
   if (!_e_hwc_windows_validate(hwc, &num_changes))
     {
        ERR("HWC-WINS: _e_hwc_windows_validate failed.");
        goto re_evaluate;
     }

   /* update the valiated_changes if there are the composition changes after validation */
   if (num_changes)
     {
        if (!_e_hwc_windows_validated_changes_update(hwc, num_changes))
          {
             ERR("HWC-WINS: _e_hwc_windows_validated_changes_update failed.");
             goto re_evaluate;
          }
     }

   /* constraints update and update the windows to be composited to the target_buffer */
   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        if (e_hwc_window_is_target(hwc_window)) continue;

        e_hwc_window_constraints_update(hwc_window);
        e_hwc_window_render_target_window_update(hwc_window);

        if (hwc_window->state == E_HWC_WINDOW_STATE_CLIENT) num_client++;
        if (hwc_window->state == E_HWC_WINDOW_STATE_DEVICE) num_device++;
        if (hwc_window->state == E_HWC_WINDOW_STATE_VIDEO) num_video++;
     }

   /* update the E_HWC_MODE */
   hwc_mode = _e_hwc_windows_hwc_mode_update(hwc, num_client, num_device, num_video);

   /* set the state of the target_window */
   if (hwc_mode == E_HWC_MODE_NONE)
     {
        EHWSTRACE(" HWC_MODE is NONE composition.", NULL);
        _e_hwc_windows_target_state_set(hwc->target_hwc_window, E_HWC_WINDOW_STATE_DEVICE);
     }
   else if (hwc_mode == E_HWC_MODE_HYBRID)
     {
        EHWSTRACE(" HWC_MODE is HYBRID composition.", NULL);
        _e_hwc_windows_target_state_set(hwc->target_hwc_window, E_HWC_WINDOW_STATE_DEVICE);
     }
   else
     {
        EHWSTRACE(" HWC_MODE is FULL HW composition.", NULL);
        _e_hwc_windows_target_state_set(hwc->target_hwc_window, E_HWC_WINDOW_STATE_NONE);
     }

   /* skip the target_buffer when the window is on trainsition of the composition */
   if (hwc_mode != E_HWC_MODE_FULL && _e_hwc_windows_transition_check(hwc))
     {
        e_hwc_window_target_buffer_skip(hwc->target_hwc_window);
        goto re_evaluate;
     }

   /* accept the result of the validation */
   if (!_e_hwc_windows_accept(hwc))
     {
        ERR("HWC-WINS: _e_hwc_windows_validated_changes_update failed.");
        goto re_evaluate;
     }

   EHWSTRACE(" Succeed the compsition_evaulation.", NULL);

   return EINA_TRUE;

re_evaluate:
   EHWSTRACE(" Need the comopsition re-evaulation.", NULL);

   return EINA_FALSE;
}

static Eina_Bool
_e_hwc_windows_target_buffer_prepared(E_Hwc *hwc)
{
   E_Hwc_Window *hwc_window = NULL;

   hwc_window = (E_Hwc_Window *)hwc->target_hwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc->target_hwc_window, EINA_FALSE);

   if (!hwc_window->buffer.tsurface) return EINA_FALSE;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_windows_init(E_Hwc *hwc)
{
   return EINA_TRUE;
}

EINTERN void
e_hwc_windows_deinit(void)
{
}

EINTERN Eina_Bool
e_hwc_windows_render(E_Hwc *hwc)
{
   E_Output *output = hwc->output;
   E_Hwc_Window_Target *target_hwc_window;

   target_hwc_window = hwc->target_hwc_window;
   if (!target_hwc_window)
     {
        ERR("fail to get target hwc_window for output(%p).", output);
        return EINA_FALSE;
     }

   if (!_e_hwc_windows_target_window_render(output, target_hwc_window))
     ERR("fail to render output(%p).", output);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_windows_commit(E_Hwc *hwc)
{
   E_Output *output = NULL;
   tdm_error error = TDM_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   output = hwc->output;

   if (hwc->wait_commit) return EINA_TRUE;

   if (e_comp_canvas_norender_get() > 0)
     {
        EHWSTRACE(" Block Display... NoRender get.", NULL);
        return EINA_TRUE;
     }

   if (!_e_hwc_windows_changes_update(hwc))
     return EINA_TRUE;

   if (!_e_hwc_windows_evaluate(hwc))
     return EINA_TRUE;

   if (hwc->hwc_mode != E_HWC_MODE_FULL) {
     if (!_e_hwc_windows_target_buffer_prepared(hwc))
       return EINA_TRUE;
   }

   if (output->dpms == E_OUTPUT_DPMS_OFF)
     {
        _e_hwc_windows_offscreen_commit(hwc);
        return EINA_TRUE;
     }

   if (!_e_hwc_windows_commit_data_aquire(hwc))
     return EINA_TRUE;

   EHWSTRACE("!!!!!!!! HWC Commit !!!!!!!!", NULL);

   if (output->zoom_set)
     {
        e_output_zoom_rotating_check(output);
        EHWSTRACE("###### PP Commit", NULL);
        if (!_e_hwc_windows_pp_commit(hwc))
          {
            ERR("_e_hwc_windows_pp_commit failed.");
            goto fail;
          }
     }
   else
     {
        error = tdm_hwc_commit(hwc->thwc, 0, _e_hwc_windows_commit_handler, hwc);
        if (error != TDM_ERROR_NONE)
          {
             ERR("tdm_hwc_commit failed.");
             _e_hwc_windows_commit_handler(hwc->thwc, 0, 0, 0, hwc);
             goto fail;
          }

        hwc->wait_commit = EINA_TRUE;
     }

   return EINA_TRUE;

fail:
   return EINA_FALSE;
}

EINTERN Eina_Bool
e_hwc_windows_pp_commit_possible_check(E_Hwc *hwc)
{
   if (!hwc->pp_set) return EINA_FALSE;

   if (hwc->pp_tqueue)
     {
        if (!tbm_surface_queue_can_dequeue(hwc->pp_tqueue, 0))
          return EINA_FALSE;
     }

   if (hwc->pending_pp_hwc_window_list)
     {
        if (eina_list_count(hwc->pending_pp_hwc_window_list) != 0)
          return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_windows_zoom_set(E_Hwc *hwc, Eina_Rectangle *rect)
{
   E_Comp_Screen *e_comp_screen = NULL;
   tdm_error ret = TDM_ERROR_NONE;
   int w, h;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   if ((hwc->pp_rect.x == rect->x) &&
       (hwc->pp_rect.y == rect->y) &&
       (hwc->pp_rect.w == rect->w) &&
       (hwc->pp_rect.h == rect->h))
     return EINA_TRUE;

   e_comp_screen = e_comp->e_comp_screen;
   e_output_size_get(hwc->output, &w, &h);

   if (!hwc->tpp)
     {
        hwc->tpp = tdm_display_create_pp(e_comp_screen->tdisplay, &ret);
        if (ret != TDM_ERROR_NONE)
          {
             ERR("fail tdm pp create");
             goto fail;
          }
     }

   if (!hwc->pp_tqueue)
     {
        //TODO: Does e20 get the buffer flags from the tdm backend?
        hwc->pp_tqueue = tbm_surface_queue_create(3, w, h, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
        if (!hwc->pp_tqueue)
          {
             ERR("fail tbm_surface_queue_create");
             goto fail;
          }
     }

   hwc->pp_rect.x = rect->x;
   hwc->pp_rect.y = rect->y;
   hwc->pp_rect.w = rect->w;
   hwc->pp_rect.h = rect->h;

   hwc->pp_set = EINA_TRUE;
   hwc->target_hwc_window->skip_surface_set = EINA_TRUE;
   hwc->pp_set_info = EINA_TRUE;

   /* to wake up main loop */
   uint64_t value = 1;
   if (write(hwc->target_hwc_window->event_fd, &value, sizeof(value)) < 0)
     ERR("failed to wake up main loop:%m");

   return EINA_TRUE;

fail:
   if (hwc->tpp)
     {
        tdm_pp_destroy(hwc->tpp);
        hwc->tpp = NULL;
     }

   return EINA_FALSE;
}

EINTERN void
e_hwc_windows_zoom_unset(E_Hwc *hwc)
{
   EINA_SAFETY_ON_NULL_RETURN(hwc);

   hwc->pp_set_info = EINA_FALSE;
   hwc->target_hwc_window->skip_surface_set = EINA_FALSE;
   hwc->pp_set = EINA_FALSE;

   hwc->pp_rect.x = 0;
   hwc->pp_rect.y = 0;
   hwc->pp_rect.w = 0;
   hwc->pp_rect.h = 0;

   _e_hwc_windows_pp_pending_data_remove(hwc);

   if (hwc->pp_tsurface)
     tbm_surface_queue_release(hwc->pp_tqueue, hwc->pp_tsurface);

   if (hwc->pp_tqueue)
     {
        tbm_surface_queue_destroy(hwc->pp_tqueue);
        hwc->pp_tqueue = NULL;
     }

   if (!hwc->pp_commit)
     {
        if (hwc->tpp)
          {
             tdm_pp_destroy(hwc->tpp);
             hwc->tpp = NULL;
          }
     }

   if (hwc->pp_output_commit_data)
     hwc->wait_commit = EINA_TRUE;

   /* to wake up main loop */
   uint64_t value = 1;
   if (write(hwc->target_hwc_window->event_fd, &value, sizeof(value)) < 0)
     ERR("failed to wake up main loop:%m");
}
