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

static E_Client_Hook *client_hook_new = NULL;
static E_Client_Hook *client_hook_del = NULL;
static Ecore_Event_Handler *zone_set_event_handler = NULL;
static uint64_t ee_rendered_hw_list_key;

static E_Comp_Wl_Buffer *
_e_hwc_window_comp_wl_buffer_get(E_Hwc_Window *hwc_window)
{
   E_Client *ec = hwc_window->ec;
   E_Comp_Wl_Client_Data *cdata = NULL;
   E_Comp_Wl_Buffer_Ref *buffer_ref;

   if (!ec) return NULL;

   cdata = ec->comp_data;
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
        EHWINF("has no wl_tbm_server_client_queue. -- {%25s}, state:%s, zpos:%d, deleted:%s",
               ec, ec->hwc_window,
               ec->icccm.title, e_hwc_window_state_string_get(ec->hwc_window->state),
               ec->hwc_window->zpos, ec->hwc_window->is_deleted ? "yes" : "no");
     }

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
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, NULL);

   tqueue = target_hwc_window->queue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, NULL);

   if (tbm_surface_queue_can_acquire(tqueue, 0))
     {
        tsq_err = tbm_surface_queue_acquire(tqueue, &tsurface);
        if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
          {
             ERR("Failed to acquire tbm_surface from tbm_surface_queue(%p): tsq_err = %d", tqueue, tsq_err);
             return NULL;
          }

        EHWINF("ACQ ts:%p tq:%p -- {%s}.",
               NULL, target_hwc_window, tsurface, tqueue, "@TARGET WINDOW@");
     }
   else
     {
        return NULL;
     }

   return tsurface;
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

   EHWINF("REL ts:%p tq:%p -- {%s}.",
          NULL, target_hwc_window, tsurface, tqueue, "@TARGET WINDOW@");

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
   E_Hwc_Window *hwc_window = NULL;

   EINA_LIST_FREE(ee_rendered_hw_list, hwc_window)
     e_object_unref(E_OBJECT(hwc_window));
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
        EHWINF("gets dequeue noti ts:%p -- {%s}.",
               NULL, target_hwc_window, tsurface, "@TARGET WINDOW@");

        tbm_surface_internal_add_user_data(tsurface,
                                           ee_rendered_hw_list_key,
                                           _e_hwc_window_target_window_surface_data_free);
        target_hwc_window->dequeued_tsurface = tsurface;
        target_hwc_window->rendered_tsurface_list = eina_list_append(target_hwc_window->rendered_tsurface_list,
                                                                     tsurface);
     }
   /* tsurface has been released at the queue */
   if (trace == TBM_SURFACE_QUEUE_TRACE_RELEASE)
     {
        tbm_surface_internal_delete_user_data(tsurface, ee_rendered_hw_list_key);
        target_hwc_window->rendered_tsurface_list = eina_list_remove(target_hwc_window->rendered_tsurface_list,
                                                                     tsurface);
     }
}

/* gets called as evas_renderer enqueues a new buffer into the queue */
static void
_e_hwc_window_target_window_surface_queue_acquirable_cb(tbm_surface_queue_h surface_queue, void *data)
{
    E_Hwc_Window_Target *target_hwc_window = (E_Hwc_Window_Target *)data;
    uint64_t value = 1;
    int ret;

    ret = write(target_hwc_window->event_fd, &value, sizeof(value));
    if (ret == -1)
      ERR("failed to send acquirable event:%m");
}

