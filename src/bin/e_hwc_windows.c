#include "e.h"
#include "services/e_service_quickpanel.h"
# include <Evas_Engine_GL_Tbm.h>
# include <Evas_Engine_Software_Tbm.h>
# include <sys/eventfd.h>

#define DBG_EVALUATE 1

#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof (x))
#endif

#define EHWSERR(f, hwc, x...)                                     \
   do                                                                 \
     {                                                                \
        ERR("EWL|%20.20s|              |             |%8s|"f,       \
            "HWC-WINS", (e_hwc_output_id_get(hwc)), ##x);           \
     }                                                                \
   while (0)

#define EHWSINF(f, ec, hwc, x...)                                     \
   do                                                                 \
     {                                                                \
        if (!ec)                                                      \
          INF("EWL|%20.20s|              |             |%8s|"f,       \
              "HWC-WINS", (e_hwc_output_id_get(hwc)), ##x);           \
        else                                                          \
          INF("EWL|%20.20s|win:0x%08zx|ec:%8p|%8s|"f,                 \
              "HWC-WINS",                                             \
              (e_client_util_win_get(ec)),                            \
              (ec), (e_hwc_output_id_get(hwc)),                       \
              ##x);                                                   \
     }                                                                \
   while (0)

#define EHWSTRACE(f, ec, hwc, x...)                                   \
   do                                                                 \
     {                                                                \
        if (ehws_trace)                                               \
          {                                                           \
             if (!ec)                                                 \
               INF("EWL|%20.20s|              |             |%8s|"f,  \
                   "HWC-WINS", (e_hwc_output_id_get(hwc)), ##x);      \
             else                                                     \
               INF("EWL|%20.20s|win:0x%08zx|ec:%8p|%8s|"f,            \
                   "HWC-WINS",                                        \
                   (e_client_util_win_get(ec)),                       \
                   (ec), (e_hwc_output_id_get(hwc)),                  \
                   ##x);                                              \
          }                                                           \
     }                                                                \
   while (0)

static Eina_Bool ehws_trace = EINA_FALSE;
static Eina_Bool ehws_dump_enable = EINA_FALSE;
static uint64_t ehws_rendered_windows_key;
#define EHWS_RENDERED_WINDOWS_KEY  (unsigned long)(&ehws_rendered_windows_key)

static uint64_t ehws_rendered_buffers_key;
#define EHWS_RENDERED_BUFFERS_KEY  (unsigned long)(&ehws_rendered_buffers_key)

static Eina_Bool _e_hwc_windows_pp_output_data_commit(E_Hwc *hwc, E_Hwc_Window_Commit_Data *data);
static Eina_Bool _e_hwc_windows_pp_window_commit(E_Hwc *hwc, E_Hwc_Window *hwc_window);

static E_Comp_Wl_Buffer *
_e_hwc_windows_comp_wl_buffer_get(E_Hwc_Window *hwc_window)
{
   E_Client *ec = hwc_window->ec;
   E_Comp_Wl_Client_Data *cdata = NULL;

   if (!ec) return NULL;

   cdata = ec->comp_data;
   if (!cdata) return NULL;

   return cdata->buffer_ref.buffer;
}

static void
_e_hwc_windows_update_fps(E_Hwc *hwc)
{
   if (e_comp->calc_fps)
     {
        double dt;
        double tim = ecore_time_get();

        dt = tim - hwc->frametimes[0];
        hwc->frametimes[0] = tim;

        hwc->time += dt;
        hwc->cframes++;

        if (hwc->lapse == 0.0)
          {
             hwc->lapse = tim;
             hwc->flapse = hwc->cframes;
          }
        else if ((tim - hwc->lapse) >= 0.5)
          {
             hwc->fps = (hwc->cframes - hwc->flapse) / (tim - hwc->lapse);
             hwc->lapse = tim;
             hwc->flapse = hwc->cframes;
             hwc->time = 0.0;
          }
     }
}

static E_Hwc_Mode
_e_hwc_windows_hwc_mode_update(E_Hwc *hwc, int num_client, int num_device, int num_video)
{
   E_Hwc_Mode hwc_mode = E_HWC_MODE_NONE;
   int num_visible = hwc->num_visible_windows;

   if (hwc->pp_set && hwc->pp_hwc_window)
     {
        hwc_mode = E_HWC_MODE_FULL;
        ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
        hwc->hwc_mode = hwc_mode;
        return hwc_mode;
     }

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
        EHWSERR("not supported format: %x", NULL, surf_info.format);
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
         if (e_hwc_window_is_video(hwc_window) && hwc_window->ec)
           e_client_video_commit_data_release(hwc_window->ec, sequence, tv_sec, tv_usec);

         if (!e_hwc_window_commit_data_release(hwc_window)) continue;
     }
}

static Eina_Bool
_e_hwc_windows_commit_data_acquire(E_Hwc *hwc)
{
   const Eina_List *l;
   E_Hwc_Window *hwc_window;
   Eina_Bool ret = EINA_FALSE;
   Eina_Bool temp = EINA_FALSE;

   /* return TRUE when the number of the commit data is more than one */
   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        if (hwc->pp_set)
          {
             if (hwc->pp_hwc_window)
               temp = e_hwc_window_pp_commit_data_acquire(hwc_window, EINA_TRUE);
             else
               temp = e_hwc_window_pp_commit_data_acquire(hwc_window, EINA_FALSE);
          }
        else
          temp = e_hwc_window_commit_data_acquire(hwc_window);
        if (!temp) continue;

        if (ehws_dump_enable)
          e_hwc_window_commit_data_buffer_dump(hwc_window);

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

   EHWSTRACE("!!!!!!!! HWC Commit Handler !!!!!!!!", NULL, hwc);

   if (hwc->pp_tsurface && !hwc->pp_set)
     {
        tbm_surface_internal_unref(hwc->pp_tsurface);
        hwc->pp_tsurface = NULL;
     }

   _e_hwc_windows_commit_data_release(hwc, sequence, tv_sec, tv_usec);

   /* 'wait_commit' is mechanism to make 'fetch and commit' no more than one time per a frame;
    * a 'page flip' happened so it's time to allow to make 'fetch and commit' for the e_output */
   hwc->wait_commit = EINA_FALSE;
}

static void
_e_hwc_windows_offscreen_commit(E_Hwc *hwc)
{
   E_Hwc_Window *hwc_window = NULL;
   Eina_List *l;
   Eina_Bool ret = EINA_FALSE;

   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        if (hwc->pp_set)
          {
             if (hwc->pp_hwc_window)
               ret = e_hwc_window_pp_commit_data_acquire(hwc_window, EINA_TRUE);
             else
               ret = e_hwc_window_pp_commit_data_acquire(hwc_window, EINA_FALSE);
          }
        else
          ret = e_hwc_window_commit_data_acquire(hwc_window);
        if (!ret) continue;

        EHWSTRACE("!!!!!!!! HWC OffScreen Commit !!!!!!!!", NULL, hwc);


        /* send frame event enlightenment doesn't send frame event in nocomp */
        if (hwc_window->ec)
          e_pixmap_image_clear(hwc_window->ec->pixmap, 1);

        e_hwc_window_commit_data_release(hwc_window);
     }
}

static Eina_List *
_e_hwc_windows_target_window_rendered_windows_get(E_Hwc *hwc)
{
   E_Hwc_Window_Target *target_hwc_window;
   Eina_List *rendered_windows = NULL, *new_list = NULL;
   E_Hwc_Window *hw1, *hw2;
   const Eina_List *l, *ll;
   tbm_surface_h target_tsurface;

   target_hwc_window = hwc->target_hwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, NULL);

   target_tsurface = target_hwc_window->hwc_window.buffer.tsurface;
   if (!target_tsurface) return NULL;

   tbm_surface_internal_get_user_data(target_tsurface, EHWS_RENDERED_WINDOWS_KEY,
                            (void**)&rendered_windows);

   /* refresh list of composited e_thwc_windows according to existed ones */
   EINA_LIST_FOREACH(rendered_windows, l, hw1)
      EINA_LIST_FOREACH(hwc->hwc_windows, ll, hw2)
         if (hw1 == hw2) new_list = eina_list_append(new_list, hw1);

   return new_list;
}

static Eina_Bool
_e_hwc_windows_target_window_buffer_skip(E_Hwc *hwc, Eina_Bool tdm_set)
{
   E_Hwc_Window_Target *target_hwc_window;
   E_Hwc_Window *hwc_window = NULL;
   E_Hwc_Window_Queue_Buffer *queue_buffer;
   tdm_hwc *thwc = NULL;
   tdm_region fb_damage;

   target_hwc_window = hwc->target_hwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   hwc_window = (E_Hwc_Window *)target_hwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window->queue, EINA_FALSE);

   thwc = hwc->thwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc, EINA_FALSE);

   CLEAR(fb_damage);

   if (hwc_window->buffer.tsurface &&
       hwc_window->buffer.tsurface != hwc_window->display.buffer.tsurface)
     {
        if (hwc_window->buffer.queue)
          {
             queue_buffer = e_hwc_window_queue_buffer_find(hwc_window->buffer.queue, hwc_window->buffer.tsurface);
             if (queue_buffer)
               e_hwc_window_queue_buffer_release(hwc_window->buffer.queue, queue_buffer);
          }

        e_hwc_window_buffer_set(hwc_window, hwc_window->display.buffer.tsurface, hwc_window->display.buffer.queue);
        if (tdm_set)
          tdm_hwc_set_client_target_buffer(thwc, hwc_window->display.buffer.tsurface, fb_damage);
     }

   return EINA_TRUE;
}

