#include "e.h"

# include <Evas_Engine_GL_Tbm.h>
# include <Evas_Engine_Software_Tbm.h>
# include <sys/eventfd.h>
# include <gbm/gbm_tbm.h>
# include <pixman.h>
# include <wayland-tbm-server.h>

#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof (x))
#endif

#define EHWINF(f, ec, ehw, x...)                                \
   do                                                           \
     {                                                          \
        if ((!ec) && (!ehw))                                    \
          INF("EWL|%20.20s|              |             |"f,     \
              "HWC-WIN", ##x);                                  \
        else                                                    \
          INF("EWL|%20.20s|win:0x%08x|ec:0x%08x| ehw:0x%08x "f, \
              "HWC-WIN",                                        \
              (unsigned int)(e_client_util_win_get(ec)),        \
              (unsigned int)(ec),                               \
              (unsigned int)(ehw),                              \
              ##x);                                             \
     }                                                          \
   while (0)

#define EHWTRACE(f, ec, ehw, x...)                                  \
   do                                                               \
     {                                                              \
        if (ehw_trace)                                              \
          {                                                         \
            if ((!ec) && (!ehw))                                    \
              INF("EWL|%20.20s|              |             |"f,     \
                  "HWC-WIN", ##x);                                  \
            else                                                    \
              INF("EWL|%20.20s|win:0x%08x|ec:0x%08x| ehw:0x%08x "f, \
                  "HWC-WIN",                                        \
                  (unsigned int)(e_client_util_win_get(ec)),        \
                  (unsigned int)(ec),                               \
                  (unsigned int)(ehw),                              \
                  ##x);                                             \
          }                                                         \
     }                                                              \
   while (0)

static Eina_Bool ehw_trace = EINA_TRUE;
static E_Client_Hook *client_hook_new = NULL;
static E_Client_Hook *client_hook_del = NULL;
static Ecore_Event_Handler *zone_set_event_handler = NULL;
static uint64_t ee_rendered_hw_list_key;

static int _e_hwc_window_hooks_delete = 0;
static int _e_hwc_window_hooks_walking = 0;

static Eina_Inlist *_e_hwc_window_hooks[] =
{
   [E_HWC_WINDOW_HOOK_ACCEPTED_STATE_CHANGE] = NULL,
};

static void
_e_hwc_window_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Hwc_Window_Hook *ch;
   unsigned int x;
   for (x = 0; x < E_HWC_WINDOW_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_hwc_window_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_hwc_window_hooks[x] = eina_inlist_remove(_e_hwc_window_hooks[x], EINA_INLIST_GET(ch));
          free(ch);
       }
}

static void
_e_hwc_window_hook_call(E_Hwc_Window_Hook_Point hookpoint, E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_Hook *ch;

   _e_hwc_window_hooks_walking++;
   EINA_INLIST_FOREACH(_e_hwc_window_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        ch->func(ch->data, hwc_window);
     }
   _e_hwc_window_hooks_walking--;
   if ((_e_hwc_window_hooks_walking == 0) && (_e_hwc_window_hooks_delete > 0))
     _e_hwc_window_hooks_clean();
}

static E_Comp_Wl_Buffer *
_e_hwc_window_comp_wl_buffer_get(E_Hwc_Window *hwc_window)
{
   E_Client *ec = hwc_window->ec;
   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   E_Comp_Wl_Buffer_Ref *buffer_ref;

   if (!cdata) return NULL;

   buffer_ref = &cdata->buffer_ref;

   return buffer_ref->buffer;
}

struct wayland_tbm_client_queue *
_get_wayland_tbm_client_queue(E_Client *ec)
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
   if (!cqueue)
     {
        ELOGF("HWC-WINS", " ehw:%p has no wl_tbm_server_client_queue. -- {%25s}, state:%s, zpos:%d, deleted:%s",
              ec->pixmap, ec, ec->hwc_window, ec->icccm.title,
              e_hwc_window_state_string_get(ec->hwc_window->state),
              ec->hwc_window->zpos, ec->hwc_window->is_deleted ? "yes" : "no");
     }
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, NULL);

   return cqueue;
}

static tbm_surface_queue_h
_get_tbm_surface_queue()
{
   return e_comp->e_comp_screen->tqueue;
}

static tdm_hwc_window_composition
_get_composition_type(E_Hwc_Window_State state)
{
   tdm_hwc_window_composition composition_type = TDM_COMPOSITION_NONE;

   switch (state)
     {
      case E_HWC_WINDOW_STATE_NONE:
        composition_type = TDM_COMPOSITION_NONE;
        break;
      case E_HWC_WINDOW_STATE_CLIENT:
        composition_type = TDM_COMPOSITION_CLIENT;
        break;
      case E_HWC_WINDOW_STATE_DEVICE:
        composition_type = TDM_COMPOSITION_DEVICE;
        break;
      case E_HWC_WINDOW_STATE_DEVICE_CANDIDATE:
        composition_type = TDM_COMPOSITION_DEVICE_CANDIDATE;
        break;
      case E_HWC_WINDOW_STATE_CURSOR:
        composition_type = TDM_COMPOSITION_CURSOR;
        break;
      case E_HWC_WINDOW_STATE_VIDEO:
        composition_type = TDM_COMPOSITION_VIDEO;
        break;
      default:
        composition_type = TDM_COMPOSITION_NONE;
        ERR("hwc-opt: unknown state of hwc_window.");
     }

   return composition_type;
}

static unsigned int
_get_aligned_width(tbm_surface_h tsurface)
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

static tbm_surface_h
_e_hwc_window_target_window_surface_acquire(E_Hwc_Window_Target *target_hwc_window)
{
   tbm_surface_queue_h queue = NULL;
   tbm_surface_h surface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, NULL);

   queue = target_hwc_window->queue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, NULL);

   if (tbm_surface_queue_can_acquire(queue, 0))
     {
        tsq_err = tbm_surface_queue_acquire(queue, &surface);

        if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
          {
             ERR("Failed to acquire tbm_surface from tbm_surface_queue(%p): tsq_err = %d", queue, tsq_err);
             return NULL;
          }
     }
   else
     {
        return NULL;
     }

   return surface;
}

static void
_e_hwc_window_target_window_surface_release(E_Hwc_Window_Target *target_hwc_window, tbm_surface_h tsurface)
{
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   tbm_surface_queue_h tqueue = NULL;

   EINA_SAFETY_ON_NULL_RETURN(target_hwc_window);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);

   tqueue = target_hwc_window->queue;
   EINA_SAFETY_ON_NULL_RETURN(tqueue);

   tsq_err = tbm_surface_queue_release(tqueue, tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("Failed to release tbm_surface(%p) from tbm_surface_queue(%p): tsq_err = %d", tsurface, tqueue, tsq_err);
        return;
     }
}