/* gets called at the beginning of an ecore_main_loop iteration */
static Eina_Bool
_e_hwc_window_target_window_render_finished_cb(void *data, Ecore_Fd_Handler *fd_handler)
{
   int len;
   int fd;
   char buffer[64];

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
   E_Hwc_Window *hwc_window = NULL;
   Eina_List *ee_rendered_hw_list;

   EHWINF("gets render_flush_post noti ------ {@TARGET WINDOW@}", NULL, target_hwc_window);

   if (!target_hwc_window->dequeued_tsurface)
     {
        WRN("flush_post_cb is called but tsurface isn't dequeued");

        EINA_LIST_FREE(target_hwc_window->ee_rendered_hw_list, hwc_window)
          e_object_unref(E_OBJECT(hwc_window));

        target_hwc_window->ee_rendered_hw_list = NULL;
        return;
     }

   /* all ecs have been composited so we can attach a list of composited e_hwc_windows to the surface
    * which contains their ecs composited */
   ee_rendered_hw_list = eina_list_clone(target_hwc_window->ee_rendered_hw_list);

   tbm_surface_internal_set_user_data(target_hwc_window->dequeued_tsurface,
           ee_rendered_hw_list_key, ee_rendered_hw_list);

   eina_list_free(target_hwc_window->ee_rendered_hw_list);
   target_hwc_window->ee_rendered_hw_list = NULL;
   target_hwc_window->dequeued_tsurface = NULL;
}

static void
_e_hwc_window_target_free(E_Hwc_Window_Target *target_hwc_window)
{
   evas_event_callback_del(target_hwc_window->evas,
                           EVAS_CALLBACK_RENDER_FLUSH_POST,
                           _e_hwc_window_target_window_render_flush_post_cb);

   ecore_main_fd_handler_del(target_hwc_window->event_hdlr);
   close(target_hwc_window->event_fd);

   if (target_hwc_window->queue)
     tbm_surface_queue_destroy(target_hwc_window->queue);

   EHWINF("Free target window", NULL, target_hwc_window);

   E_FREE(target_hwc_window);
}

static E_Hwc_Window_Target *
_e_hwc_window_target_new(E_Hwc *hwc)
{
   const char *name = NULL;
   E_Hwc_Window_Target *target_hwc_window = NULL;
   Evas *evas = NULL;

   name = ecore_evas_engine_name_get(e_comp->ee);
   EINA_SAFETY_ON_NULL_RETURN_VAL(name, NULL);

   evas = ecore_evas_get(e_comp->ee);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evas, NULL);

   if(!strcmp("gl_drm_tbm", name) ||
      !strcmp("drm_tbm", name) ||
      !strcmp("gl_tbm", name) ||
      !strcmp("software_tbm", name))
     {
        ecore_evas_manual_render_set(e_comp->ee, 1);
     }

   target_hwc_window = E_OBJECT_ALLOC(E_Hwc_Window_Target, E_HWC_WINDOW_TYPE, _e_hwc_window_target_free);
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

   evas_event_callback_add(evas, EVAS_CALLBACK_RENDER_FLUSH_POST, _e_hwc_window_target_window_render_flush_post_cb, target_hwc_window);

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
   E_Hwc_Window *hwc_window = (E_Hwc_Window *)target_hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   tqueue = target_hwc_window->queue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   if (hwc_window->tsurface &&
       hwc_window->tsurface != hwc_window->display_info.tsurface)
     _e_hwc_window_target_window_surface_release(target_hwc_window, hwc_window->tsurface);

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
        EHWINF("Try to create hwc_window, but it couldn't.(no zone)", ec, NULL);
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

   EHWINF("set on eout:%p, zone_id:%d.",
          ec, hwc_window, output, zone->id);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static tbm_surface_h