/* set the tsurface to the target_window->tsurface according to the state.
 *  1. try to set the tsurface to the target_window at E_HWC_WINDOW_STATE_DEVICE.
 *  2. try to set NULL and release(clear) tsurface_queue of the target_window at E_HWC_WINDOW_STATE_NONE.
 *  Returing EINA_FALSE means that there is no update for the target_window->tsurface.
 **/
static Eina_Bool
_e_hwc_windows_target_buffer_fetch(E_Hwc *hwc, Eina_Bool tdm_set)
{
   E_Hwc_Window_Target *target_hwc_window;
   E_Hwc_Window *hwc_window, *hw;
   tdm_hwc *thwc;
   tbm_surface_h tsurface;
   tdm_region fb_damage;
   Eina_List *rendered_windows = NULL;
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;
   uint32_t n_thw = 0;
   const Eina_List *l;
   int i;

   target_hwc_window = hwc->target_hwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   hwc_window = (E_Hwc_Window *)target_hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window->queue , EINA_FALSE);

   thwc = hwc->thwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc, EINA_FALSE);

   if ((hwc_window->state == E_HWC_WINDOW_STATE_DEVICE) ||
       (hwc->pp_set && hwc->pp_hwc_window))
     {
        /* acquire the surface */
        queue_buffer = e_hwc_window_queue_buffer_acquire(hwc_window->queue);
        if (!queue_buffer) return EINA_FALSE;

        tsurface = queue_buffer->tsurface;
        EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, EINA_FALSE);

        if (hwc_window->buffer.tsurface &&
            hwc_window->buffer.tsurface != hwc_window->display.buffer.tsurface)
          {
             queue_buffer = e_hwc_window_queue_buffer_find(hwc_window->buffer.queue, hwc_window->buffer.tsurface);
             if (queue_buffer)
               e_hwc_window_queue_buffer_release(hwc_window->buffer.queue, queue_buffer);
          }

        e_hwc_window_buffer_set(hwc_window, tsurface, hwc_window->queue);

        /* the damage isn't supported by hwc extension yet */
        CLEAR(fb_damage);

        /* If pp_set is true, tdm_hwc_set_client_target_buffer is done with the result buffer of the pp */
        if (tdm_set)
          tdm_hwc_set_client_target_buffer(thwc, hwc_window->buffer.tsurface, fb_damage);

        if (ehws_trace)
          {
             rendered_windows = _e_hwc_windows_target_window_rendered_windows_get(hwc);
             n_thw = eina_list_count(rendered_windows);
             if (n_thw)
               {
                  EHWSTRACE("FET {%s} ts:%p state:%s has hwc_windows to render below.",
                           NULL, hwc, "@TARGET WINDOW@", hwc_window->buffer.tsurface,
                           e_hwc_window_state_string_get(hwc_window->state));

                  i = 0;
                  EINA_LIST_FOREACH(rendered_windows, l, hw)
                    {
                       EHWSTRACE("  (%d) ehw:%p ts:%p -- {%25s}, state:%s, zpos:%d, deleted:%s",
                                hwc_window->ec, hwc, i++, hw, hw->buffer.tsurface, e_hwc_window_name_get(hw),
                                e_hwc_window_state_string_get(hw->state), hwc_window->zpos,
                                (hwc_window->is_deleted ? "yes" : "no"));
                    }
                }
              else
                EHWSTRACE("FET {%s} ts:%p state:%s has no hwc_windows to render.",
                         NULL, hwc, "@TARGET WINDOW@", hwc_window->buffer.tsurface,
                         e_hwc_window_state_string_get(hwc_window->state));
          }
     }
   else
     {
        e_hwc_window_queue_clear(hwc_window->queue);

        if (!hwc_window->buffer.tsurface) return EINA_FALSE;

        if (hwc_window->buffer.tsurface != hwc_window->display.buffer.tsurface)
          {
             queue_buffer = e_hwc_window_queue_buffer_find(hwc_window->buffer.queue, hwc_window->buffer.tsurface);
             if (queue_buffer)
               e_hwc_window_queue_buffer_release(hwc_window->buffer.queue, queue_buffer);
          }

        e_hwc_window_buffer_set(hwc_window, NULL, NULL);

        CLEAR(fb_damage);

        tdm_hwc_set_client_target_buffer(thwc, NULL, fb_damage);
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_hwc_windows_target_window_rendered_window_has(E_Hwc *hwc, E_Hwc_Window *hwc_window)
{
   Eina_List *rendered_windows = NULL;
   E_Hwc_Window_Target *target_hwc_window;
   E_Hwc_Window *target_window;
   E_Hwc_Window *hw;
   const Eina_List *l = NULL;
   int n_thw;

   target_hwc_window = hwc->target_hwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   target_window = (E_Hwc_Window *)target_hwc_window;
   if (e_hwc_window_state_get(target_window) != E_HWC_WINDOW_STATE_DEVICE) return EINA_FALSE;

   rendered_windows = _e_hwc_windows_target_window_rendered_windows_get(hwc);
   n_thw = eina_list_count(rendered_windows);
   if (n_thw)
     {
        EINA_LIST_FOREACH(rendered_windows, l, hw)
          if (hw == hwc_window) return EINA_TRUE;
     }

   return EINA_FALSE;
}

static void
_e_hwc_windows_rendered_buffers_free(void *data)
{
   Eina_List *rendered_buffers = (Eina_List *)data;
   E_Comp_Wl_Buffer_Ref *buffer_ref;

   if (!rendered_buffers) return;

   if (eina_list_count(rendered_buffers))
     {
        EINA_LIST_FREE(rendered_buffers, buffer_ref)
          {
             e_comp_wl_buffer_reference(buffer_ref, NULL);
             E_FREE(buffer_ref);
          }
     }
}

static void
_e_hwc_windows_rendered_windows_free(void *data)
{
   Eina_List *rendered_windows = (Eina_List *)data;
   E_Hwc_Window *hwc_window = NULL;

  if (eina_list_count(rendered_windows))
    {
        EINA_LIST_FREE(rendered_windows, hwc_window)
          e_object_unref(E_OBJECT(hwc_window));
    }
}

/* gets called as somebody modifies target_window's queue */
static void
_e_hwc_windows_target_window_surface_queue_trace_cb(tbm_surface_queue_h surface_queue,
        tbm_surface_h tsurface, tbm_surface_queue_trace trace, void *data)
{
   E_Hwc_Window_Target *target_hwc_window = (E_Hwc_Window_Target *)data;

   /* gets called as evas_renderer dequeues a new buffer from the queue */
   if (trace == TBM_SURFACE_QUEUE_TRACE_DEQUEUE)
     {
        if (!target_hwc_window->is_rendering) return;

        EHWSTRACE("{%s} dequeue ts:%p", NULL, target_hwc_window->hwc, "@TARGET WINDOW@", tsurface);

        tbm_surface_internal_add_user_data(tsurface,
                                           EHWS_RENDERED_BUFFERS_KEY,
                                           _e_hwc_windows_rendered_buffers_free);

        tbm_surface_internal_add_user_data(tsurface,
                                           EHWS_RENDERED_WINDOWS_KEY,
                                           _e_hwc_windows_rendered_windows_free);
        target_hwc_window->dequeued_tsurface = tsurface;
        target_hwc_window->target_buffer_list = eina_list_append(target_hwc_window->target_buffer_list,
                                                                     tsurface);
     }

   if (trace == TBM_SURFACE_QUEUE_TRACE_ACQUIRE)
     tbm_surface_internal_set_user_data(tsurface, EHWS_RENDERED_BUFFERS_KEY, NULL);

   /* tsurface has been released at the queue */
   if (trace == TBM_SURFACE_QUEUE_TRACE_RELEASE)
     {
        EHWSTRACE("{%s} release ts:%p", NULL, NULL, "@TARGET WINDOW@", tsurface);

        tbm_surface_internal_delete_user_data(tsurface, EHWS_RENDERED_BUFFERS_KEY);

        tbm_surface_internal_delete_user_data(tsurface, EHWS_RENDERED_WINDOWS_KEY);
        target_hwc_window->target_buffer_list = eina_list_remove(target_hwc_window->target_buffer_list,
                                                                     tsurface);
     }
}

/* gets called as evas_renderer enqueues a new buffer into the queue */
static void
_e_hwc_windows_target_window_surface_queue_acquirable_cb(tbm_surface_queue_h surface_queue, void *data)
{
    E_Hwc_Window_Target *target_hwc_window = (E_Hwc_Window_Target *)data;
    uint64_t value = 1;
    int ret;

    ret = write(target_hwc_window->event_fd, &value, sizeof(value));
    if (ret == -1)
      EHWSERR("failed to send acquirable event:%m", NULL);
}

/* gets called at the beginning of an ecore_main_loop iteration */
static Eina_Bool
_e_hwc_windows_target_window_render_finished_cb(void *data, Ecore_Fd_Handler *fd_handler)
{
   int len;
   int fd;
   char buffer[64];
   E_Hwc_Window_Target *target_hwc_window = (E_Hwc_Window_Target *)data;
   E_Hwc_Window *hwc_window = NULL;
   Eina_List *acquirable_buffers = NULL;
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;

   fd = ecore_main_fd_handler_fd_get(fd_handler);
   if (fd < 0) return ECORE_CALLBACK_RENEW;

   len = read(fd, buffer, sizeof(buffer));
   if (len == -1)
     EHWSERR("failed to read queue acquire event fd:%m", NULL);

   hwc_window = (E_Hwc_Window *)target_hwc_window;
   if (!hwc_window) return ECORE_CALLBACK_RENEW;
   if (!hwc_window->queue) return ECORE_CALLBACK_RENEW;

   acquirable_buffers = e_hwc_window_queue_acquirable_buffers_get(hwc_window->queue);
   if (!acquirable_buffers) return ECORE_CALLBACK_RENEW;

   EINA_LIST_FREE(acquirable_buffers, queue_buffer)
     {
        if (!queue_buffer->tsurface) continue;

        tbm_surface_internal_set_user_data(queue_buffer->tsurface,
                                           EHWS_RENDERED_BUFFERS_KEY,
                                           NULL);
     }

   return ECORE_CALLBACK_RENEW;
}

static void
_e_hwc_windows_target_window_render_flush_post_cb(void *data, Evas *e EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Hwc_Window_Target *target_hwc_window = (E_Hwc_Window_Target *)data;
   E_Hwc_Window *hwc_window = NULL;
   E_Comp_Wl_Buffer_Ref *buffer_ref = NULL;
   E_Comp_Wl_Buffer *buffer = NULL;
   Eina_List *rendered_buffers = NULL;
   Eina_List *rendered_windows = NULL;
   Eina_List *l;

   EHWSTRACE("{%s} gets render_flush_post noti.", NULL, target_hwc_window->hwc, "@TARGET WINDOW@");

   if (!target_hwc_window->dequeued_tsurface)
     {
        EHWSINF("flush_post_cb is called but tsurface isn't dequeued", NULL, target_hwc_window->hwc);

        if (eina_list_count(target_hwc_window->rendered_windows))
          {
             EINA_LIST_FREE(target_hwc_window->rendered_windows, hwc_window)
               e_object_unref(E_OBJECT(hwc_window));
          }

        target_hwc_window->rendered_windows = NULL;
        return;
     }

   /* all ecs have been composited so we can attach a list of composited e_hwc_windows to the surface
    * which contains their ecs composited */
   rendered_windows = eina_list_clone(target_hwc_window->rendered_windows);

   EINA_LIST_FOREACH(rendered_windows, l, hwc_window)
     {
        E_Client *ec = NULL;

        ec = hwc_window->ec;
        if (!ec) continue;

        buffer = e_pixmap_ref_resource_get(ec->pixmap);
        if (!buffer)
          buffer = _e_hwc_windows_comp_wl_buffer_get(hwc_window);

        if (!buffer) continue;

        /* if reference buffer created by server, server deadlock is occurred.
           beacause tbm_surface_internal_unref is called in user_data delete callback.
           tbm_surface doesn't allow it.
         */
        if (!buffer->resource) continue;

        buffer_ref = E_NEW(E_Comp_Wl_Buffer_Ref, 1);
        if (!buffer_ref) continue;

        e_comp_wl_buffer_reference(buffer_ref, buffer);
        rendered_buffers = eina_list_append(rendered_buffers, buffer_ref);
     }

   tbm_surface_internal_set_user_data(target_hwc_window->dequeued_tsurface,
                                      EHWS_RENDERED_BUFFERS_KEY,
                                      rendered_buffers);

   tbm_surface_internal_set_user_data(target_hwc_window->dequeued_tsurface,
                                      EHWS_RENDERED_WINDOWS_KEY,
                                      rendered_windows);

   eina_list_free(target_hwc_window->rendered_windows);
   target_hwc_window->rendered_windows = NULL;
   target_hwc_window->dequeued_tsurface = NULL;
}

static void
_e_hwc_windows_target_window_free(E_Hwc_Window_Target *target_hwc_window)
{
   E_Hwc_Window *target_window = (E_Hwc_Window *)target_hwc_window;

   evas_event_callback_del(target_hwc_window->evas,
                           EVAS_CALLBACK_RENDER_FLUSH_POST,
                           _e_hwc_windows_target_window_render_flush_post_cb);

   ecore_main_fd_handler_del(target_hwc_window->event_hdlr);
   close(target_hwc_window->event_fd);

   if (target_window->queue->tqueue)
     tbm_surface_queue_destroy(target_window->queue->tqueue);

   EHWSINF("Free target window", NULL, NULL);

   E_FREE(target_hwc_window);
}

static void
_e_hwc_windows_target_cb_queue_destroy(struct wl_listener *listener, void *data)
{
   E_Hwc_Window *hwc_window = NULL;

   hwc_window = container_of(listener, E_Hwc_Window, queue_destroy_listener);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   if ((E_Hwc_Window_Queue *)data != hwc_window->queue) return;

   hwc_window->queue = NULL;
}

static E_Hwc_Window_Target *
_e_hwc_windows_target_window_new(E_Hwc *hwc)
{
   const char *name = NULL;
   E_Hwc_Window_Target *target_hwc_window = NULL;
   Evas *evas = NULL;
   E_Hwc_Window_Queue *queue = NULL;

   name = ecore_evas_engine_name_get(hwc->ee);
   EINA_SAFETY_ON_NULL_RETURN_VAL(name, NULL);

   evas = ecore_evas_get(hwc->ee);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evas, NULL);

   if(!strcmp("gl_drm_tbm", name) ||
      !strcmp("drm_tbm", name) ||
      !strcmp("gl_tbm", name) ||
      !strcmp("software_tbm", name) ||
      !strcmp("gl_tbm_ES", name))
     {
        ecore_evas_manual_render_set(hwc->ee, 1);
     }

   target_hwc_window = E_OBJECT_ALLOC(E_Hwc_Window_Target, E_HWC_WINDOW_TYPE, _e_hwc_windows_target_window_free);
   EINA_SAFETY_ON_NULL_GOTO(target_hwc_window, fail);

   ((E_Hwc_Window *)target_hwc_window)->is_target = EINA_TRUE;
   ((E_Hwc_Window *)target_hwc_window)->state = E_HWC_WINDOW_STATE_DEVICE;
   ((E_Hwc_Window *)target_hwc_window)->accepted_state = E_HWC_WINDOW_STATE_DEVICE;
   ((E_Hwc_Window *)target_hwc_window)->hwc = hwc;

   target_hwc_window->ee = hwc->ee;
   target_hwc_window->evas = ecore_evas_get(target_hwc_window->ee);
   target_hwc_window->event_fd = eventfd(0, EFD_NONBLOCK);
   target_hwc_window->event_hdlr =
            ecore_main_fd_handler_add(target_hwc_window->event_fd, ECORE_FD_READ,
                                      _e_hwc_windows_target_window_render_finished_cb,
                                      (void *)target_hwc_window, NULL, NULL);

   ecore_evas_manual_render(target_hwc_window->ee);

   queue = e_hwc_window_queue_user_set((E_Hwc_Window *)target_hwc_window);
   if (!queue) goto fail;

   wl_signal_add(&queue->destroy_signal, &((E_Hwc_Window *)target_hwc_window)->queue_destroy_listener);
   ((E_Hwc_Window *)target_hwc_window)->queue_destroy_listener.notify = _e_hwc_windows_target_cb_queue_destroy;
   ((E_Hwc_Window *)target_hwc_window)->queue = queue;

   /* as evas_renderer has finished its work (to provide a composited buffer) it enqueues
    * the result buffer into this queue and acquirable cb gets called; this cb does nothing
    * except the writing into the event_fd object, this writing causes the new ecore_main loop
    * iteration to be triggered ('cause we've registered ecore_main fd handler to check this writing);
    * so it's just a way to inform E20's HWC that evas_renderer has done its work */
   tbm_surface_queue_add_acquirable_cb(queue->tqueue,
                                       _e_hwc_windows_target_window_surface_queue_acquirable_cb,
                                       (void *)target_hwc_window);

   /* TODO: we can use this call instead of an add_acquirable_cb and an add_dequeue_cb calls. */
   tbm_surface_queue_add_trace_cb(queue->tqueue,
                                  _e_hwc_windows_target_window_surface_queue_trace_cb,
                                  (void *)target_hwc_window);

   evas_event_callback_add(evas,
                           EVAS_CALLBACK_RENDER_FLUSH_POST,
                           _e_hwc_windows_target_window_render_flush_post_cb,
                           target_hwc_window);

   return target_hwc_window;

fail:
   ecore_evas_manual_render_set(hwc->ee, 0);
   if (target_hwc_window)
      e_object_del(E_OBJECT(hwc->target_hwc_window));

   return NULL;
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

   EINA_SAFETY_ON_NULL_RETURN(user_data);

   hwc = user_data;

   hwc->pp_output_commit = EINA_FALSE;

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
             /* release and unref the current pp surface on the hwc */
             tbm_surface_queue_release(hwc->pp_tqueue, hwc->pp_tsurface);
             tbm_surface_internal_unref(hwc->pp_tsurface);
          }

        /* set the new pp surface to the hwc */
        hwc->pp_tsurface = data->buffer.tsurface;

        E_FREE(data);
     }

   EHWSTRACE("!!!!!!!! HWC PP Output Commit Handler !!!!!!!!", NULL, hwc);
   EHWSTRACE("   pp_tsurface(%p)", NULL, hwc, hwc->pp_tsurface);

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

             EHWSTRACE("PP Output Commit Handler start pending commit data(%p) tsurface(%p)", NULL, hwc, data, data->buffer.tsurface);

             if (!_e_hwc_windows_pp_output_data_commit(hwc, data))
               {
                  EHWSERR("fail to _e_hwc_windows_pp_output_data_commit", hwc);
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

             EHWSTRACE("PP Output Commit Handler start pending pp data(%p) tsurface(%p)", NULL, hwc, hwc_window, hwc_window->buffer.tsurface);

             if (!_e_hwc_windows_pp_window_commit(hwc, hwc_window))
               {
                  EHWSERR("fail _e_hwc_windows_pp_data_commit", hwc);
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

   EHWSTRACE("!!!!!!!! HWC PP Output Commit !!!!!!!!", NULL, hwc);
   EHWSTRACE("   tsurface(%p)", NULL, hwc, data->buffer.tsurface);

   /* no need to pass composited_wnds list because smooth transition isn't
    * used in this case */
   terror = tdm_hwc_set_client_target_buffer(hwc->thwc, data->buffer.tsurface, fb_damage);
   if (terror != TDM_ERROR_NONE)
     {
        EHWSERR("fail to tdm_hwc_set_client_target_buffer", hwc);
        goto fail;
     }

   terror = tdm_hwc_accept_validation(hwc->thwc);
   if (terror != TDM_ERROR_NONE)
     {
        EHWSERR("fail to tdm_hwc_accept_validation", hwc);
        goto fail;
     }

   terror = tdm_hwc_commit(hwc->thwc, 0, _e_hwc_windows_pp_output_commit_handler, hwc);
   if (terror != TDM_ERROR_NONE)
     {
        EHWSERR("fail to tdm_output_commit", hwc);
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

   tbm_err = tbm_surface_queue_enqueue(hwc->pp_tqueue, tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        EHWSERR("fail tbm_surface_queue_enqueue", hwc);
        goto fail;
     }

   tbm_err = tbm_surface_queue_acquire(hwc->pp_tqueue, &pp_tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        EHWSERR("fail tbm_surface_queue_acquire", hwc);
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
        EHWSERR("fail to _e_hwc_windows_pp_output_data_commit", hwc);
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

   EHWSTRACE("!!!!!!!! HWC PP Commit Handler !!!!!!!!", NULL, hwc);
   EHWSTRACE("   tsurface src(%p) dst(%p)", NULL, hwc, tsurface_src, tsurface_dst);

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
     EHWSERR("fail to _e_hwc_windows_pp_output_commit", hwc);

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

   EHWSTRACE("PP Info  src_rect(%d,%d),(%d,%d), dst_rect(%d,%d),(%d,%d)",
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
   EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, EINA_FALSE);

   tbm_surface_h tsurface = commit_data->buffer.tsurface;

   EHWSTRACE("!!!!!!!! HWC PP Commit !!!!!!!!", NULL, hwc);
   EHWSTRACE("   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
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
        EHWSERR("fail tbm_surface_queue_dequeue", hwc);
        return EINA_FALSE;
     }

   if (!_e_hwc_pp_windows_info_set(hwc, hwc_window, pp_tsurface))
     {
        EHWSERR("fail _e_hwc_windows_info_set", hwc);
        goto pp_fail;
     }

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

   EHWSERR("failed _e_hwc_windows_pp_data_commit", hwc);

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

   if (hwc->pp_hwc_window)
     {
        if (tbm_surface_get_width(hwc->pp_hwc_window->buffer.tsurface) != w ||
            tbm_surface_get_height(hwc->pp_hwc_window->buffer.tsurface) != h)
           return NULL;

        hwc_window_for_zoom = hwc->pp_hwc_window;
     }
   else
     {
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
     }

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
   if (!commit_data || !commit_data->buffer.tsurface)
     {
        EHWSERR("no commit_data", hwc);
        return EINA_TRUE;
     }

   if (!tbm_surface_queue_can_dequeue(hwc->pp_tqueue, 0))
     {
        EHWSTRACE("PP Commit  Can Dequeue failed tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
                  NULL, hwc, commit_data->buffer.tsurface, hwc->pp_tqueue,
                  commit_data->buffer_ref.buffer ? commit_data->buffer_ref.buffer->resource : NULL, commit_data);
        hwc->pending_pp_hwc_window_list = eina_list_append(hwc->pending_pp_hwc_window_list, hwc_window);

        hwc->wait_commit = EINA_TRUE;

        return EINA_TRUE;
     }

   if (eina_list_count(hwc->pending_pp_hwc_window_list) != 0)
     {
        EHWSTRACE("PP Commit  Pending pp data remained tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
                  NULL, hwc, commit_data->buffer.tsurface, hwc->pp_tqueue,
                  commit_data->buffer_ref.buffer ? commit_data->buffer_ref.buffer->resource : NULL, commit_data);
        hwc->pending_pp_hwc_window_list = eina_list_append(hwc->pending_pp_hwc_window_list, hwc_window);

        hwc->wait_commit = EINA_TRUE;

        return EINA_TRUE;
     }

   if (!_e_hwc_windows_pp_window_commit(hwc, hwc_window))
     {
        EHWSERR("fail _e_hwc_windows_pp_data_commit", hwc);
        e_hwc_window_commit_data_release(hwc_window);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_hwc_windows_status_print(E_Hwc *hwc, Eina_Bool with_target)
{
   Eina_List *visible_windows = hwc->visible_windows;
   const Eina_List *l;
   E_Hwc_Window *hwc_window;

   EINA_LIST_FOREACH(visible_windows, l, hwc_window)
     {
        EHWSTRACE("  ehw:%p ts:%p -- {%25s}, state:%s, zpos:%d, deleted:%s",
                  hwc_window->ec, hwc, hwc_window,
                  hwc_window->buffer.tsurface, e_hwc_window_name_get(hwc_window),
                  e_hwc_window_state_string_get(hwc_window->state),
                  hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
     }
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
      case TDM_HWC_WIN_COMPOSITION_NONE:
        state = E_HWC_WINDOW_STATE_NONE;
        break;
      case TDM_HWC_WIN_COMPOSITION_CLIENT:
        state = E_HWC_WINDOW_STATE_CLIENT;
        break;
      case TDM_HWC_WIN_COMPOSITION_DEVICE:
        state = E_HWC_WINDOW_STATE_DEVICE;
        break;
      case TDM_HWC_WIN_COMPOSITION_CURSOR:
        state = E_HWC_WINDOW_STATE_CURSOR;
        break;
      case TDM_HWC_WIN_COMPOSITION_VIDEO:
        state = E_HWC_WINDOW_STATE_VIDEO;
        break;
      default:
        state = E_HWC_WINDOW_STATE_NONE;
        EHWSERR("unknown state of hwc_window.", NULL);
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
             if ((hwc_window->ec) && (!e_pixmap_resource_get(hwc_window->ec->pixmap)))
               continue;

             if (!_e_hwc_windows_target_window_rendered_window_has(hwc, hwc_window))
               {
                  e_hwc_window_transition_set(hwc_window, E_HWC_WINDOW_TRANSITION_DEVICE_TO_CLIENT);
                  transition = EINA_TRUE;
               }
          }
        /* CURSOR -> CLIENT */
        else if (hwc_window->state == E_HWC_WINDOW_STATE_CLIENT &&
                 hwc_window->accepted_state == E_HWC_WINDOW_STATE_CURSOR)
          {
             if ((hwc_window->ec) && (!e_pixmap_resource_get(hwc_window->ec->pixmap)))
               continue;

             if (!_e_hwc_windows_target_window_rendered_window_has(hwc, hwc_window))
               {
                  e_hwc_window_transition_set(hwc_window, E_HWC_WINDOW_TRANSITION_CURSOR_TO_CLIENT);
                  transition = EINA_TRUE;
               }
          }
        /* NONE -> DEVICE */
        else if (hwc_window->state == E_HWC_WINDOW_STATE_DEVICE &&
                 hwc_window->accepted_state == E_HWC_WINDOW_STATE_NONE)
          {
             if (_e_hwc_windows_target_window_rendered_window_has(hwc, hwc_window))
               {
                  e_hwc_window_transition_set(hwc_window, E_HWC_WINDOW_TRANSITION_NONE_TO_DEVICE);
                  transition = EINA_TRUE;
               }
          }
        /* NONE -> CURSOR */
        else if (hwc_window->state == E_HWC_WINDOW_STATE_CURSOR &&
                 hwc_window->accepted_state == E_HWC_WINDOW_STATE_NONE)
          {
             if (_e_hwc_windows_target_window_rendered_window_has(hwc, hwc_window))
               {
                  e_hwc_window_transition_set(hwc_window, E_HWC_WINDOW_TRANSITION_NONE_TO_CURSOR);
                  transition = EINA_TRUE;
               }
          }
        /* CLIENT -> DEVICE */
        else if (hwc_window->state == E_HWC_WINDOW_STATE_DEVICE &&
                 hwc_window->accepted_state == E_HWC_WINDOW_STATE_CLIENT)
          {
             if (_e_hwc_windows_target_window_rendered_window_has(hwc, hwc_window))
               {
                  e_hwc_window_transition_set(hwc_window, E_HWC_WINDOW_TRANSITION_CLIENT_TO_DEVICE);
                  transition = EINA_TRUE;
               }
          }
        /* CLIENT -> CURSOR */
        else if (hwc_window->state == E_HWC_WINDOW_STATE_CURSOR &&
                 hwc_window->accepted_state == E_HWC_WINDOW_STATE_CLIENT)
          {
             if (_e_hwc_windows_target_window_rendered_window_has(hwc, hwc_window))
               {
                  e_hwc_window_transition_set(hwc_window, E_HWC_WINDOW_TRANSITION_CLIENT_TO_CURSOR);
                  transition = EINA_TRUE;
               }
          }
     }
#if 0
    if (transition)
      EHWSTRACE(" [%25s(ehw:%p)] is on TRANSITION [%s -> %s].",
                hwc_window->ec, hwc, e_hwc_window_name_get(hwc_window), hwc_window,
                e_hwc_window_state_string_get(hwc_window->accepted_state),
                e_hwc_window_state_string_get(hwc_window->state));
#endif
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
        EHWSERR("failed to get changed composition types", hwc);
        goto fail;
     }

   EHWSTRACE(" Changes NUM : %d", NULL, hwc, num_changes);

   for (i = 0; i < num_changes; ++i)
     {
        hwc_window = _e_hwc_windows_window_find_by_twin(hwc, changed_hwc_window[i]);
        if (!hwc_window)
          {
             EHWSERR("cannot find the E_Hwc_Window by hwc hwc_window", hwc);
             goto fail;
          }

        /* update the state with the changed compsition */
        state = _e_hwc_windows_window_state_get(composition_types[i]);
        e_hwc_window_state_set(hwc_window, state, EINA_TRUE);
     }

#if DBG_EVALUATE
   EHWSTRACE(" Modified after HWC Validation:", NULL, hwc);
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
_e_hwc_windows_accept(E_Hwc *hwc, Eina_Bool tdm_set)
{
   E_Hwc_Window *hwc_window;
   E_Hwc_Window_State state;
   tdm_error terror = TDM_ERROR_NONE;
   const Eina_List *l;

   /* accept changes */
   if (tdm_set)
     {
        terror = tdm_hwc_accept_validation(hwc->thwc);
        if (terror != TDM_ERROR_NONE)
          {
             EHWSERR("failed to accept validation.", hwc);
             return EINA_FALSE;
          }
     }

   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        if (e_hwc_window_is_target(hwc_window)) continue;

        /* update the accepted_state */
        state = e_hwc_window_state_get(hwc_window);
        e_hwc_window_accepted_state_set(hwc_window, state);
        e_hwc_window_transition_set(hwc_window, E_HWC_WINDOW_TRANSITION_NONE_TO_NONE);

        /* notify the hwc_window that it will be displayed on hw layer */
        if (!hwc_window->queue &&
            !e_hwc_window_is_video(hwc_window) &&
            e_hwc_window_is_on_hw_overlay(hwc_window))
          e_hwc_window_activate(hwc_window, NULL);
     }

   /* _e_hwc_windows_accept */
   EHWSTRACE("======= HWC Accept Validation =======", NULL, hwc);

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
   EHWSTRACE("======= HWC Request Validation to TDM HWC =====", NULL, hwc);
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
   EHWSERR("failed to validate the output(%p)", hwc, toutput);
   E_FREE(thwc_windows);

   return EINA_FALSE;
}

static E_Client *
_e_hwc_windows_client_get_from_object(Evas_Object *o)
{
   E_Client *ec = NULL;
   Evas_Object *ob = NULL, *cob = NULL;
   Eina_List *members = NULL;
   Eina_List *l = NULL;
   Eina_List *stack = NULL;

   if (!o) return NULL;

   stack = eina_list_append(stack, o);

   while (1)
     {
        ob = eina_list_last_data_get(stack);
        if (!ob) break;

        ec = evas_object_data_get(ob, "E_Client");
        if (ec) break;

        if (evas_object_smart_data_get(ob))
          {
             members = evas_object_smart_members_get(ob);

             EINA_LIST_FOREACH(members, l, cob)
               stack = eina_list_append(stack, cob);

             if (members)
               eina_list_free(members);
          }

        stack = eina_list_remove(stack, ob);
     }

   eina_list_free(stack);

   return ec;
}

static Eina_List *
_e_hwc_windows_visible_windows_list_get(E_Hwc *hwc)
{
   Eina_List *windows_list = NULL;
   E_Hwc_Window *hwc_window;
   E_Client  *ec;
   Evas_Object *o;
   int scr_w, scr_h;
   int x, y, w, h;
   int ui_skip = EINA_FALSE;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        if (!evas_object_visible_get(o)) continue;

        ec = _e_hwc_windows_client_get_from_object(o);

        if (!ec) continue;
        if (!ec->hwc_window) continue;

        hwc_window = ec->hwc_window;

        e_hwc_window_name_set(hwc_window);

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
        e_client_geometry_get(ec, &x, &y, &w, &h);
        ecore_evas_geometry_get(e_comp->ee, NULL, NULL, &scr_w, &scr_h);
        if (!E_INTERSECTS(0, 0, scr_w, scr_h, x, y, w, h))
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
            if (!e_client_video_tbm_surface_get(ec))
              continue;

            e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_VIDEO, EINA_TRUE);
          }
        else
          {
             if (ui_skip) continue;
          }

        if (ec->is_cursor)
          {
             e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_CURSOR, EINA_TRUE);
          }

        windows_list = eina_list_append(windows_list, hwc_window);

        if (!ec->argb && E_CONTAINS(x, y, w, h, 0, 0, scr_w, scr_h))
          ui_skip = EINA_TRUE;
     }

   return windows_list;
}