static void
_e_hwc_window_target_window_surface_data_free(void *data)
{
   Eina_List *ee_rendered_hw_list = (Eina_List *)data;

   eina_list_free(ee_rendered_hw_list);
}

/* gets called as somebody modifies target_window's queue */
static void
_e_hwc_window_target_window_surface_queue_trace_cb(tbm_surface_queue_h surface_queue,
        tbm_surface_h tsurface, tbm_surface_queue_trace trace, void *data)
{
   E_Hwc_Window_Target *target_hwc_window = (E_Hwc_Window_Target *)data;

   /* gets called as evas_renderer dequeues a new buffer from the queue */
   if (trace == TBM_SURFACE_QUEUE_TRACE_DEQUEUE)
     {
        ELOGF("HWC-WINS", " ehw:%p gets dequeue noti ts:%p -- {%s}.",
              NULL, NULL, target_hwc_window, tsurface, "@TARGET WINDOW@");

        tbm_surface_internal_add_user_data(tsurface, ee_rendered_hw_list_key, _e_hwc_window_target_window_surface_data_free);
        target_hwc_window->dequeued_tsurface = tsurface;
     }
   /* gets called as evas_renderer enqueues a new buffer into the queue */
   if (trace == TBM_SURFACE_QUEUE_TRACE_ENQUEUE)
     {
        uint64_t value = 1;
        int ret;

        ret = write(target_hwc_window->event_fd, &value, sizeof(value));
        if (ret == -1)
          ERR("failed to send acquirable event:%m");

        ELOGF("HWC-WINS", " ehw:%p gets enqueue noti ts:%p -- {%s}.",
              NULL, NULL, target_hwc_window, tsurface, "@TARGET WINDOW@");

        tbm_surface_internal_add_user_data(tsurface, ee_rendered_hw_list_key, _e_hwc_window_target_window_surface_data_free);
        target_hwc_window->dequeued_tsurface = tsurface;
     }
   /* tsurface has been released at the queue */
   if (trace == TBM_SURFACE_QUEUE_TRACE_RELEASE)
     {
        tbm_surface_internal_delete_user_data(tsurface, ee_rendered_hw_list_key);
        ELOGF("HWC-WINS", " ehw:%p gets release noti ts:%p -- {%s}.",
              NULL, NULL, target_hwc_window, tsurface, "@TARGET WINDOW@");
     }
}

/* gets called as evas_renderer enqueues a new buffer into the queue */
static void
_e_hwc_window_target_window_surface_queue_acquirable_cb(tbm_surface_queue_h surface_queue, void *data)
{
 // TODO: This function to be deprecated.
#if 0
    E_Hwc_Window_Target *target_hwc_window = (E_Hwc_Window_Target *)data;
    uint64_t value = 1;
    int ret;

    ret = write(target_hwc_window->event_fd, &value, sizeof(value));
    if (ret == -1)
      ERR("failed to send acquirable event:%m");
#endif
}

/* gets called at the beginning of an ecore_main_loop iteration */
static Eina_Bool
_e_hwc_window_target_window_render_finished_cb(void *data, Ecore_Fd_Handler *fd_handler)
{
   int len;
   int fd;
   char buffer[64];

   ELOGF("HWC-WINS", " ecore_main_loop: the new iteration.", NULL, NULL);

   fd = ecore_main_fd_handler_fd_get(fd_handler);
   if (fd < 0) return ECORE_CALLBACK_RENEW;

   len = read(fd, buffer, sizeof(buffer));
   if (len == -1)
     ERR("failed to read queue acquire event fd:%m");

   return ECORE_CALLBACK_RENEW;
}

static void
_e_hwc_window_target_window_render_flush_post_cb(void *data, Evas *e EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Hwc_Window_Target *target_hwc_window = (E_Hwc_Window_Target *)data;
   Eina_List *ee_rendered_hw_list;

   ELOGF("HWC-WINS", " ehw:%p gets render_flush_post noti ------ {@TARGET WINDOW@}", NULL, NULL, target_hwc_window);

   /* all ecs have been composited so we can attach a list of composited e_thwc_windows to the surface
    * which contains their ecs composited */

   ee_rendered_hw_list = eina_list_clone(target_hwc_window->ee_rendered_hw_list);

   tbm_surface_internal_set_user_data(target_hwc_window->dequeued_tsurface,
           ee_rendered_hw_list_key, ee_rendered_hw_list);

   eina_list_free(target_hwc_window->ee_rendered_hw_list);
   target_hwc_window->ee_rendered_hw_list = NULL;
   target_hwc_window->dequeued_tsurface = NULL;
}

static E_Hwc_Window_Target *
_e_hwc_window_target_new(E_Hwc *hwc)
{
   const char *name = NULL;
   E_Hwc_Window_Target *target_hwc_window = NULL;

   name = ecore_evas_engine_name_get(e_comp->ee);
   EINA_SAFETY_ON_NULL_RETURN_VAL(name, NULL);

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
   else if(!strcmp("software_tbm", name))
     {
        ecore_evas_manual_render_set(e_comp->ee, 1);
     }

   target_hwc_window = E_NEW(E_Hwc_Window_Target, 1);
   EINA_SAFETY_ON_NULL_GOTO(target_hwc_window, fail);

   ((E_Hwc_Window *)target_hwc_window)->is_target = EINA_TRUE;
   ((E_Hwc_Window *)target_hwc_window)->state = E_HWC_WINDOW_STATE_DEVICE;
   ((E_Hwc_Window *)target_hwc_window)->hwc = hwc;

   target_hwc_window->ee = e_comp->ee;
   target_hwc_window->evas = ecore_evas_get(target_hwc_window->ee);
   target_hwc_window->event_fd = eventfd(0, EFD_NONBLOCK);
   target_hwc_window->event_hdlr =
            ecore_main_fd_handler_add(target_hwc_window->event_fd, ECORE_FD_READ,
                                      _e_hwc_window_target_window_render_finished_cb,
                                      (void *)target_hwc_window, NULL, NULL);

   ecore_evas_manual_render(target_hwc_window->ee);

   target_hwc_window->queue = _get_tbm_surface_queue();

   /* as evas_renderer has finished its work (to provide a composited buffer) it enqueues
    * the result buffer into this queue and acquirable cb gets called; this cb does nothing
    * except the writing into the event_fd object, this writing causes the new ecore_main loop
    * iteration to be triggered ('cause we've registered ecore_main fd handler to check this writing);
    * so it's just a way to inform E20's HWC that evas_renderer has done its work */
   tbm_surface_queue_add_acquirable_cb(target_hwc_window->queue, _e_hwc_window_target_window_surface_queue_acquirable_cb, (void *)target_hwc_window);

   /* TODO: we can use this call instead of an add_acquirable_cb and an add_dequeue_cb calls. */
   tbm_surface_queue_add_trace_cb(target_hwc_window->queue, _e_hwc_window_target_window_surface_queue_trace_cb, (void *)target_hwc_window);

   evas_event_callback_add(e_comp->evas, EVAS_CALLBACK_RENDER_FLUSH_POST, _e_hwc_window_target_window_render_flush_post_cb, target_hwc_window);

   /* sorry..., current version of gcc requires an initializer to be evaluated at compile time */
   ee_rendered_hw_list_key = (uintptr_t)&ee_rendered_hw_list_key;

   return target_hwc_window;

fail:
   ecore_evas_manual_render_set(e_comp->ee, 0);

   return NULL;
}