_e_hwc_window_client_surface_acquire(E_Hwc_Window *hwc_window)
{
   E_Comp_Wl_Buffer *buffer = _e_hwc_window_comp_wl_buffer_get(hwc_window);
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   tbm_surface_h tsurface = NULL;

   if (!buffer) return NULL;

   switch (buffer->type)
     {
       case E_COMP_WL_BUFFER_TYPE_NATIVE:
       case E_COMP_WL_BUFFER_TYPE_VIDEO:
         tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
         break;
       case E_COMP_WL_BUFFER_TYPE_TBM:
         tsurface = buffer->tbm_surface;
         break;
       default:
         ERR("not supported buffer type:%d", buffer->type);
         break;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, NULL);

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

        buffer = e_comp_wl_tbm_buffer_get(tsurface);
        if (!buffer) return;
     }

   /* force update */
   e_comp_wl_surface_attach(ec, buffer);

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
       (hwc_window->cursor_info.tsurface) &&
       (hwc_window->cursor_info.rotation == pointer->rotation))
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

   hwc_window->cursor_info.rotation = pointer->rotation;

   w = (output->cursor_available.min_w > tw) ? output->cursor_available.min_w : tw;
   h = (output->cursor_available.min_h > th) ? output->cursor_available.min_h : th;

   if (e_comp->hwc_reuse_cursor_buffer)
     {
        if (hwc_window->cursor_info.tsurface)
          {
             tsurface_w = tbm_surface_get_width(hwc_window->cursor_info.tsurface);
             tsurface_h = tbm_surface_get_height(hwc_window->cursor_info.tsurface);

             if (w != tsurface_w || h != tsurface_h)
               {
                  tbm_surface_destroy(hwc_window->cursor_info.tsurface);
                  hwc_window->cursor_info.tsurface = NULL;
               }
          }
     }
   else
     {
        if (hwc_window->cursor_info.tsurface)
          {
             tbm_surface_destroy(hwc_window->cursor_info.tsurface);
             hwc_window->cursor_info.tsurface = NULL;
          }
     }

   if (!hwc_window->cursor_info.tsurface)
     {
        /* Which tbm flags should be used? */
        tsurface = tbm_surface_internal_create_with_flags(w, h, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
        EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, EINA_FALSE);
     }
   else
     tsurface = hwc_window->cursor_info.tsurface;

   ret = tbm_surface_map(tsurface, TBM_SURF_OPTION_WRITE, &tsurface_info);
   if (ret != TBM_SURFACE_ERROR_NONE)
     {
        ERR("Failed to map tsurface");
        tbm_surface_destroy(tsurface);
        return EINA_FALSE;
     }

   _e_hwc_window_cursor_image_draw(buffer, &tsurface_info, pointer->rotation);

   tbm_surface_unmap(tsurface);

   hwc_window->cursor_info.tsurface = tsurface;

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
   if (!pointer) return NULL;

   buffer = ec->comp_data->buffer_ref.buffer;
   if (!buffer) return NULL;

   if (!_e_hwc_window_cursor_surface_refresh(hwc_window, pointer))
     {
        ERR("Failed to _e_hwc_window_cursor_surface_refresh");
        return NULL;
     }

   tsurface = hwc_window->cursor_info.tsurface;

   return tsurface;
}

static void
_e_hwc_window_cursor_position_get(E_Pointer *ptr, int width, int height, unsigned int *x, unsigned int *y)
{
   int rotation;;

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
}

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

static void
_e_hwc_window_free(E_Hwc_Window *hwc_window)
{
   E_Hwc *hwc = NULL;
   E_Output *output = NULL;
   tdm_output *toutput = NULL;

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_GOTO(hwc, done);

   hwc->hwc_windows = eina_list_remove(hwc->hwc_windows, hwc_window);

   output = hwc->output;
   EINA_SAFETY_ON_NULL_GOTO(hwc->output, done);

   toutput = output->toutput;
   EINA_SAFETY_ON_NULL_GOTO(toutput, done);

   if (hwc_window->thwc_window)
     tdm_hwc_window_destroy(hwc_window->thwc_window);

   EHWINF("Free", NULL, hwc_window);

done:
   E_FREE(hwc_window);
}