static Eina_Bool
_e_hwc_windows_device_states_available_check(E_Hwc *hwc)
{
   return hwc->device_state_available;
}

static void
_e_hwc_windows_visible_windows_states_update(E_Hwc *hwc)
{
   Eina_List *visible_windows = NULL;
   Eina_List *l;
   E_Hwc_Window *hwc_window = NULL;
   E_Client *ec = NULL;

   /* get the visible ecs */
   visible_windows = hwc->visible_windows;

   /* check if e20 forces to set that all window has TDM_HWC_WIN_COMPOSITION_CLIENT types */
   if (_e_hwc_windows_device_states_available_check(hwc))
     {
        /* check clients are able to use hwc */
        EINA_LIST_FOREACH(visible_windows, l, hwc_window)
          {
             /* The video window set the TDM_HWC_WIN_COMPOSITION_VIDEO type. */
             if (e_hwc_window_is_video(hwc_window))
               {
                  if (!e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_VIDEO, EINA_TRUE))
                    EHWSERR("cannot update E_Hwc_Window(%p)", hwc, hwc_window);

                  continue;
               }

              /* the cursor state is decided through finding the visible_windows. */
              if (e_hwc_window_is_cursor(hwc_window)) continue;

             /* filter the visible clients which e20 prevent to shown by hw directly
                by demand of e20 */
             if (e_hwc_window_device_state_available_get(hwc_window))
               e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_DEVICE, EINA_TRUE);
             else
               {
                  ec = hwc_window->ec;
                  if (!ec) continue;
                  if (ec->redirected)
                    e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_CLIENT, EINA_TRUE);
                  else
                    {
                       e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_DEVICE, EINA_TRUE);
                       EHWSTRACE("   ehw:%p -- {%25s} is Force hwc_acceptable(Redirected FALSE).",
                               hwc_window->ec, hwc, hwc_window, e_hwc_window_name_get(hwc_window));
                    }
               }
          }
     }
   else
     {
        EINA_LIST_FOREACH(visible_windows, l, hwc_window)
          {
             /* The video window set the TDM_HWC_WIN_COMPOSITION_VIDEO type. */
             if (e_hwc_window_is_video(hwc_window))
               {
                  if (!e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_VIDEO, EINA_TRUE))
                    EHWSERR("cannot update E_Hwc_Window(%p)", hwc, hwc_window);
                  continue;
               }

             ec = hwc_window->ec;
             if (!ec) continue;

             if (hwc->pp_set)
               {
                  e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_CLIENT, EINA_TRUE);
                  EHWSTRACE("   ehw:%p -- {%25s} is Force hwc_unacceptable.",
                          hwc_window->ec, hwc, hwc_window, e_hwc_window_name_get(hwc_window));
                   continue;
               }

             if (ec->redirected)
               {
                  e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_CLIENT, EINA_TRUE);
                  EHWSTRACE("   ehw:%p -- {%25s} is NOT hwc_acceptable.",
                          hwc_window->ec, hwc, hwc_window, e_hwc_window_name_get(hwc_window));
               }
             else
               {
                  e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_DEVICE, EINA_TRUE);
                  EHWSTRACE("   ehw:%p -- {%25s} is Force hwc_acceptable(Redirected FALSE).",
                          hwc_window->ec, hwc, hwc_window, e_hwc_window_name_get(hwc_window));
               }
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

  if (eina_list_count(hwc->visible_windows))
    {
       EINA_LIST_FREE(hwc->visible_windows, hwc_window)
          e_object_unref(E_OBJECT(hwc_window));
    }

   /* store the current visible windows and the number of them */
   hwc->visible_windows = visible_windows;
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