static E_Hwc_Window_Target *
_e_hwc_window_target_window_get(E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_Target *target_hwc_window;
   E_Hwc *hwc;

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, NULL);

   target_hwc_window = hwc->target_hwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, NULL);

   return target_hwc_window;
}

static Eina_Bool
_e_hwc_window_target_window_clear(E_Hwc_Window_Target *target_hwc_window)
{
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   tqueue = target_hwc_window->queue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   while ((tsurface = _e_hwc_window_target_window_surface_acquire(target_hwc_window)))
     _e_hwc_window_target_window_surface_release(target_hwc_window, tsurface);

  return EINA_TRUE;
}

static void
_e_hwc_window_client_cb_new(void *data EINA_UNUSED, E_Client *ec)
{
   E_Output *output;
   E_Hwc_Window *hwc_window;
   E_Zone *zone;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   zone = ec->zone;
   if (!zone)
     {
        ELOGF("HWC-WINS", "Try to create hwc_window, but it couldn't.(no zone)", ec->pixmap, ec);
        return;
     }

   EINA_SAFETY_ON_NULL_RETURN(zone->output_id);

   output = e_output_find(zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN(output);

   /* If an e_client belongs to the e_output managed by hwc_plane policy,
    * there's no need to deal with hwc_windows. */
   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
     return;

   hwc_window = e_hwc_window_new(output->hwc, ec, E_HWC_WINDOW_STATE_NONE);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   /* set the hwc window to the e client */
   ec->hwc_window = hwc_window;

   return;
}

static void
_e_hwc_window_client_cb_del(void *data EINA_UNUSED, E_Client *ec)
{
   E_Output *output;
   E_Zone *zone;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   zone = ec->zone;
   EINA_SAFETY_ON_NULL_RETURN(zone);
   EINA_SAFETY_ON_NULL_RETURN(zone->output_id);

   output = e_output_find(zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN(output);

   /* If an e_client belongs to the e_output managed by hwc_plane policy,
    * there's no need to deal with hwc_windows. */
   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
     return;

   if (!ec->hwc_window) return;

   e_hwc_window_free(ec->hwc_window);
   ec->hwc_window = NULL;
}

static Eina_Bool
_e_hwc_window_client_cb_zone_set(void *data, int type, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;
   E_Zone *zone;
   E_Output *output;
   E_Hwc_Window *hwc_window = NULL;

   ev = event;
   EINA_SAFETY_ON_NULL_GOTO(ev, end);

   ec = ev->ec;
   EINA_SAFETY_ON_NULL_GOTO(ec, end);

   zone = ec->zone;
   EINA_SAFETY_ON_NULL_GOTO(zone, end);
   EINA_SAFETY_ON_NULL_GOTO(zone->output_id, end);

   output = e_output_find(zone->output_id);
   EINA_SAFETY_ON_NULL_GOTO(output, end);

   /* If an e_client belongs to the e_output managed by hwc_plane policy,
    * there's no need to deal with hwc_windows. */
   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
      return ECORE_CALLBACK_PASS_ON;

   if (ec->hwc_window)
     {
        /* we manage the video window in the video module */
        if (e_hwc_window_is_video(ec->hwc_window)) goto end;
        if (ec->hwc_window->hwc == output->hwc) goto end;

        e_hwc_window_free(ec->hwc_window);
        ec->hwc_window = NULL;
     }

   hwc_window = e_hwc_window_new(output->hwc, ec, E_HWC_WINDOW_STATE_NONE);
   EINA_SAFETY_ON_NULL_GOTO(hwc_window, end);

   /* set the hwc window to the e client */
   ec->hwc_window = hwc_window;

   ELOGF("HWC-WINS", "ehw:%p is set on eout:%p, zone_id:%d.",
         hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec,
         hwc_window, output, zone->id);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static tbm_surface_h
_e_hwc_window_client_surface_acquire(E_Hwc_Window *hwc_window)
{
   E_Comp_Wl_Buffer *buffer = _e_hwc_window_comp_wl_buffer_get(hwc_window);
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   tbm_surface_h tsurface = NULL;

   if (!buffer)
   {
      ELOGF("HWC-WINS", "[soolim] ehw:%p E_Comp_Wl_Buffer is null.",
            hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec,
            hwc_window);
      return NULL;
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
_e_hwc_window_client_recover(E_Hwc_Window *hwc_window)
{
   E_Client *ec = hwc_window->ec;
   E_Comp_Wl_Client_Data *cdata = NULL;
   E_Comp_Wl_Buffer *buffer = NULL;
   tbm_surface_h tsurface =NULL;

   if (!ec) return;

   cdata = ec->comp_data;
   if (!cdata) return;

   buffer = cdata->buffer_ref.buffer;

   if (!buffer)
     {
        tsurface = e_hwc_window_displaying_surface_get(hwc_window);
        if (!tsurface) return;

        tbm_surface_internal_ref(tsurface);
        buffer = e_comp_wl_tbm_buffer_get(tsurface);
        if (!buffer)
          {
             tbm_surface_internal_unref(tsurface);
             return;
          }
     }

   /* force update */
   e_pixmap_resource_set(ec->pixmap, buffer);
   e_pixmap_dirty(ec->pixmap);
   e_pixmap_refresh(ec->pixmap);

   e_pixmap_image_refresh(ec->pixmap);
   e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
   e_comp_object_dirty(ec->frame);
   e_comp_object_render(ec->frame);

   return;
}

static void
_e_hwc_window_cursor_image_draw(E_Comp_Wl_Buffer *buffer, tbm_surface_info_s *tsurface_info, int rotation)
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

static Eina_Bool
_e_hwc_window_cursor_surface_refresh(E_Hwc_Window *hwc_window, E_Pointer *pointer)
{
   E_Hwc *hwc = NULL;
   E_Output *output = NULL;
   int w, h, tw, th;
   int tsurface_w, tsurface_h;
   void *src_ptr = NULL;
   tbm_surface_h tsurface = NULL;
   E_Comp_Wl_Buffer *buffer = NULL;
   tbm_surface_error_e ret = TBM_SURFACE_ERROR_NONE;
   tbm_surface_info_s tsurface_info;
   E_Client *ec;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   output = hwc->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   buffer = ec->comp_data->buffer_ref.buffer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, EINA_FALSE);

   if ((hwc_window->display_info.buffer_ref.buffer == buffer) &&
       (hwc_window->cursor_tsurface) &&
       (hwc_window->cursor_rotation == pointer->rotation))
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

   hwc_window->cursor_rotation = pointer->rotation;

   w = (output->cursor_available.min_w > tw) ? output->cursor_available.min_w : tw;
   h = (output->cursor_available.min_h > th) ? output->cursor_available.min_h : th;

   if (e_comp->hwc_reuse_cursor_buffer)
     {
        if (hwc_window->cursor_tsurface)
          {
             tsurface_w = tbm_surface_get_width(hwc_window->cursor_tsurface);
             tsurface_h = tbm_surface_get_height(hwc_window->cursor_tsurface);

             if (w != tsurface_w || h != tsurface_h)
               {
                  tbm_surface_destroy(hwc_window->cursor_tsurface);
                  hwc_window->cursor_tsurface = NULL;
               }
          }
     }
   else
     {
        if (hwc_window->cursor_tsurface)
          {
             tbm_surface_destroy(hwc_window->cursor_tsurface);
             hwc_window->cursor_tsurface = NULL;
          }
     }

   if (!hwc_window->cursor_tsurface)
     {
        /* Which tbm flags should be used? */
        tsurface = tbm_surface_internal_create_with_flags(w, h, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
        if (!tsurface) return EINA_FALSE;
     }
   else
     {
        tsurface = hwc_window->cursor_tsurface;
     }

   ret = tbm_surface_map(tsurface, TBM_SURF_OPTION_WRITE, &tsurface_info);
   if (ret != TBM_SURFACE_ERROR_NONE)
     {
        ERR("Failed to map tsurface");
        tbm_surface_destroy(tsurface);
        return EINA_FALSE;
     }

   _e_hwc_window_cursor_image_draw(buffer, &tsurface_info, pointer->rotation);

   tbm_surface_unmap(tsurface);

   hwc_window->cursor_tsurface = tsurface;

   /* to set the hwc_window_cursor_tsurface to the hwc_window->tsurface */
   e_comp_object_hwc_update_set(ec->frame, EINA_TRUE);

   return EINA_TRUE;
}

static tbm_surface_h
_e_hwc_window_cursor_surface_acquire(E_Hwc_Window *hwc_window)
{
   tbm_surface_h tsurface = NULL;
   E_Client *ec = hwc_window->ec;
   E_Comp_Wl_Buffer *buffer = NULL;
   E_Pointer *pointer = NULL;

   pointer = e_pointer_get(ec);
   if (!pointer)
     {
        ELOGF("HWC-WINS", "[soolim] ehw:%p Pointer is null.(Cursor)",
             hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec,
             hwc_window);
        return NULL;
     }

   buffer = ec->comp_data->buffer_ref.buffer;
   if (!buffer)
     {
        ELOGF("HWC-WINS", "[soolim] ehw:%p E_Comp_Wl_Buffer is null.(Cursor)",
             hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec,
             hwc_window);
        return NULL;
     }
#if 1
   if (hwc_window->cursor_tsurface) return hwc_window->cursor_tsurface;
#else
   /* return the exist tsurface when there is no update on cursor image(pointer image) */
   if (!e_comp_object_hwc_update_exists(ec->frame) && hwc_window->tsurface)
     return hwc_window->tsurface;
#endif

   //e_comp_object_hwc_update_set(ec->frame, EINA_FALSE);

   if (!_e_hwc_window_cursor_surface_refresh(hwc_window, pointer))
     {
        ERR("Failed to _e_hwc_window_cursor_surface_refresh");
        return NULL;
     }

   tsurface = hwc_window->cursor_tsurface;

   return tsurface;
}

static Eina_Bool
_e_hwc_window_info_set(E_Hwc_Window *hwc_window, tbm_surface_h tsurface)
{
   E_Hwc *hwc = hwc_window->hwc;
   E_Output *output = hwc->output;
   E_Client *ec = hwc_window->ec;
   tbm_surface_info_s surf_info;
   int size_w, size_h, src_x, src_y, src_w, src_h;
   int dst_x, dst_y, dst_w, dst_h;
   tbm_format format;
   tdm_hwc_window_info hwc_win_info = {0};
   tdm_error error;

   if (e_hwc_window_is_video(hwc_window))
     {
        if (!e_comp_wl_video_hwc_window_info_get(hwc_window, &hwc_win_info))
          {
             ERR("Video window does not get the hwc_win_info.");
             return EINA_FALSE;
          }

        error = tdm_hwc_window_set_info(hwc_window->thwc_window, &hwc_win_info);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

        return EINA_TRUE;
     }

   /* set hwc_window when the layer infomation is different from the previous one */
   tbm_surface_get_info(tsurface, &surf_info);

   format = surf_info.format;

   size_w = _get_aligned_width(tsurface);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(size_w == 0, EINA_FALSE);

   size_h = surf_info.height;

   src_x = 0;
   src_y = 0;
   src_w = surf_info.width;
   src_h = surf_info.height;

   if (e_hwc_window_is_cursor(hwc_window))
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

   if (hwc_window->info.src_config.size.h != size_w ||
            hwc_window->info.src_config.size.v != size_h ||
            hwc_window->info.src_config.pos.x != src_x ||
            hwc_window->info.src_config.pos.y != src_y ||
            hwc_window->info.src_config.pos.w != src_w ||
            hwc_window->info.src_config.pos.h != src_h ||
            hwc_window->info.src_config.format != format ||
            hwc_window->info.dst_pos.x != dst_x ||
            hwc_window->info.dst_pos.y != dst_y ||
            hwc_window->info.dst_pos.w != dst_w ||
            hwc_window->info.dst_pos.h != dst_h)
     {
        tdm_error error;

        /* change the information at plane->info */
        hwc_window->info.src_config.size.h = size_w;
        hwc_window->info.src_config.size.v = size_h;
        hwc_window->info.src_config.pos.x = src_x;
        hwc_window->info.src_config.pos.y = src_y;
        hwc_window->info.src_config.pos.w = src_w;
        hwc_window->info.src_config.pos.h = src_h;
        hwc_window->info.dst_pos.x = dst_x;
        hwc_window->info.dst_pos.y = dst_y;
        hwc_window->info.dst_pos.w = dst_w;
        hwc_window->info.dst_pos.h = dst_h;
        hwc_window->info.transform = TDM_TRANSFORM_NORMAL;
        hwc_window->info.src_config.format = format;

        error = tdm_hwc_window_set_info(hwc_window->thwc_window, &hwc_window->info);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);
     }

   return EINA_TRUE;
}

#if 0
static Eina_Bool
_e_hwc_window_correct_transformation_check(E_Hwc_Window *hwc_window)
{
   E_Client *ec;
   int transform;
   E_Hwc *hwc = hwc_window->hwc;
   E_Output *output = hwc->output;

   /* do not check the transformation of the video window */
   if (e_hwc_window_is_video(hwc_window)) return EINA_TRUE;

   ec = hwc_window->ec;

   transform = e_comp_wl_output_buffer_transform_get(ec);

   /* request an ec to change its transformation if it doesn't fit the transformation */
   if ((output->config.rotation / 90) != transform)
     {
        if (!e_config->screen_rotation_client_ignore && hwc_window->need_change_buffer_transform)
          {
             hwc_window->need_change_buffer_transform = EINA_FALSE;

             /* TODO: why e_comp_wl_output_init() call ins't enough? why additional
              * tizen_screen_rotation_send_ignore_output_transform() call is needed? */
             e_comp_screen_rotation_ignore_output_transform_send(ec, EINA_FALSE);

             ELOGF("HWC-WINS", " request {%25s} to change transformation to %d.",
                     ec->pixmap, ec, ec->icccm.title, output->config.rotation);
          }

        return EINA_FALSE;
     }
   else
      hwc_window->need_change_buffer_transform = EINA_TRUE;

   return EINA_TRUE;
}
#endif

EINTERN Eina_Bool
e_hwc_window_init(E_Hwc *hwc)
{
   E_Hwc_Window_Target *target_hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   if (e_hwc_policy_get(hwc) == E_HWC_POLICY_PLANES)
     return EINA_FALSE;

   client_hook_new =  e_client_hook_add(E_CLIENT_HOOK_NEW_CLIENT,
                                        _e_hwc_window_client_cb_new, NULL);
   if (!client_hook_new)
     {
        ERR("fail to add the E_CLIENT_HOOK_NEW_CLIENT hook.");
        return EINA_FALSE;
     }
   client_hook_del =  e_client_hook_add(E_CLIENT_HOOK_DEL,
                                        _e_hwc_window_client_cb_del, NULL);
   if (!client_hook_del)
     {
        ERR("fail to add the E_CLIENT_HOOK_DEL hook.");
        return EINA_FALSE;
     }

   zone_set_event_handler =
            ecore_event_handler_add(E_EVENT_CLIENT_ZONE_SET, _e_hwc_window_client_cb_zone_set, NULL);
   if (!zone_set_event_handler)
     {
        ERR("fail to add the E_EVENT_CLIENT_ZONE_SET event handler.");
        return EINA_FALSE;
     }

   target_hwc_window = _e_hwc_window_target_new(hwc);
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);
   target_hwc_window->hwc = hwc;

   /* set the target_window to the hwc */
   hwc->target_hwc_window = target_hwc_window;

   hwc->hwc_windows = eina_list_append(hwc->hwc_windows, target_hwc_window);

   return EINA_TRUE;
}

// TODO:
EINTERN void
e_hwc_window_deinit(E_Hwc *hwc)
{
   EINA_SAFETY_ON_NULL_RETURN(hwc);

   // TODO:
}

EINTERN E_Hwc_Window *
e_hwc_window_new(E_Hwc *hwc, E_Client *ec, E_Hwc_Window_State state)
{
   E_Hwc_Window *hwc_window = NULL;
   tdm_hwc *thwc;;
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);

   thwc = hwc->thwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc, EINA_FALSE);

   hwc_window = E_NEW(E_Hwc_Window, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, NULL);

   hwc_window->hwc = hwc;
   hwc_window->ec = ec;
   hwc_window->state = state;
   hwc_window->need_change_buffer_transform = EINA_TRUE;

   hwc_window->thwc_window = tdm_hwc_create_window(thwc, &error);
   if (error != TDM_ERROR_NONE)
     {
        ERR("cannot create tdm_hwc_window for thwc(%p)", thwc);
        E_FREE(hwc_window);
        return NULL;
     }

   /* cursor window */
   if (e_policy_client_is_cursor(ec))
     hwc_window->is_cursor = EINA_TRUE;

   /* video window */
   if (state == E_HWC_WINDOW_STATE_VIDEO)
     hwc_window->is_video = EINA_TRUE;

   hwc->hwc_windows = eina_list_append(hwc->hwc_windows, hwc_window);

   ELOGF("HWC-WINS", "ehw:%p is created on eout:%p, zone_id:%d",
         hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
         hwc_window, hwc->output, ec->zone->id);

   return hwc_window;
}

EINTERN void
e_hwc_window_free(E_Hwc_Window *hwc_window)
{
   E_Hwc *hwc = NULL;

   EINA_SAFETY_ON_NULL_RETURN(hwc_window);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window->hwc);

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_RETURN(hwc);

   /* we cannot remove the hwc_window because we need to release the commit_data */
   if (e_hwc_window_displaying_surface_get(hwc_window))
     {
        ELOGF("HWC-WINS", "ehw:%p is destroyed on ehwc:%p. displaying surface.",
              hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
              hwc_window, hwc);

        /* mark as deleted and delete when commit_data will be released */
        hwc_window->is_deleted = EINA_TRUE;
        hwc_window->ec = NULL;
        e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_NONE);
        return;
     }
   else
     ELOGF("HWC-WINS", "ehw:%p is destroyed on ehwc:%p",
           hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
           hwc_window, hwc);

   if (hwc_window->thwc_window)
      tdm_hwc_window_destroy(hwc_window->thwc_window);

   hwc->hwc_windows = eina_list_remove(hwc->hwc_windows, hwc_window);

   free(hwc_window);
}