EINTERN void
e_hwc_window_free(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   EHWINF("Del", hwc_window->ec, hwc_window);

   hwc_window->ec = NULL;
   hwc_window->is_deleted = EINA_TRUE;
   e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_NONE);

   e_object_del(E_OBJECT(hwc_window));
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

   hwc_window = E_OBJECT_ALLOC(E_Hwc_Window, E_HWC_WINDOW_TYPE, _e_hwc_window_free);
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

   EHWINF("is created on eout:%p, zone_id:%d",
          hwc_window->ec, hwc_window, hwc->output, ec->zone->id);
   return hwc_window;
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
e_hwc_window_info_update(E_Hwc_Window *hwc_window)
{
   E_Hwc *hwc = NULL;
   E_Output *output = NULL;
   E_Client *ec = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_info_s surf_info = {0};
   tdm_hwc_window_info hwc_win_info = {0};

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->is_deleted) return EINA_FALSE;

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   output = hwc->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   tsurface = hwc_window->tsurface;

   if (e_hwc_window_is_video(hwc_window))
     {
        if (!e_comp_wl_video_hwc_window_info_get(hwc_window, &hwc_win_info))
          {
             ERR("Video window does not get the hwc_win_info.");
             return EINA_FALSE;
          }
     }
   else if (tsurface)
     {
        /* set hwc_window when the layer infomation is different from the previous one */
        tbm_surface_get_info(tsurface, &surf_info);

        hwc_win_info.src_config.format = surf_info.format;
        hwc_win_info.src_config.pos.x = 0;
        hwc_win_info.src_config.pos.y = 0;
        hwc_win_info.src_config.pos.w = surf_info.width;
        hwc_win_info.src_config.pos.h = surf_info.height;

        hwc_win_info.src_config.size.h = _get_aligned_width(tsurface);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(hwc_win_info.src_config.size.h == 0, EINA_FALSE);
        hwc_win_info.src_config.size.v = surf_info.height;

        if (e_hwc_window_is_cursor(hwc_window))
          {
              E_Pointer *pointer = e_pointer_get(ec);
              if (!pointer) return EINA_FALSE;

              _e_hwc_window_cursor_position_get(pointer,
                                                hwc_win_info.src_config.pos.w,
                                                hwc_win_info.src_config.pos.h,
                                                &hwc_win_info.dst_pos.x,
                                                &hwc_win_info.dst_pos.y);
          }
        else
          {
              hwc_win_info.dst_pos.x = ec->x;
              hwc_win_info.dst_pos.y = ec->y;
          }

        /* if output is transformed, the position of a buffer on screen should be also
        * transformed.
        */
        if (output->config.rotation > 0)
          {
              int bw, bh;
              int dst_x, dst_y;
              e_pixmap_size_get(ec->pixmap, &bw, &bh);
              e_comp_wl_rect_convert(ec->zone->w, ec->zone->h,
                                    output->config.rotation / 90, 1,
                                    hwc_win_info.dst_pos.x, hwc_win_info.dst_pos.y,
                                    bw, bh,
                                    &dst_x, &dst_y,
                                    NULL, NULL);

              hwc_win_info.dst_pos.x = dst_x;
              hwc_win_info.dst_pos.y = dst_y;
          }

        hwc_win_info.dst_pos.w = surf_info.width;
        hwc_win_info.dst_pos.h = surf_info.height;
     }

   if (memcmp(&hwc_window->info, &hwc_win_info, sizeof(tdm_hwc_window_info)))
     {
        tdm_error error;

        memcpy(&hwc_window->info, &hwc_win_info, sizeof(tdm_hwc_window_info));
        error = tdm_hwc_window_set_info(hwc_window->thwc_window, &hwc_window->info);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

        return EINA_TRUE;
     }

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_hwc_window_buffer_fetch(E_Hwc_Window *hwc_window)
{
   tbm_surface_h tsurface = NULL;
   tdm_hwc_window *thwc_window = NULL;
   tdm_error error = TDM_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   thwc_window = hwc_window->thwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc_window, EINA_FALSE);

   /* for video we set buffer in the video module */
   if (e_hwc_window_is_video(hwc_window))
     {
        tsurface = e_comp_wl_video_hwc_widow_surface_get(hwc_window);
        if (!tsurface)
          {
              EHWINF("no video buffer yet -- {%25s}, state:%s, zpos:%d, deleted:%s (Video)",
                    hwc_window->ec, hwc_window,
                    hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
                    e_hwc_window_state_string_get(hwc_window->state),
                    hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
              return EINA_FALSE;
          }

        if (tsurface == hwc_window->tsurface) return EINA_FALSE;

        EHWINF("FET ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s (Video)",
              hwc_window->ec, hwc_window,
              tsurface, hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
              e_hwc_window_state_string_get(hwc_window->state),
              hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
     }
   else if (e_hwc_window_is_cursor(hwc_window))
     {
        tsurface = _e_hwc_window_cursor_surface_acquire(hwc_window);
        if (!tsurface) return EINA_FALSE;

        if (!e_comp_object_hwc_update_exists(hwc_window->ec->frame)) return EINA_FALSE;

        e_comp_object_hwc_update_set(hwc_window->ec->frame, EINA_FALSE);

        EHWINF("FET ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s (Cusor)",
               hwc_window->ec, hwc_window,
               tsurface, hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
               e_hwc_window_state_string_get(hwc_window->state),
               hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
     }
   else
     {
        /* acquire the surface */
        tsurface = _e_hwc_window_client_surface_acquire(hwc_window);
        if (!tsurface) return EINA_FALSE;

        if (tsurface == hwc_window->tsurface) return EINA_FALSE;

        EHWINF("FET ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s (Window)",
               hwc_window->ec, hwc_window,
               tsurface, hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
               e_hwc_window_state_string_get(hwc_window->state),
               hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
     }

   /* exist tsurface for update hwc_window */
   hwc_window->tsurface = tsurface;

   error = tdm_hwc_window_set_buffer(thwc_window, hwc_window->tsurface);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_commit_data_acquire(E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_Commit_Data *commit_data = NULL;

   commit_data = E_NEW(E_Hwc_Window_Commit_Data, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, EINA_FALSE);

   if ((hwc_window->accepted_state == E_HWC_WINDOW_STATE_DEVICE) ||
       (hwc_window->accepted_state == E_HWC_WINDOW_STATE_CURSOR))
     {
        if (!hwc_window->tsurface) return EINA_FALSE;
        if (hwc_window->tsurface == hwc_window->display_info.tsurface) return EINA_FALSE;

        commit_data->tsurface = hwc_window->tsurface;
        tbm_surface_internal_ref(commit_data->tsurface);

        if (!e_hwc_window_is_target(hwc_window) &&
            !e_hwc_window_is_video(hwc_window))
          e_comp_wl_buffer_reference(&commit_data->buffer_ref,
                                      _e_hwc_window_comp_wl_buffer_get(hwc_window));
     }
   else
     {
        if (!hwc_window->display_info.tsurface) return EINA_FALSE;
        commit_data->tsurface = NULL;
     }

   if (e_hwc_window_is_target(hwc_window))
     {
        EHWINF("COM ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s",
               hwc_window->ec, hwc_window,
               commit_data->tsurface, "@TARGET WINDOW@",
               e_hwc_window_state_string_get(hwc_window->state),
               hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
     }
   else
     {
        EHWINF("COM ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s",
               hwc_window->ec, hwc_window,
               commit_data->tsurface, hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
               e_hwc_window_state_string_get(hwc_window->state),
               hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
     }

   e_object_ref(E_OBJECT(hwc_window));

   hwc_window->commit_data = commit_data;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_commit_data_release(E_Hwc_Window *hwc_window)
{
   tbm_surface_h tsurface = NULL;

   /* we don't have data to release */
   if (!hwc_window->commit_data) return EINA_FALSE;

   tsurface = hwc_window->commit_data->tsurface;

   EHWINF("DON ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s (Window)",
          hwc_window->ec, hwc_window,
          tsurface, hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
          e_hwc_window_state_string_get(hwc_window->state),
          hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");

   if (!tsurface)
     {
        if (hwc_window->display_info.tsurface)
          {
             if (e_hwc_window_is_target(hwc_window))
               _e_hwc_window_target_window_surface_release((E_Hwc_Window_Target *)hwc_window,
                                                           hwc_window->display_info.tsurface);

             tbm_surface_internal_unref(hwc_window->display_info.tsurface);
             e_object_unref(E_OBJECT(hwc_window));
          }

        e_comp_wl_buffer_reference(&hwc_window->display_info.buffer_ref, NULL);
        hwc_window->display_info.tsurface = NULL;
     }
   else
     {
        if (hwc_window->commit_data->buffer_ref.buffer)
          e_comp_wl_buffer_reference(&hwc_window->display_info.buffer_ref,
                                     hwc_window->commit_data->buffer_ref.buffer);

        /* release and unreference the previous surface */
        if (hwc_window->display_info.tsurface)
          {
             if (e_hwc_window_is_target(hwc_window))
               _e_hwc_window_target_window_surface_release((E_Hwc_Window_Target *)hwc_window,
                                                           hwc_window->display_info.tsurface);

             tbm_surface_internal_unref(hwc_window->display_info.tsurface);
             e_object_unref(E_OBJECT(hwc_window));
          }

        /* update hwc_window display info */
        hwc_window->display_info.tsurface = tsurface;
        e_object_ref(E_OBJECT(hwc_window));
     }

   e_comp_wl_buffer_reference(&hwc_window->commit_data->buffer_ref, NULL);
   free(hwc_window->commit_data);
   hwc_window->commit_data = NULL;
   e_object_unref(E_OBJECT(hwc_window));

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_target_surface_queue_can_dequeue(E_Hwc_Window_Target *target_hwc_window)
{
   tbm_surface_queue_h tqueue = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   tqueue = target_hwc_window->queue;
   if (!tqueue)
     {
        WRN("tbm_surface_queue is NULL");
        return EINA_FALSE;
     }

   if (tbm_surface_queue_can_dequeue(tqueue, 0))
     return EINA_TRUE;

   return EINA_FALSE;
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

EINTERN Eina_Bool
e_hwc_window_target_buffer_skip(E_Hwc_Window_Target *target_hwc_window)
{
   E_Hwc_Window *hwc_window = NULL;
   E_Hwc *hwc = NULL;
   tdm_hwc *thwc = NULL;
   tdm_region fb_damage;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   hwc_window = (E_Hwc_Window *)target_hwc_window;

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   thwc = hwc->thwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc, EINA_FALSE);

   CLEAR(fb_damage);

   if (hwc_window->tsurface &&
       hwc_window->tsurface != hwc_window->display_info.tsurface)
     {
        _e_hwc_window_target_window_surface_release(target_hwc_window, hwc_window->tsurface);
        hwc_window->tsurface = hwc_window->display_info.tsurface;
        tdm_hwc_set_client_target_buffer(thwc, hwc_window->display_info.tsurface, fb_damage);
     }

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

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   thwc = hwc->thwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc, EINA_FALSE);

   if (hwc_window->state == E_HWC_WINDOW_STATE_DEVICE)
     {
        /* acquire the surface */
        tsurface = _e_hwc_window_target_window_surface_acquire((E_Hwc_Window_Target *)hwc_window);
        if (!tsurface) return EINA_FALSE;

        if (hwc_window->tsurface &&
            hwc_window->tsurface != hwc_window->display_info.tsurface)
          _e_hwc_window_target_window_surface_release((E_Hwc_Window_Target *)hwc_window, hwc_window->tsurface);

        hwc_window->tsurface = tsurface;

        /* the damage isn't supported by hwc extension yet */
        CLEAR(fb_damage);

        tdm_hwc_set_client_target_buffer(thwc, hwc_window->tsurface, fb_damage);

        ee_rendered_hw_list = e_hwc_window_target_window_ee_rendered_hw_list_get(target_hwc_window);
        n_thw = eina_list_count(ee_rendered_hw_list);
        if (n_thw)
          {
             EHWINF("FET ts:%10p ------- {%25s}, state:%s",
                    NULL, hwc_window, hwc_window->tsurface, "@TARGET WINDOW@",
                    e_hwc_window_state_string_get(hwc_window->state));

             i = 0;
             EINA_LIST_FOREACH(ee_rendered_hw_list, l, hw)
               {
                  EHWINF("  (%d) with ------- {%25s}, state:%s, zpos:%d, deleted:%s",
                         hwc_window->ec, hw,
                         i++, hw->ec ? hw->ec->icccm.title : "UNKNOWN",
                         e_hwc_window_state_string_get(hw->state),
                         hwc_window->zpos, hwc_window->is_deleted ? "yes" : "no");
               }
          }
        else
          EHWINF("FET ts:%10p ------- {%25s}, state:%s, zpos:%d no hwc_windows to render.",
                 NULL, hwc_window, hwc_window->tsurface, "@TARGET WINDOW@",
                 e_hwc_window_state_string_get(hwc_window->state), hwc_window->zpos);
     }
   else
     {
        if (!_e_hwc_window_target_window_clear((E_Hwc_Window_Target *)hwc_window))
          ERR("fail to _e_hwc_window_target_window_clear");

        if (!hwc_window->tsurface) return EINA_FALSE;

        hwc_window->tsurface = NULL;

        CLEAR(fb_damage);

        tdm_hwc_set_client_target_buffer(thwc, NULL, fb_damage);
     }

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

   if (e_hwc_window_is_cursor(hwc_window))
     {
        E_Pointer *pointer = NULL;

        pointer = e_pointer_get(ec);
        EINA_SAFETY_ON_NULL_RETURN_VAL(pointer, EINA_FALSE);

        e_pointer_hwc_set(pointer, EINA_TRUE);
     }
   else
     {
        cqueue = _get_wayland_tbm_client_queue(ec);
        if (cqueue)
          wayland_tbm_server_client_queue_activate(cqueue, 0, 0, 0);
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

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   output = hwc->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   transform = e_comp_wl_output_buffer_transform_get(ec);

   if (output->config.rotation != 0 && (output->config.rotation / 90) == transform)
      e_comp_screen_rotation_ignore_output_transform_send(ec, EINA_TRUE);

   if (e_hwc_window_is_cursor(hwc_window))
     {
        E_Pointer *pointer = NULL;

        pointer = e_pointer_get(ec);
        if (pointer)
          e_pointer_hwc_set(pointer, EINA_FALSE);
     }
   else
     {
        cqueue = _get_wayland_tbm_client_queue(ec);
        if (cqueue)
        /* TODO: do we have to immediately inform a wayland client
         *       that an e_client got redirected or wait till it's being composited
         *       on the fb_target and a hw overlay owned by it gets free? */
          wayland_tbm_server_client_queue_deactivate(cqueue);

        _e_hwc_window_client_recover(hwc_window);
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

   if (hwc_window->accepted_state != state) hwc_window->accepted_state = state;

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

   e_object_ref(E_OBJECT(hwc_window));

   EHWINF(" added the render_list -- {%25s}.", ec, hwc_window, ec->icccm.title);
}

EINTERN Eina_Bool
e_hwc_window_is_on_target_window(E_Hwc_Window *hwc_window)
{
   Eina_List *ee_rendered_hw_list = NULL;
   E_Hwc_Window_Target *target_hwc_window;
   E_Hwc_Window *target_window;
   E_Hwc_Window *hw;
   const Eina_List *l, *ll;
   tbm_surface_h target_tsurface;

   target_hwc_window = _e_hwc_window_target_window_get(hwc_window);
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   target_window = (E_Hwc_Window *)target_hwc_window;
   if (e_hwc_window_state_get(target_window) != E_HWC_WINDOW_STATE_DEVICE) return EINA_FALSE;

   EINA_LIST_FOREACH(target_hwc_window->rendered_tsurface_list, l, target_tsurface)
     {
        tbm_surface_internal_get_user_data(target_tsurface, ee_rendered_hw_list_key, (void**)&ee_rendered_hw_list);

        EINA_LIST_FOREACH(ee_rendered_hw_list, ll, hw)
          if (hw == hwc_window) return EINA_TRUE;
     }

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
       return "DV"; // Device
     case E_HWC_WINDOW_STATE_VIDEO:
       return "VD"; // Video
     case E_HWC_WINDOW_STATE_CURSOR:
       return "CS"; // Cursor
     default:
       return "UNKNOWN";
    }
}