static Eina_Bool
_e_hwc_windows_device_state_available_update(E_Hwc *hwc)
{
   Eina_Bool available = EINA_TRUE;
   E_Hwc_Window *hwc_window = NULL;
   Eina_List *visible_windows = NULL;

   /* make the full_gl_composite when the zoom is enabled */
   if (hwc->pp_set)
     {
        available = EINA_FALSE;

        if (hwc->num_visible_windows > 1 || hwc->pp_set_info)
          {
             hwc->pp_hwc_window = NULL;
          }
        else
          {
             /* if there is only 1 visible window, set to pp_hwc_window*/
             visible_windows = hwc->visible_windows;

             if (eina_list_count(visible_windows) == 1)
               hwc_window = eina_list_nth(visible_windows, 0);

             if (hwc_window == NULL)
               hwc->pp_hwc_window = NULL;
             else
               hwc->pp_hwc_window = hwc_window;
          }

        goto finish;
     }

   /* make the full_gl_composite when the zoom is disabled */
   if (hwc->pp_unset)
     {
        available = EINA_FALSE;
        hwc->pp_hwc_window = NULL;

        goto finish;
     }

   /* full composite is forced to be set */
   if (e_hwc_deactive_get(hwc))
     {
        available = EINA_FALSE;
        goto finish;
     }

   /* hwc_window manager required full GLES composition */
   if (e_comp->nocomp_override > 0)
     {
        available = EINA_FALSE;
        goto finish;
     }

finish:
   if (hwc->device_state_available == available) return EINA_FALSE;

   hwc->device_state_available = available;

   return EINA_TRUE;
}