EINTERN Eina_Bool
e_hwc_window_zpos_set(E_Hwc_Window *hwc_window, int zpos)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->zpos != zpos) hwc_window->zpos = zpos;

   return EINA_TRUE;
}

EINTERN int
e_hwc_window_zpos_get(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->state == E_HWC_WINDOW_STATE_NONE) return -999;

   return hwc_window->zpos;
}

EINTERN Eina_Bool
e_hwc_window_compsition_update(E_Hwc_Window *hwc_window)
{
   tdm_hwc_window *thwc_window;
   tdm_hwc_window_composition composition_type;
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (e_hwc_window_is_target(hwc_window))
   {
      ERR("HWC-WINS: target window cannot update at e_hwc_window_compsition_update.");
      return EINA_FALSE;
   }

   thwc_window = hwc_window->thwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc_window, EINA_FALSE);

   /* set composition type */
   composition_type = _get_composition_type(hwc_window->state);
   error = tdm_hwc_window_set_composition_type(hwc_window->thwc_window, composition_type);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_buffer_update(E_Hwc_Window *hwc_window)
{
   tbm_surface_h tsurface = NULL;
   tdm_hwc_window *thwc_window;
   tdm_error error;
   Eina_Bool result;

   thwc_window = hwc_window->thwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc_window, EINA_FALSE);

   tsurface = hwc_window->tsurface;
   if (tsurface)
     {
        /* set window info */
        result = _e_hwc_window_info_set(hwc_window, tsurface);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(result != EINA_TRUE, EINA_FALSE);
     }

   /* set buffer */
   error = tdm_hwc_window_set_buffer(thwc_window, hwc_window->tsurface);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_is_target(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   return hwc_window->is_target;
}