/* check if there is a need to update the output */
static Eina_Bool
_e_hwc_windows_changes_update(E_Hwc *hwc)
{
   E_Hwc_Window *hwc_window = NULL;
   Eina_Bool update_changes = EINA_FALSE;
   const Eina_List *l;
   Eina_Bool ret = EINA_FALSE;

   /* update the the visible windows */
   if (_e_hwc_windows_visible_windows_update(hwc))
     update_changes = EINA_TRUE;

  /* update the the visible windows */
   if (_e_hwc_windows_device_state_available_update(hwc))
     update_changes = EINA_TRUE;

   /* if pp set enabled, buffer set to tdm after pp done */
   if (hwc->pp_set)
     {
        /* fetch the target buffer (try acquire) */
        if (_e_hwc_windows_target_buffer_fetch(hwc, EINA_FALSE))
          update_changes = EINA_TRUE;

        /* if pp set, need update once */
        if (hwc->pp_set_info)
          update_changes = EINA_TRUE;
     }
   else
     {
        /* fetch the target buffer (try acquire) */
        if (_e_hwc_windows_target_buffer_fetch(hwc, EINA_TRUE))
          update_changes = EINA_TRUE;

        if (hwc->pp_unset)
          update_changes = EINA_TRUE;
     }

   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        if (e_hwc_window_is_target(hwc_window)) continue;

        /* fetch the window buffer */
        if (hwc->pp_set)
          ret = e_hwc_window_buffer_fetch(hwc_window, EINA_FALSE);
        else
          ret = e_hwc_window_buffer_fetch(hwc_window, EINA_TRUE);
        if (ret)
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

        /* update the window's props */
        if (e_hwc_window_prop_update(hwc_window))
          update_changes = EINA_TRUE;

        if (e_hwc_window_device_state_available_update(hwc_window))
          update_changes = EINA_TRUE;
     }

   /* update the states of the visible windows when there is something to update */
   if (update_changes)
     {
        EHWSTRACE("======= HWC Update the Windows' Changes =====", NULL, hwc);
        _e_hwc_windows_visible_windows_states_update(hwc);
     }

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
   Eina_Bool ret = EINA_FALSE;

   /* validate the visible hwc_windows' states*/
   if (!_e_hwc_windows_validate(hwc, &num_changes))
     {
        EHWSERR("_e_hwc_windows_validate failed.", hwc);
        goto re_evaluate;
     }

   /* update the valiated_changes if there are the composition changes after validation */
   if (num_changes)
     {
        if (!_e_hwc_windows_validated_changes_update(hwc, num_changes))
          {
             EHWSERR("_e_hwc_windows_validated_changes_update failed.", hwc);
             goto re_evaluate;
          }
     }

   /* constraints update and update the windows to be composited to the target_buffer */
   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        if (e_hwc_window_is_target(hwc_window)) continue;

        e_hwc_window_constraints_update(hwc_window);
        if (hwc->pp_hwc_window)
          e_hwc_window_pp_rendered_window_update(hwc_window);
        else
          e_hwc_window_rendered_window_update(hwc_window);

        if (hwc_window->state == E_HWC_WINDOW_STATE_CLIENT) num_client++;
        if (hwc_window->state == E_HWC_WINDOW_STATE_DEVICE) num_device++;
        if (hwc_window->state == E_HWC_WINDOW_STATE_VIDEO) num_video++;
     }

   /* update the E_HWC_MODE */
   hwc_mode = _e_hwc_windows_hwc_mode_update(hwc, num_client, num_device, num_video);

   /* set the state of the target_window */
   if (hwc_mode == E_HWC_MODE_NONE)
     {
        EHWSTRACE(" HWC_MODE is NONE composition.", NULL, hwc);
        _e_hwc_windows_target_state_set(hwc->target_hwc_window, E_HWC_WINDOW_STATE_DEVICE);
     }
   else if (hwc_mode == E_HWC_MODE_HYBRID)
     {
        EHWSTRACE(" HWC_MODE is HYBRID composition.", NULL, hwc);
        _e_hwc_windows_target_state_set(hwc->target_hwc_window, E_HWC_WINDOW_STATE_DEVICE);
     }
   else
     {
        EHWSTRACE(" HWC_MODE is FULL HW composition.", NULL, hwc);
        _e_hwc_windows_target_state_set(hwc->target_hwc_window, E_HWC_WINDOW_STATE_NONE);
     }

   /* skip the target_buffer when the window is on trainsition of the composition */
   if (hwc_mode != E_HWC_MODE_FULL && _e_hwc_windows_transition_check(hwc))
     {
        /* if pp set enabled, buffer set to tdm after pp done */
        if (hwc->pp_set)
          _e_hwc_windows_target_window_buffer_skip(hwc, EINA_FALSE);
        else
          _e_hwc_windows_target_window_buffer_skip(hwc, EINA_TRUE);
        goto re_evaluate;
     }

   /* accept the result of the validation */
   if (hwc->pp_set)
     ret = _e_hwc_windows_accept(hwc, EINA_FALSE);
   else
     ret = _e_hwc_windows_accept(hwc, EINA_TRUE);
   if (!ret)
     {
        EHWSERR("_e_hwc_windows_validated_changes_update failed.", hwc);
        goto re_evaluate;
     }

   return EINA_TRUE;

re_evaluate:
   EHWSTRACE("======= HWC NOT Accept Validation Yet !! =======", NULL, hwc);

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

static Eina_Bool
_e_hwc_windos_pp_pending_window_check(E_Hwc *hwc)
{
   E_Hwc_Window *hwc_window = NULL;
   E_Hwc_Window *hwc_window_pp = NULL;
   const Eina_List *l;

   if (eina_list_count(hwc->pending_pp_hwc_window_list) != 0)
     {
        hwc_window_pp = _e_hwc_windows_pp_get_hwc_window_for_zoom(hwc);
        EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window_pp, EINA_FALSE);

        EINA_LIST_FOREACH(hwc->pending_pp_hwc_window_list, l, hwc_window)
          {
             if (hwc_window == hwc_window_pp)
               return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_hwc_windows_init(void)
{
   if (!e_hwc_window_init())
     {
        ERR("E_Hwc_Window init failed");
        return EINA_FALSE;
     }

   if (!e_hwc_window_queue_init())
     {
        ERR("E_Hwc_Window_Queue init failed");
        e_hwc_window_deinit();
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN void
e_hwc_windows_deinit(void)
{
   e_hwc_window_queue_deinit();
   e_hwc_window_deinit();
}

EINTERN Eina_Bool
e_hwc_windows_render(E_Hwc *hwc)
{
   E_Hwc_Window_Target *target_hwc_window;
   E_Hwc_Window *target_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   target_hwc_window = hwc->target_hwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   target_window = (E_Hwc_Window *)target_hwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_window->queue, EINA_FALSE);

   if (e_hwc_window_state_get(target_window) == E_HWC_WINDOW_STATE_NONE)
     {
        evas_norender(target_hwc_window->evas);
        return EINA_TRUE;
     }

   if (e_comp_canvas_norender_get() > 0)
     {
        EHWSTRACE("NoRender get. Do not ecore_evas_manual_render.", NULL, hwc);
        return EINA_TRUE;
     }

   if (e_hwc_window_queue_buffer_can_dequeue(target_window->queue))
     {
        TRACE_DS_BEGIN(MANUAL RENDER);
        target_hwc_window->is_rendering = EINA_TRUE;
        ecore_evas_manual_render(target_hwc_window->ee);
        target_hwc_window->is_rendering = EINA_FALSE;
        TRACE_DS_END();
     }

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

   if (!_e_hwc_windows_changes_update(hwc))
     return EINA_TRUE;

   if (!_e_hwc_windows_evaluate(hwc))
     return EINA_TRUE;

   if (e_comp_canvas_norender_get() > 0)
     {
        EHWSTRACE(" Block Display... NoRender get.", NULL, hwc);
        return EINA_TRUE;
     }

   if (hwc->hwc_mode != E_HWC_MODE_FULL) {
     if (!_e_hwc_windows_target_buffer_prepared(hwc))
       return EINA_TRUE;
   }

   if (output->dpms == E_OUTPUT_DPMS_OFF)
     {
        _e_hwc_windows_offscreen_commit(hwc);
        return EINA_TRUE;
     }

   if (hwc->pp_set)
     {
        if (_e_hwc_windos_pp_pending_window_check(hwc))
          return EINA_TRUE;
     }

   if (!_e_hwc_windows_commit_data_acquire(hwc))
     return EINA_TRUE;

   if (hwc->pp_unset)
     hwc->pp_unset = EINA_FALSE;

   if (hwc->pp_set)
     {
        e_output_zoom_rotating_check(output);
        _e_hwc_windows_update_fps(hwc);
        if (!_e_hwc_windows_pp_commit(hwc))
          {
            EHWSERR("_e_hwc_windows_pp_commit failed.", hwc);
            goto fail;
          }
     }
   else
     {
        EHWSTRACE("!!!!!!!! HWC Commit !!!!!!!!", NULL, hwc);
        _e_hwc_windows_update_fps(hwc);

        hwc->wait_commit = EINA_TRUE;

        error = tdm_hwc_commit(hwc->thwc, 0, _e_hwc_windows_commit_handler, hwc);
        if (error != TDM_ERROR_NONE)
          {
             EHWSERR("tdm_hwc_commit failed.", hwc);
             _e_hwc_windows_commit_handler(hwc->thwc, 0, 0, 0, hwc);
             goto fail;
          }
     }

   return EINA_TRUE;

fail:
   hwc->wait_commit = EINA_FALSE;

   return EINA_FALSE;
}

EINTERN E_Hwc_Window_Target *
e_hwc_windows_target_window_new(E_Hwc *hwc)
{
   E_Hwc_Window_Target *target_hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc->thwc, NULL);

   if (e_hwc_policy_get(hwc) == E_HWC_POLICY_PLANES)
     return NULL;

   target_hwc_window = _e_hwc_windows_target_window_new(hwc);
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, NULL);
   target_hwc_window->hwc = hwc;

   hwc->hwc_windows = eina_list_append(hwc->hwc_windows, target_hwc_window);

   return target_hwc_window;
}

EINTERN void
e_hwc_windows_target_window_del(E_Hwc_Window_Target *target_hwc_window)
{
   E_Hwc *hwc;

   EINA_SAFETY_ON_NULL_RETURN(target_hwc_window);

   hwc = target_hwc_window->hwc;
   EINA_SAFETY_ON_NULL_RETURN(hwc);

   hwc->hwc_windows = eina_list_remove(hwc->hwc_windows, hwc->target_hwc_window);
   e_object_del(E_OBJECT(hwc->target_hwc_window));
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
             EHWSERR("fail tdm pp create", hwc);
             goto fail;
          }

        ret = tdm_pp_set_done_handler(hwc->tpp, _e_hwc_windows_pp_commit_handler, hwc);
        EINA_SAFETY_ON_FALSE_GOTO(ret == TDM_ERROR_NONE, fail);
     }

   if (!hwc->pp_tqueue)
     {
        //TODO: Does e20 get the buffer flags from the tdm backend?
        hwc->pp_tqueue = tbm_surface_queue_create(3, w, h, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
        if (!hwc->pp_tqueue)
          {
             EHWSERR("fail tbm_surface_queue_create", hwc);
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
   hwc->pp_unset = EINA_FALSE;
   hwc->pp_hwc_window = NULL;

   /* to wake up main loop */
   uint64_t value = 1;
   if (write(hwc->target_hwc_window->event_fd, &value, sizeof(value)) < 0)
     EHWSERR("failed to wake up main loop:%m", hwc);

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
   hwc->pp_hwc_window = NULL;
   hwc->pp_unset = EINA_TRUE;

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
     EHWSERR("failed to wake up main loop:%m", hwc);
}

// add hwc_window to the render_list
EINTERN void
e_hwc_windows_rendered_window_add(E_Hwc_Window *hwc_window)
{
   E_Hwc *hwc;
   E_Hwc_Window_Target *target_hwc_window;
   E_Client *ec;

   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN(ec);

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_RETURN(hwc);

   target_hwc_window = hwc->target_hwc_window;
   EINA_SAFETY_ON_NULL_RETURN(target_hwc_window);

   target_hwc_window->rendered_windows =
           eina_list_append(target_hwc_window->rendered_windows, hwc_window);

   e_object_ref(E_OBJECT(hwc_window));

   EHWSTRACE(" add ehw:%p ts:%p to the render_list -- {%25s}.", ec, hwc, hwc_window,
            hwc_window->buffer.tsurface, e_hwc_window_name_get(hwc_window));
}

EINTERN Eina_Bool
e_hwc_windows_fps_get(E_Hwc *hwc, double *fps)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   if (hwc->old_fps == hwc->fps)
     return EINA_FALSE;

   if (hwc->fps > 0.0)
     {
        *fps = hwc->fps;
        hwc->old_fps = hwc->fps;
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_hwc_windows_get_available_properties(E_Hwc *hwc, const tdm_prop **props, int *count)
{
   tdm_hwc *thwc;
   tdm_error ret = TDM_ERROR_OPERATION_FAILED;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(props, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(count, EINA_FALSE);

   thwc = hwc->thwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc, EINA_FALSE);

   ret = tdm_hwc_get_available_properties(thwc, props, count);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(ret != TDM_ERROR_NONE, ret);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_windows_get_video_available_properties(E_Hwc *hwc, const tdm_prop **props, int *count)
{
   tdm_hwc *thwc;
   tdm_error ret = TDM_ERROR_OPERATION_FAILED;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(props, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(count, EINA_FALSE);

   thwc = hwc->thwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc, EINA_FALSE);

   ret = tdm_hwc_get_video_available_properties(thwc, props, count);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(ret != TDM_ERROR_NONE, ret);

   return EINA_TRUE;
}

EINTERN void
e_hwc_windows_trace_debug(Eina_Bool onoff)
{
   if (onoff == ehws_trace) return;
   ehws_trace = onoff;
   e_hwc_window_trace_debug(onoff);
   e_hwc_window_queue_trace_debug(onoff);
   INF("EHWS: hwc trace_debug is %s", onoff?"ON":"OFF");
}

EINTERN void
e_hwc_windows_dump_start(void)
{
   if (ehws_dump_enable) return;

   ehws_dump_enable = EINA_TRUE;
}

EINTERN void
e_hwc_windows_dump_stop(void)
{
   if (!ehws_dump_enable) return;

   ehws_dump_enable = EINA_FALSE;
}

static int
_e_hwc_windows_window_debug_cb_sort(const void *d1, const void *d2)
{
   E_Hwc_Window *hwc_window1 = (E_Hwc_Window *)d1;
   E_Hwc_Window *hwc_window2 = (E_Hwc_Window *)d2;

   if (!hwc_window1) return(1);
   if (!hwc_window2) return(-1);

   return (hwc_window2->zpos - hwc_window1->zpos);
}

static void
_e_hwc_windows_window_debug_info_get(Eldbus_Message_Iter *iter, E_Hwc_Wins_Debug_Cmd cmd)
{
   Eldbus_Message_Iter *line_array;
   E_Hwc_Window *hwc_window = NULL;
   E_Comp_Screen *e_comp_screen = NULL;
   E_Output *output = NULL;
   E_Hwc *hwc = NULL;
   Eina_List *l, *l2;
   int idx = 0;
   char info_str[1024];
   char fmt_str[5];
   char flip = ' ';

   e_comp_screen = e_comp->e_comp_screen;

   eldbus_message_iter_arguments_append(iter, "as", &line_array);
   if (!e_comp_screen)
     {
        eldbus_message_iter_basic_append(line_array,
                                         's',
                                         "e_comp_screen not initialized..");
        eldbus_message_iter_container_close(iter, line_array);
        return;
     }

   eldbus_message_iter_basic_append(line_array, 's',
   "==========================================================================================="
   "=============================================================");
   eldbus_message_iter_basic_append(line_array, 's',
   " No   Win_ID    Hwc_win    zpos  ST   AC_ST  ACTI TRANSI  tsurface  src_size        src_pos"
   "       FMT       dst_pos        TRANSF DP_tsurface   Queue");
   eldbus_message_iter_basic_append(line_array, 's',
   "==========================================================================================="
   "=============================================================");

   EINA_LIST_FOREACH(e_comp_screen->outputs, l, output)
     {
        if (!output) continue;
        if (!output->config.enabled) continue;
        if (!output->tdm_hwc) continue;

        hwc = output->hwc;
        if (!output->hwc) continue;

        hwc->hwc_windows = eina_list_sort(hwc->hwc_windows, eina_list_count(hwc->hwc_windows), _e_hwc_windows_window_debug_cb_sort);

        EINA_LIST_FOREACH(hwc->hwc_windows, l2, hwc_window)
          {
             if (!hwc_window) continue;

             if ((cmd == E_HWC_WINS_DEBUG_CMD_VIS) && (hwc_window->accepted_state == E_HWC_WINDOW_STATE_NONE))
               continue;
             else if ((cmd == E_HWC_WINS_DEBUG_CMD_DV) && (hwc_window->accepted_state != E_HWC_WINDOW_STATE_DEVICE))
               continue;
             else if ((cmd == E_HWC_WINS_DEBUG_CMD_CL) && (hwc_window->accepted_state != E_HWC_WINDOW_STATE_CLIENT))
               continue;
             else if ((cmd == E_HWC_WINS_DEBUG_CMD_CS) && (hwc_window->accepted_state != E_HWC_WINDOW_STATE_CURSOR))
               continue;
             else if ((cmd == E_HWC_WINS_DEBUG_CMD_VD) && (hwc_window->accepted_state != E_HWC_WINDOW_STATE_VIDEO))
               continue;
             else if ((cmd == E_HWC_WINS_DEBUG_CMD_NO) && (hwc_window->accepted_state != E_HWC_WINDOW_STATE_NONE))
               continue;

             if (hwc_window->info.src_config.format)
               snprintf(fmt_str, sizeof(fmt_str), "%c%c%c%c", FOURCC_STR(hwc_window->info.src_config.format));
             else
               snprintf(fmt_str, sizeof(fmt_str), "    ");

             if (hwc_window->info.transform > TDM_TRANSFORM_270)
               flip = 'F';

             snprintf(info_str, sizeof(info_str),
                      "%3d 0x%08zx 0x%08zx %4d   %s    %s     %s   %s 0x%08zx %04dx%04d %04dx%04d+%04d+%04d"
                      " %4s %04dx%04d+%04d+%04d  %c%3d  0x%08zx  0x%08zx",
                      ++idx,
                      e_client_util_win_get(hwc_window->ec),
                      (uintptr_t)hwc_window,
                      hwc_window->zpos,
                      e_hwc_window_state_string_get(hwc_window->state),
                      e_hwc_window_state_string_get(hwc_window->accepted_state),
                      hwc_window->accepted_state ? "A" : "D",
                      e_hwc_window_transition_string_get(hwc_window->transition),
                      (uintptr_t)hwc_window->buffer.tsurface,
                      hwc_window->info.src_config.size.h,
                      hwc_window->info.src_config.size.v,
                      hwc_window->info.src_config.pos.w,
                      hwc_window->info.src_config.pos.h,
                      hwc_window->info.src_config.pos.x,
                      hwc_window->info.src_config.pos.y,
                      fmt_str,
                      hwc_window->info.dst_pos.w,
                      hwc_window->info.dst_pos.h,
                      hwc_window->info.dst_pos.x,
                      hwc_window->info.dst_pos.y,
                      flip,
                      (hwc_window->info.transform < 4) ? hwc_window->info.transform * 90 : (hwc_window->info.transform - 4) * 90,
                      (uintptr_t)hwc_window->display.buffer.tsurface,
                      (uintptr_t)hwc_window->queue);
             eldbus_message_iter_basic_append(line_array, 's', info_str);
          }
     }

   eldbus_message_iter_basic_append(line_array, 's',
   "==========================================================================================="
   "=============================================================");

   eldbus_message_iter_container_close(iter, line_array);
}

EINTERN void
e_hwc_windows_debug_info_get(Eldbus_Message_Iter *iter, E_Hwc_Wins_Debug_Cmd cmd)
{
   if (cmd == E_HWC_WINS_DEBUG_CMD_QUEUE)
     e_hwc_window_queue_debug_info_get(iter);
   else if (cmd >= E_HWC_WINS_DEBUG_CMD_VIS && cmd <= E_HWC_WINS_DEBUG_CMD_NO)
     _e_hwc_windows_window_debug_info_get(iter, cmd);
}