EINTERN Eina_Bool
e_hwc_window_is_video(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   return hwc_window->is_video;
}

EINTERN Eina_Bool
e_hwc_window_is_cursor(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   return hwc_window->is_cursor;
}

EINTERN Eina_Bool
e_hwc_window_buffer_fetch(E_Hwc_Window *hwc_window)
{
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);
   if (hwc_window->is_deleted)
     {
        hwc_window->tsurface = NULL;
        hwc_window->update_exist = EINA_TRUE;
        return EINA_TRUE;
     }

   /* set the buffer to be null  */
   if (hwc_window->state == E_HWC_WINDOW_STATE_NONE)
     {
        if (hwc_window->cursor_tsurface)
          {
             tbm_surface_destroy(hwc_window->cursor_tsurface);
             hwc_window->cursor_tsurface = NULL;
          }

        if (hwc_window->tsurface == NULL) return EINA_FALSE;

        hwc_window->update_exist = EINA_TRUE;
        hwc_window->tsurface = NULL;
     }
   else
     {
        /* for video we set buffer in the video module */
        if (e_hwc_window_is_video(hwc_window))
          {
             tsurface = e_comp_wl_video_hwc_widow_surface_get(hwc_window);
             if (!tsurface)
                {
                   ELOGF("HWC-WINS", " ehw:%p no video buffer yet -- {%25s}, state:%s, zpos:%d, deleted:%s (Video)",
                         hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
                         hwc_window, hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
                         e_hwc_window_state_string_get(hwc_window->state),
                         hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
                   return EINA_FALSE;
                }
             hwc_window->update_exist = EINA_TRUE;

             ELOGF("HWC-WINS", " ehw:%p sets ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s (Video)",
                   hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
                   hwc_window, tsurface, hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
                   e_hwc_window_state_string_get(hwc_window->state),
                   hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
          }
        else if (e_hwc_window_is_cursor(hwc_window))
          {
             tsurface = _e_hwc_window_cursor_surface_acquire(hwc_window);
             if (!tsurface)
               {
                  ELOGF("HWC-WINS", " ehw:%10p cursor buffer is null.",
                        hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec, hwc_window);
                  return EINA_FALSE;
               }
            if (!e_comp_object_hwc_update_exists(hwc_window->ec->frame)) return EINA_FALSE;

            e_comp_object_hwc_update_set(hwc_window->ec->frame, EINA_FALSE);
            hwc_window->update_exist = EINA_TRUE;

            ELOGF("HWC-WINS", " ehw:%p sets ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s (Cusor)",
                  hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
                  hwc_window, tsurface, hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
                  e_hwc_window_state_string_get(hwc_window->state),
                  hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
          }
        else
          {
             /* acquire the surface */
             tsurface = _e_hwc_window_client_surface_acquire(hwc_window);
             if (!tsurface)
               {
                  ELOGF("HWC-WINS", " ehw:%p client buffer is null.",
                        hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec, hwc_window);
                  return EINA_FALSE;
               }
             if (tsurface == hwc_window->tsurface) return EINA_FALSE;

             hwc_window->update_exist = EINA_TRUE;

             ELOGF("HWC-WINS", " ehw:%p sets ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s (Window)",
                   hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
                   hwc_window, tsurface, hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
                   e_hwc_window_state_string_get(hwc_window->state),
                   hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
          }
     }

   /* exist tsurface for update hwc_window */
   hwc_window->tsurface = tsurface;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_commit_data_aquire(E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_Commit_Data *commit_data = NULL;


   if (hwc_window->update_exist == EINA_FALSE) return EINA_FALSE;

   commit_data = E_NEW(E_Hwc_Window_Commit_Data, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, EINA_FALSE);

   /* fake commit data for free the e_hwc_window resource */
   if (hwc_window->is_deleted) goto done;

   if (e_hwc_window_is_target(hwc_window) ||
       e_hwc_window_is_video(hwc_window))
     {
        commit_data->tsurface = hwc_window->tsurface;
        tbm_surface_internal_ref(commit_data->tsurface);
     }
   else
     {
        commit_data->tsurface = hwc_window->tsurface;
        tbm_surface_internal_ref(commit_data->tsurface);

        e_comp_wl_buffer_reference(&commit_data->buffer_ref, _e_hwc_window_comp_wl_buffer_get(hwc_window));
     }

done:
   hwc_window->commit_data = commit_data;
   hwc_window->update_exist = EINA_FALSE;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_commit_data_release(E_Hwc_Window *hwc_window)
{
   tbm_surface_h tsurface = NULL;
   tbm_surface_h displaying_surface;

   /* we don't have data to release */
   if (!hwc_window->commit_data) return EINA_FALSE;

   if (hwc_window->is_deleted)
     {
        displaying_surface = e_hwc_window_displaying_surface_get(hwc_window);
        if (displaying_surface) tbm_surface_internal_unref(displaying_surface);
        hwc_window->display_info.tsurface = NULL;

        free(hwc_window->commit_data);
        e_hwc_window_free(hwc_window);
     }
   else
     {
        tsurface = hwc_window->commit_data->tsurface;

        if (!tsurface)
          {
             e_comp_wl_buffer_reference(&hwc_window->display_info.buffer_ref, NULL);
          }
        else if (e_hwc_window_is_target(hwc_window) || e_hwc_window_is_video(hwc_window))
          {
             e_comp_wl_buffer_reference(&hwc_window->display_info.buffer_ref, NULL);
          }
        else
          {
             e_comp_wl_buffer_reference(&hwc_window->display_info.buffer_ref, hwc_window->commit_data->buffer_ref.buffer);
          }

        e_comp_wl_buffer_reference(&hwc_window->commit_data->buffer_ref, NULL);

        /* release and unreference the previous surface */
        displaying_surface = e_hwc_window_displaying_surface_get(hwc_window);
        if (displaying_surface)
          {
             if (e_hwc_window_is_target(hwc_window))
               {
                  _e_hwc_window_target_window_surface_release((E_Hwc_Window_Target *)hwc_window, displaying_surface);
               }
             tbm_surface_internal_unref(displaying_surface);
          }

        /* update hwc_window display info */
        hwc_window->display_info.tsurface = tsurface;

        free(hwc_window->commit_data);
        hwc_window->commit_data = NULL;
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_target_surface_queue_can_dequeue(E_Hwc_Window_Target *target_hwc_window)
{
   tbm_surface_queue_h tqueue = NULL;
   int num_free = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   tqueue = target_hwc_window->queue;
   if (!tqueue)
     {
        WRN("tbm_surface_queue is NULL");
        return EINA_FALSE;
     }

   num_free = tbm_surface_queue_can_dequeue(tqueue, 0);

   if (num_free <= 0) return EINA_FALSE;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_target_enabled(E_Hwc_Window_Target *target_hwc_window)
{
   E_Hwc_Window *hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   hwc_window = (E_Hwc_Window *)target_hwc_window;

   if (hwc_window->state != E_HWC_WINDOW_STATE_DEVICE)
      return EINA_FALSE;

   return EINA_TRUE;
}

/* set the tsurface to the target_window->tsurface according to the state.
 *  1. try to set the tsurface to the target_window at E_HWC_WINDOW_STATE_DEVICE.
 *  2. try to set NULL and release(clear) tsurface_queue of the target_window at E_HWC_WINDOW_STATE_NONE.
 *  Returing EINA_FALSE means that there is no update for the target_window->tsurface.
 **/
EINTERN Eina_Bool
e_hwc_window_target_buffer_fetch(E_Hwc_Window_Target *target_hwc_window)
{
   E_Hwc *hwc;
   E_Hwc_Window *hwc_window, *hw;
   tdm_hwc *thwc;
   tbm_surface_h tsurface;
   tdm_region fb_damage;
   Eina_List *ee_rendered_hw_list = NULL;
   uint32_t n_thw = 0;
   const Eina_List *l;
   int i;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   hwc_window = (E_Hwc_Window *)target_hwc_window;

   hwc_window->update_exist = EINA_FALSE;

   if (hwc_window->state == E_HWC_WINDOW_STATE_DEVICE)
     {
        /* acquire the surface */
        tsurface = _e_hwc_window_target_window_surface_acquire((E_Hwc_Window_Target *)hwc_window);
        if (!tsurface)
          {
             //ELOGF("HWC-WINS", " the surface of target is null.(E_HWC_WINDOW_STATE_DEVICE)", NULL, NULL);
             return EINA_FALSE;
          }
        if (tsurface == hwc_window->tsurface)
          {
             //ELOGF("HWC-WINS", " aquired buffer is same.(ts:%p)(E_HWC_WINDOW_STATE_DEVICE)", NULL, NULL, hwc_window->tsurface);
             return EINA_FALSE;
          }

        hwc_window->tsurface = tsurface;

        hwc = hwc_window->hwc;
        EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

        thwc = hwc->thwc;
        EINA_SAFETY_ON_NULL_RETURN_VAL(thwc, EINA_FALSE);

        /* the damage isn't supported by hwc extension yet */
        memset(&fb_damage, 0, sizeof(fb_damage));

        tdm_hwc_set_client_target_buffer(thwc, hwc_window->tsurface, fb_damage);

        ee_rendered_hw_list = e_hwc_window_target_window_ee_rendered_hw_list_get(target_hwc_window);
        n_thw = eina_list_count(ee_rendered_hw_list);
        if (n_thw)
          {
             ELOGF("HWC-WINS", " ehw:%p sets ts:%10p ------- {%25s}, state:%s",
                   NULL, NULL, hwc_window, hwc_window->tsurface, "@TARGET WINDOW@",
                   e_hwc_window_state_string_get(hwc_window->state));

             i = 0;
             EINA_LIST_FOREACH(ee_rendered_hw_list, l, hw)
               {
                  ELOGF("HWC-WINS", "  (%d) with ehw:%p, ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s",
                        hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
                        i++, hw, hw->tsurface, hw->ec ? hw->ec->icccm.title : "UNKNOWN",
                        e_hwc_window_state_string_get(hw->state),
                        hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
               }
          }
        else
          ELOGF("HWC-WINS", " ehw:%p sets ts:%10p ------- {%25s}, state:%s, zpos:%d no hwc_windows to render.",
                NULL, NULL, hwc_window, hwc_window->tsurface, "@TARGET WINDOW@",
                e_hwc_window_state_string_get(hwc_window->state), hwc_window->zpos);
     }
   else
     {
        if (hwc_window->tsurface == NULL)
          {
             //ELOGF("HWC-WINS", " the surface of target is null.(E_HWC_WINDOW_STATE_NONE)", NULL, NULL);
             return EINA_FALSE;
          }

        if (hwc_window->tsurface)
          _e_hwc_window_target_window_surface_release((E_Hwc_Window_Target *)hwc_window, hwc_window->tsurface);

        if (!_e_hwc_window_target_window_clear((E_Hwc_Window_Target *)hwc_window))
          ERR("fail to _e_hwc_window_target_window_clear");

        hwc_window->tsurface = NULL;

        //TODO: Do we set the target_buffer to be NULL?

        ELOGF("HWC-WINS", " ehw:%p sets ts:%10p ------- {%25s}, state:%s",
              NULL, NULL, hwc_window, hwc_window->tsurface, "@TARGET WINDOW@",
              e_hwc_window_state_string_get(hwc_window->state));
     }

   hwc_window->update_exist = EINA_TRUE;

   return EINA_TRUE;
}

EINTERN Eina_List *
e_hwc_window_target_window_ee_rendered_hw_list_get(E_Hwc_Window_Target *target_window)
{
   Eina_List *ee_rendered_hw_list = NULL, *new_list = NULL;
   E_Hwc_Window *hw1, *hw2;
   const Eina_List *l, *ll;
   E_Hwc *hwc;
   tbm_surface_h target_tsurface;

   hwc = target_window->hwc_window.hwc;

   target_tsurface = target_window->hwc_window.tsurface;
   tbm_surface_internal_get_user_data(target_tsurface, ee_rendered_hw_list_key, (void**)&ee_rendered_hw_list);

   /* refresh list of composited e_thwc_windows according to existed ones */
   EINA_LIST_FOREACH(ee_rendered_hw_list, l, hw1)
      EINA_LIST_FOREACH(hwc->hwc_windows, ll, hw2)
         if (hw1 == hw2) new_list = eina_list_append(new_list, hw1);

   return new_list;
}

EINTERN Eina_Bool
e_hwc_window_activate(E_Hwc_Window *hwc_window)
{
   struct wayland_tbm_client_queue *cqueue = NULL;
   E_Client *ec;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->activation_state == E_HWC_WINDOW_ACTIVATION_STATE_ACTIVATED)
     return EINA_TRUE;

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   /* set the redirected to FALSE */
   e_client_redirected_set(ec, EINA_FALSE);

   cqueue = _get_wayland_tbm_client_queue(ec);

   if (cqueue)
     wayland_tbm_server_client_queue_activate(cqueue, 0, 0, 0);

   if (e_hwc_window_is_cursor(hwc_window))
     {
        E_Pointer *pointer = NULL;

        pointer = e_pointer_get(ec);
        EINA_SAFETY_ON_NULL_RETURN_VAL(pointer, EINA_FALSE);

        e_pointer_hwc_set(pointer, EINA_TRUE);
     }

   hwc_window->activation_state = E_HWC_WINDOW_ACTIVATION_STATE_ACTIVATED;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_deactivate(E_Hwc_Window *hwc_window)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   E_Client *ec = NULL;
   int transform;
   E_Output *output = NULL;
   E_Hwc *hwc = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->activation_state == E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED)
     return EINA_TRUE;

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   /* set the redirected to TRUE */
   e_client_redirected_set(ec, EINA_TRUE);

   cqueue = _get_wayland_tbm_client_queue(ec);
   if (cqueue)
     /* TODO: do we have to immediately inform a wayland client
      *       that an e_client got redirected or wait till it's being composited
      *       on the fb_target and a hw overlay owned by it gets free? */
     wayland_tbm_server_client_queue_deactivate(cqueue);

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   output = hwc->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   transform = e_comp_wl_output_buffer_transform_get(ec);

   if (output->config.rotation != 0 && (output->config.rotation / 90) == transform)
      e_comp_screen_rotation_ignore_output_transform_send(ec, EINA_TRUE);

   _e_hwc_window_client_recover(hwc_window);

   if (e_hwc_window_is_cursor(hwc_window))
     {
        E_Pointer *pointer = NULL;

        pointer = e_pointer_get(ec);
        if (pointer)
          e_pointer_hwc_set(pointer, EINA_FALSE);
     }

   hwc_window->activation_state = E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_is_on_hw_overlay(E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_State state = E_HWC_WINDOW_STATE_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   state = e_hwc_window_state_get(hwc_window);

   if (state == E_HWC_WINDOW_STATE_DEVICE) return EINA_TRUE;
   if (state == E_HWC_WINDOW_STATE_CURSOR) return EINA_TRUE;
   if (state == E_HWC_WINDOW_STATE_VIDEO) return EINA_TRUE;

   return EINA_FALSE;
}

EINTERN tbm_surface_h
e_hwc_window_displaying_surface_get(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, NULL);

   return hwc_window->display_info.tsurface;
}

EINTERN Eina_Bool
e_hwc_window_accepted_state_set(E_Hwc_Window *hwc_window, E_Hwc_Window_State state)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->accepted_state == state) return EINA_TRUE;

   hwc_window->accepted_state = state;

   EHWTRACE("Set Accepted state:%s -- {%s}",
            hwc_window->ec, hwc_window, e_hwc_window_state_string_get(state),
            hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN");

   _e_hwc_window_hook_call(E_HWC_WINDOW_HOOK_ACCEPTED_STATE_CHANGE, hwc_window);

   return EINA_TRUE;
}

EINTERN E_Hwc_Window_State
e_hwc_window_accepted_state_get(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, E_HWC_WINDOW_STATE_NONE);

   return hwc_window->accepted_state;
}

EINTERN Eina_Bool
e_hwc_window_state_set(E_Hwc_Window *hwc_window, E_Hwc_Window_State state)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->state != state) hwc_window->state = state;

   /* zpos is -999 at state none */
   if (state == E_HWC_WINDOW_STATE_NONE)
     e_hwc_window_zpos_set(hwc_window, -999);

   return EINA_TRUE;
}

EINTERN E_Hwc_Window_State
e_hwc_window_state_get(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, E_HWC_WINDOW_STATE_NONE);

   return hwc_window->state;
}

// add hwc_window to the render_list
EINTERN void
e_hwc_window_render_list_add(E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_Target *target_hwc_window;
   E_Client *ec;

   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN(ec);

   target_hwc_window = _e_hwc_window_target_window_get(hwc_window);
   EINA_SAFETY_ON_NULL_RETURN(target_hwc_window);

   target_hwc_window->ee_rendered_hw_list =
           eina_list_append(target_hwc_window->ee_rendered_hw_list, hwc_window);

   ELOGF("HWC-WINS", " added the render_list -- ehw:%p -- {%25s}.", ec->pixmap, ec, hwc_window, ec->icccm.title);
}

EINTERN Eina_Bool
e_hwc_window_is_on_target_window(E_Hwc_Window *hwc_window)
{
   Eina_List *ee_rendered_hw_list = NULL;
   E_Hwc_Window_Target *target_hwc_window;
   E_Hwc_Window *target_window;
   E_Hwc_Window *hw;
   const Eina_List *l;
   tbm_surface_h target_tsurface;

   target_hwc_window = _e_hwc_window_target_window_get(hwc_window);
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   target_window = (E_Hwc_Window *)target_hwc_window;
   if (e_hwc_window_state_get(target_window) != E_HWC_WINDOW_STATE_DEVICE) return EINA_FALSE;

   target_tsurface = target_hwc_window->hwc_window.tsurface;

   tbm_surface_internal_get_user_data(target_tsurface, ee_rendered_hw_list_key, (void**)&ee_rendered_hw_list);

   EINA_LIST_FOREACH(ee_rendered_hw_list, l, hw)
     if (hw == hwc_window) return EINA_TRUE;

   return EINA_FALSE;
}


EINTERN const char*
e_hwc_window_state_string_get(E_Hwc_Window_State hwc_window_state)
{
    switch (hwc_window_state)
    {
     case E_HWC_WINDOW_STATE_NONE:
       return "NO"; // None
     case E_HWC_WINDOW_STATE_CLIENT:
       return "CL"; // Client
     case E_HWC_WINDOW_STATE_DEVICE:
       return "DV"; // Deivce
     case E_HWC_WINDOW_STATE_VIDEO:
       return "VD"; // Video
     case E_HWC_WINDOW_STATE_DEVICE_CANDIDATE:
       return "DC"; // Deivce Candidate
     case E_HWC_WINDOW_STATE_CURSOR:
       return "CS"; // Cursor
     default:
       return "UNKNOWN";
    }
}

E_API E_Hwc_Window_Hook *
e_hwc_window_hook_add(E_Hwc_Window_Hook_Point hookpoint, E_Hwc_Window_Hook_Cb func, const void *data)
{
   E_Hwc_Window_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_HWC_WINDOW_HOOK_LAST, NULL);
   ch = E_NEW(E_Hwc_Window_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_hwc_window_hooks[hookpoint] = eina_inlist_append(_e_hwc_window_hooks[hookpoint],
                                                       EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_hwc_window_hook_del(E_Hwc_Window_Hook *ch)
{
   ch->delete_me = 1;
   if (_e_hwc_window_hooks_walking == 0)
     {
        _e_hwc_window_hooks[ch->hookpoint] = eina_inlist_remove(_e_hwc_window_hooks[ch->hookpoint],
                                                                EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_hwc_window_hooks_delete++;
}
