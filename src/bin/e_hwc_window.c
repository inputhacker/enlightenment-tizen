#include "e.h"

# include <Evas_Engine_GL_Drm.h>
# include <Evas_Engine_GL_Tbm.h>
# include <Evas_Engine_Software_Tbm.h>
# include <sys/eventfd.h>
# include <gbm/gbm_tbm.h>

static E_Client_Hook *client_hook_new = NULL;
static E_Client_Hook *client_hook_del = NULL;
static Ecore_Event_Handler *zone_set_event_handler = NULL;

struct wayland_tbm_client_queue *
_e_hwc_window_wayland_tbm_client_queue_get(E_Client *ec)
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
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, NULL);

   return cqueue;
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
_get_tbm_surface_queue()
{
   return e_comp->e_comp_screen->tqueue;
}

static Eina_Bool
_e_hwc_window_target_surface_queue_clear(E_Hwc_Window_Target *target_hwc_window)
{
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   tqueue = target_hwc_window->queue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   while ((tsurface = e_hwc_window_target_surface_queue_acquire(target_hwc_window)))
     e_hwc_window_target_surface_queue_release(target_hwc_window, tsurface);

  return EINA_TRUE;
}

static tbm_surface_h
_e_hwc_window_surface_from_ecore_evas_acquire(E_Hwc_Window_Target *target_hwc_window)
{
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_h queue = NULL;

   if (!target_hwc_window->queue)
     {
        if(!(queue = _get_tbm_surface_queue(e_comp)))
          {
             WRN("fail to _get_tbm_surface_queue");
             return NULL;
          }

        target_hwc_window->queue = queue;
     }

   tsurface = e_hwc_window_target_surface_queue_acquire(target_hwc_window);

   return tsurface;
}

static tbm_surface_h
_e_hwc_window_surface_from_client_acquire(E_Hwc_Window *hwc_window)
{
   E_Client *ec = hwc_window->ec;
   E_Comp_Wl_Buffer *buffer = _get_comp_wl_buffer(ec);
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(buffer, NULL);

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
   if (!tsurface)
     {
        ERR("fail to wayland_tbm_server_get_surface");
        return NULL;
     }

   e_comp_object_hwc_update_set(ec->frame, EINA_FALSE);

   return tsurface;
}

static void
_e_hwc_window_client_cb_new(void *data EINA_UNUSED, E_Client *ec)
{
   E_Output *output;
   E_Hwc_Window *hwc_window;
   E_Zone *zone;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   zone = ec->zone;
   EINA_SAFETY_ON_NULL_RETURN(zone);
   EINA_SAFETY_ON_NULL_RETURN(zone->output_id);

   output = e_output_find(zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN(output);

   /* if an e_client belongs to the e_output managed by
    * no-opt hwc there's no need to deal with hwc_windows */
   if (!e_output_hwc_windows_enabled(output->output_hwc))
      return;

   hwc_window = e_hwc_window_new(output->output_hwc, ec, E_HWC_WINDOW_STATE_NONE);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   /* set the hwc window to the e client */
   ec->hwc_window = hwc_window;

   ELOGF("HWC-OPT", "E_Hwc_Window: new window(%p)", ec->pixmap, ec, hwc_window);

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

   /* if an e_client belongs to the e_output managed by
    * no-opt hwc there's no need to deal with hwc_windows */
   if (!e_output_hwc_windows_enabled(output->output_hwc))
      return;

   if (!ec->hwc_window) return;

   ELOGF("HWC-OPT", "E_Hwc_Window: free hwc_window(%p)", ec->pixmap, ec, ec->hwc_window);

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

   /* if an e_client belongs to the e_output managed by
    * no-opt hwc there's no need to deal with hwc_windows */
   if (!e_output_hwc_windows_enabled(output->output_hwc))
      return ECORE_CALLBACK_PASS_ON;

   if (ec->hwc_window)
     {
        /* we manage the video window in the video module */
        if (e_hwc_window_is_video(ec->hwc_window)) goto end;
        if (ec->hwc_window->output == output) goto end;

        e_hwc_window_free(ec->hwc_window);
        ec->hwc_window = NULL;
     }

   hwc_window = e_hwc_window_new(output->output_hwc, ec, E_HWC_WINDOW_STATE_NONE);
   EINA_SAFETY_ON_NULL_GOTO(hwc_window, fail);

   /* set the hwc window to the e client */
   ec->hwc_window = hwc_window;

   ELOGF("HWC-OPT", "E_Hwc_Window: output is changed for hwc_window(%p)", ec->pixmap, ec, hwc_window);

end:
   return ECORE_CALLBACK_PASS_ON;
fail:
   if (hwc_window)
     e_hwc_window_free(hwc_window);

   return ECORE_CALLBACK_PASS_ON;
}

/* gets called as evas_renderer enqueues a new buffer into the queue */
static void
_e_hwc_window_target_queue_acquirable_cb(tbm_surface_queue_h surface_queue, void *data)
{
    E_Hwc_Window_Target *target_hwc_window = (E_Hwc_Window_Target *)data;
    uint64_t value = 1;
    int ret;

    ELOGF("HWC-OPT", "evas_renderer enqueued a new buffer into the queue", NULL, NULL);

    ret = write(target_hwc_window->event_fd, &value, sizeof(value));
    if (ret == -1)
      ERR("failed to send acquirable event:%m");
}

/* gets called as evas_renderer dequeues a new buffer from the queue */
static void
_e_hwc_window_target_queue_dequeue_cb(tbm_surface_queue_h surface_queue, void *data)
{
  E_Hwc_Window_Target *target_hwc_window = (E_Hwc_Window_Target *)data;

  if (!target_hwc_window) return;

  target_hwc_window->post_render_flush_cnt++;
  ELOGF("HWC-OPT", "[soolim] dequeue the target_hwc_window(%p) post_render_flush_cnt(%d)", NULL, NULL, target_hwc_window, target_hwc_window->post_render_flush_cnt);
}

/* gets called at the beginning of an ecore_main_loop iteration */
static Eina_Bool
_evas_renderer_finished_composition_cb(void *data, Ecore_Fd_Handler *fd_handler)
{
   int len;
   int fd;
   char buffer[64];

   ELOGF("HWC-OPT", "ecore_main_loop: the new iteration.", NULL, NULL);

   fd = ecore_main_fd_handler_fd_get(fd_handler);
   len = read(fd, buffer, sizeof(buffer));
   if (len == -1)
     ERR("failed to read queue acquire event fd:%m");

   return ECORE_CALLBACK_RENEW;
}

static E_Hwc_Window_Target *
_e_hwc_window_target_new(E_Output_Hwc *output_hwc)
{
   const char *name = NULL;
   E_Hwc_Window_Target *target_hwc_window = NULL;
   tdm_error error = TDM_ERROR_NONE;
   Ecore_Fd_Handler *event_hdlr = NULL;
   E_Output *output = NULL;

   name = ecore_evas_engine_name_get(e_comp->ee);
   EINA_SAFETY_ON_NULL_RETURN_VAL(name, NULL);

   output = output_hwc->output;

   if (!strcmp("gl_drm", name))
     {
        Evas_Engine_Info_GL_Drm *einfo = NULL;
        /* get the evas_engine_gl_drm information */
        einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
        if (!einfo)
          {
             ERR("fail to get the GL_Drm einfo.");
             goto fail;
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
   /* the target hwc_window is always displayed on hw layer */
   ((E_Hwc_Window *)target_hwc_window)->type = TDM_COMPOSITION_DEVICE;
   ((E_Hwc_Window *)target_hwc_window)->output = output;

   target_hwc_window->hwc_window.hwc_wnd = tdm_output_hwc_create_window(output->toutput, &error);
   EINA_SAFETY_ON_TRUE_GOTO(error != TDM_ERROR_NONE, fail);

   target_hwc_window->hwc_window.is_excluded = EINA_TRUE;
   error = tdm_hwc_window_set_composition_type(target_hwc_window->hwc_window.hwc_wnd,
           TDM_COMPOSITION_NONE);
   EINA_SAFETY_ON_TRUE_GOTO(error != TDM_ERROR_NONE, fail);

   error = tdm_hwc_window_set_zpos(target_hwc_window->hwc_window.hwc_wnd, 0);
   EINA_SAFETY_ON_TRUE_GOTO(error != TDM_ERROR_NONE, fail);

   target_hwc_window->ee = e_comp->ee;
   target_hwc_window->evas = ecore_evas_get(target_hwc_window->ee);
   target_hwc_window->event_fd = eventfd(0, EFD_NONBLOCK);
   event_hdlr =
            ecore_main_fd_handler_add(target_hwc_window->event_fd, ECORE_FD_READ,
                                      _evas_renderer_finished_composition_cb,
                                      (void *)target_hwc_window, NULL, NULL);

   ecore_evas_manual_render(target_hwc_window->ee);

   target_hwc_window->queue = _get_tbm_surface_queue();

   /* as evas_renderer has finished its work (to provide a composited buffer) it enqueues
    * the result buffer into this queue and acquirable cb gets called; this cb does nothing
    * except the writing into the event_fd object, this writing causes the new ecore_main loop
    * iteration to be triggered ('cause we've registered ecore_main fd handler to check this writing);
    * so it's just a way to inform E20's HWC that evas_renderer has done its work */
   tbm_surface_queue_add_acquirable_cb(target_hwc_window->queue, _e_hwc_window_target_queue_acquirable_cb,
           (void *)target_hwc_window);

  /* add the dequeue callback */
  tbm_surface_queue_add_dequeue_cb(target_hwc_window->queue, _e_hwc_window_target_queue_dequeue_cb, (void *)target_hwc_window);

   return target_hwc_window;

fail:
   ecore_evas_manual_render_set(e_comp->ee, 0);

   if (target_hwc_window)
     {
        if (event_hdlr)
          ecore_main_fd_handler_del(event_hdlr);

        if (target_hwc_window->event_fd)
          close(target_hwc_window->event_fd);

        free(target_hwc_window);
     }

   return NULL;
}

static void
_e_hwc_window_recover_ec(E_Hwc_Window *hwc_window)
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
        tsurface = e_hwc_window_get_displaying_surface(hwc_window);
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

EINTERN Eina_Bool
e_hwc_window_init(E_Output_Hwc *output_hwc)
{
   E_Hwc_Window_Target *target_hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);

   if (!e_output_hwc_windows_enabled(output_hwc)) return EINA_FALSE;

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

   target_hwc_window = _e_hwc_window_target_new(output_hwc);
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, EINA_FALSE);

   /* set the target_window to the output_hwc */
   output_hwc->target_hwc_window = target_hwc_window;

   output_hwc->hwc_windows = eina_list_append(output_hwc->hwc_windows, target_hwc_window);

   return EINA_TRUE;
}

// TODO:
EINTERN void
e_hwc_window_deinit(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   // TODO:
}

EINTERN E_Hwc_Window *
e_hwc_window_new(E_Output_Hwc *output_hwc, E_Client *ec, E_Hwc_Window_State state)
{
   E_Hwc_Window *hwc_window = NULL;
   tdm_output *toutput;
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc->output, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);

   toutput = output_hwc->output->toutput;
   EINA_SAFETY_ON_NULL_RETURN_VAL(toutput, EINA_FALSE);

   hwc_window = E_NEW(E_Hwc_Window, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, NULL);

   hwc_window->output = output_hwc->output;
   hwc_window->ec = ec;
   hwc_window->state = state;

   if (state == E_HWC_WINDOW_STATE_VIDEO)
     hwc_window->hwc_wnd = tdm_output_hwc_create_video_window(toutput, &error);
   else
     hwc_window->hwc_wnd = tdm_output_hwc_create_window(toutput, &error);

   if (error != TDM_ERROR_NONE)
     {
        ERR("cannot create tdm_hwc_window for toutput(%p)", toutput);
        E_FREE(hwc_window);
        return NULL;
     }

   if (state != E_HWC_WINDOW_STATE_VIDEO)
     {
        hwc_window->is_excluded = EINA_TRUE;
        error = tdm_hwc_window_set_composition_type(hwc_window->hwc_wnd, TDM_COMPOSITION_NONE);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, NULL);
     }
   else
     {
        hwc_window->is_excluded = EINA_FALSE;
        hwc_window->is_video = 1;
        /* the video hwc_window is always displayed on hw layer */
        hwc_window->type = TDM_COMPOSITION_DEVICE;
     }

   output_hwc->hwc_windows = eina_list_append(output_hwc->hwc_windows, hwc_window);

   ELOGF("HWC-OPT", "E_Hwc_Window: hwc_window(%p), output(%p)",
         hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec,
         hwc_window, output_hwc->output);

   return hwc_window;
}

EINTERN void
e_hwc_window_free(E_Hwc_Window *hwc_window)
{
   E_Output_Hwc *output_hwc = NULL;
   tdm_output *toutput = NULL;

   EINA_SAFETY_ON_NULL_RETURN(hwc_window);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window->output);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window->output->output_hwc);

   /* we cannot remove the hwc_window because we need to release the commit_data */
   if (e_hwc_window_get_displaying_surface(hwc_window))
     {  /* mark as deleted and delete when commit_data will be released */
        hwc_window->is_deleted = EINA_TRUE;
        hwc_window->ec = NULL;
        hwc_window->is_excluded = EINA_TRUE;
        hwc_window->state = E_HWC_WINDOW_STATE_NONE;
        return;
     }

   toutput = hwc_window->output->toutput;
   EINA_SAFETY_ON_NULL_RETURN(toutput);

   if (hwc_window->hwc_wnd)
      tdm_output_hwc_destroy_window(toutput, hwc_window->hwc_wnd);

   output_hwc = hwc_window->output->output_hwc;
   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   output_hwc->hwc_windows = eina_list_remove(output_hwc->hwc_windows, hwc_window);

   free(hwc_window);
}

EINTERN Eina_Bool
e_hwc_window_set_zpos(E_Hwc_Window *hwc_window, int zpos)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   hwc_window->zpos = zpos;

   return EINA_TRUE;
}

EINTERN int
e_hwc_window_get_zpos(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->is_excluded) return -1;

   return hwc_window->zpos;
}

static unsigned int
_e_hwc_window_aligned_width_get(tbm_surface_h tsurface)
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
_e_hwc_window_info_set(E_Hwc_Window *hwc_window)
{
   tbm_surface_h tsurface = hwc_window->tsurface;
   E_Output *output = hwc_window->output;
   E_Client *ec = hwc_window->ec;
   tbm_surface_info_s surf_info;
   int size_w, size_h, src_x, src_y, src_w, src_h;
   int dst_x, dst_y, dst_w, dst_h;
   tbm_format format;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(tsurface == NULL, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(output == NULL, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(ec == NULL, EINA_FALSE);

   /* set hwc_window when the layer infomation is different from the previous one */
   tbm_surface_get_info(tsurface, &surf_info);

   format = surf_info.format;

   size_w = _e_hwc_window_aligned_width_get(tsurface);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(size_w == 0, EINA_FALSE);

   size_h = surf_info.height;

   src_x = 0;
   src_y = 0;
   src_w = surf_info.width;
   src_h = surf_info.height;

   dst_x = ec->x;
   dst_y = ec->y;

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

        error = tdm_hwc_window_set_info(hwc_window->hwc_wnd, &hwc_window->info);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_update(E_Hwc_Window *hwc_window)
{
   tdm_hwc_window *hwc_wnd;
   E_Client *ec;
   tdm_error error;
   Eina_Bool result;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   hwc_wnd = hwc_window->hwc_wnd;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_wnd, EINA_FALSE);

   if (e_hwc_window_is_video(hwc_window))
        return EINA_TRUE;

   error = tdm_hwc_window_set_zpos(hwc_wnd, hwc_window->zpos);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

   /* hwc_window manager could ask to prevent some e_clients being shown by hw directly */
   if (hwc_window->hwc_acceptable && hwc_window->tsurface)
     {
        error = tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_DEVICE);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

        hwc_window->type = TDM_COMPOSITION_DEVICE;

        result = _e_hwc_window_info_set(hwc_window);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(result != EINA_TRUE, EINA_FALSE);
     }
   else
     {
        error = tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_CLIENT);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

        hwc_window->type = TDM_COMPOSITION_CLIENT;
     }

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
e_hwc_window_fetch(E_Hwc_Window *hwc_window)
{
   tbm_surface_h tsurface = NULL;
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (e_comp_canvas_norender_get() > 0)
     return EINA_FALSE;

   output = hwc_window->output;

   /* for video we set buffer in the video module */
   if (e_hwc_window_is_video(hwc_window)) return EINA_FALSE;

   if (hwc_window->is_excluded)
     {
        if (e_hwc_window_is_target(hwc_window))
          {
             if (hwc_window->update_exist && hwc_window->tsurface)
               e_hwc_window_target_surface_queue_release((E_Hwc_Window_Target *)hwc_window, hwc_window->tsurface);

             if (!_e_hwc_window_target_surface_queue_clear((E_Hwc_Window_Target *)hwc_window))
               ERR("fail to _e_hwc_window_target_surface_queue_clear");
          }

        hwc_window->tsurface = NULL;

        return EINA_FALSE;
     }

   if (hwc_window->update_exist)
      return EINA_TRUE;

   if (e_hwc_window_is_target(hwc_window))
     {
        /* acquire the surface */
        tsurface = _e_hwc_window_surface_from_ecore_evas_acquire((E_Hwc_Window_Target *)hwc_window);

        if (!tsurface)
          {
               ELOGF("HWC-OPT", "no updated surface (target_window) on the hwc_wnd:%p.",
                     NULL, NULL, hwc_window->hwc_wnd);

             return EINA_FALSE;
          }
     }
   else
     {
        /* acquire the surface */
        tsurface = _e_hwc_window_surface_from_client_acquire(hwc_window);

        /* For send frame::done to client */
        if (!tsurface)
          e_pixmap_image_clear(hwc_window->ec->pixmap, 1);

        if (hwc_window->tsurface == tsurface) {
           ELOGF("HWC-OPT", "no updated surface (title:%s, name:%s) on the hwc_wnd:%p.",
                  hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec,
                  hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
                  hwc_window->ec ? hwc_window->ec->icccm.name : "UNKNOWN",
                  hwc_window->hwc_wnd);

           return EINA_FALSE;
        }
     }

   /* exist tsurface for update hwc_window */
   hwc_window->tsurface = tsurface;

   if (e_hwc_window_is_target(hwc_window))
     {
        tdm_hwc_region fb_damage;

        /* the damage isn't supported by hwc extension yet */
        memset(&fb_damage, 0, sizeof(fb_damage));

        tdm_output_hwc_set_client_target_buffer(output->toutput, tsurface, fb_damage);
        ELOGF("HWC-OPT", "[soolim] set surface:%p on the fb_target",
              hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec, tsurface);
     }
   else
     {
        tdm_hwc_window_set_buffer(hwc_window->hwc_wnd, tsurface);
        ELOGF("HWC-OPT", "[soolim] set surface:%p (title:%s, name:%s) on the hwc_wnd:%p.",
              hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec,
              tsurface, hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
              hwc_window->ec ? hwc_window->ec->icccm.name : "UNKNOWN",
              hwc_window->hwc_wnd);
     }

   hwc_window->update_exist = EINA_TRUE;

   return EINA_TRUE;
}

EINTERN void
e_hwc_window_unfetch(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window->tsurface);

   if (!e_hwc_window_is_on_hw_overlay(hwc_window)) return;

   if (e_hwc_window_is_target(hwc_window))
     {
        e_hwc_window_target_surface_queue_release((E_Hwc_Window_Target *)hwc_window, hwc_window->tsurface);
     }

   hwc_window->tsurface = e_hwc_window_get_displaying_surface(hwc_window);

   if (e_hwc_window_is_target(hwc_window))
     {
        E_Output *output = hwc_window->output;
        tdm_hwc_region fb_damage;

        /* the damage isn't supported by hwc extension yet */
        memset(&fb_damage, 0, sizeof(fb_damage));

        tdm_output_hwc_set_client_target_buffer(output->toutput, hwc_window->tsurface, fb_damage);
        ELOGF("HWC-OPT", "(unfetch) set surface:%p on the fb_target.",
              hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec, hwc_window->tsurface);
     }
   else
     {
        tdm_hwc_window_set_buffer(hwc_window->hwc_wnd, hwc_window->tsurface);
        ELOGF("HWC-OPT", "(unfetch) set surface:%p on the hwc_wnd:%p.",
              hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec, hwc_window->tsurface, hwc_window->hwc_wnd);
     }

   hwc_window->update_exist = EINA_FALSE;
}

EINTERN Eina_Bool
e_hwc_window_commit_data_aquire(E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_Commit_Data *commit_data = NULL;

   if (!e_hwc_window_is_on_hw_overlay(hwc_window))
     {
        hwc_window->update_exist = EINA_FALSE;

        /* if the hwc_window unset is needed and we can do commit */
        if (e_hwc_window_get_displaying_surface(hwc_window))
          {
             commit_data = E_NEW(E_Hwc_Window_Commit_Data, 1);
             EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, EINA_FALSE);

             hwc_window->commit_data = commit_data;

             return EINA_TRUE;
          }

        return EINA_FALSE;
     }

   /* If there are not updates and there is not displaying surface it means the
    * hwc_window's composition type just became TDM_COMPOSITION_DEVICE. Buffer
    * for this hwc_window was set in the previous e_hwc_window_fetch but ref for
    * buffer didn't happen because hwc_window had TDM_COMPOSITION_CLIENT type.
    * So e20 needs to make ref for the current buffer which is set on the hwc_window.
    */
   if (!hwc_window->update_exist && e_hwc_window_get_displaying_surface(hwc_window))
     {
        return EINA_FALSE;
     }

   if (hwc_window->tsurface == e_hwc_window_get_displaying_surface(hwc_window))
     return EINA_FALSE;

   commit_data = E_NEW(E_Hwc_Window_Commit_Data, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, EINA_FALSE);

   hwc_window->update_exist = EINA_FALSE;

   if (e_hwc_window_is_target(hwc_window) || e_hwc_window_is_video(hwc_window))
     {
        commit_data->tsurface = hwc_window->tsurface;
        tbm_surface_internal_ref(commit_data->tsurface);
     }
   else
     {
        commit_data->tsurface = hwc_window->tsurface;
        tbm_surface_internal_ref(commit_data->tsurface);

        e_comp_wl_buffer_reference(&commit_data->buffer_ref,
                                   _get_comp_wl_buffer(hwc_window->ec));
     }

   hwc_window->commit_data = commit_data;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_commit_data_release(E_Hwc_Window *hwc_window)
{
   tbm_surface_h tsurface = NULL;
   tbm_surface_h displaying_surface;

   /* we don't have data to release */
   if (!hwc_window->commit_data) return EINA_FALSE;

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

   displaying_surface = e_hwc_window_get_displaying_surface(hwc_window);
   if (displaying_surface)
     {
        if (e_hwc_window_is_target(hwc_window))
          {
             e_hwc_window_target_surface_queue_release((E_Hwc_Window_Target *)hwc_window, displaying_surface);
          }
     }

   /* update hwc_window display info */
   if (displaying_surface)
     tbm_surface_internal_unref(displaying_surface);

   hwc_window->display_info.tsurface = tsurface;

   free(hwc_window->commit_data);
   hwc_window->commit_data = NULL;

   if (hwc_window->is_deleted && !e_hwc_window_get_displaying_surface(hwc_window))
     e_hwc_window_free(hwc_window);

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

EINTERN tbm_surface_h
e_hwc_window_target_surface_queue_acquire(E_Hwc_Window_Target *target_hwc_window)
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

EINTERN void
e_hwc_window_target_surface_queue_release(E_Hwc_Window_Target *target_hwc_window, tbm_surface_h tsurface)
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

   cqueue = _e_hwc_window_wayland_tbm_client_queue_get(ec);

   if (cqueue)
     wayland_tbm_server_client_queue_activate(cqueue, 0, 0, 0);

   hwc_window->activation_state = E_HWC_WINDOW_ACTIVATION_STATE_ACTIVATED;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_deactivate(E_Hwc_Window *hwc_window)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->activation_state == E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED)
     return EINA_TRUE;

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   /* set the redirected to TRUE */
   e_client_redirected_set(ec, EINA_TRUE);

   cqueue = _e_hwc_window_wayland_tbm_client_queue_get(ec);

   if (cqueue)
     /* TODO: do we have to immediately inform a wayland client
      *       that an e_client got redirected or wait till it's being composited
      *       on the fb_target and a hw overlay owned by it gets free? */
     wayland_tbm_server_client_queue_deactivate(cqueue);

   _e_hwc_window_recover_ec(hwc_window);

   hwc_window->activation_state = E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_is_on_hw_overlay(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->is_excluded) return EINA_FALSE;
   if (hwc_window->type != TDM_COMPOSITION_DEVICE)
     return EINA_FALSE;

   return EINA_TRUE;
}

EINTERN tbm_surface_h
e_hwc_window_get_displaying_surface(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, NULL);

   return hwc_window->display_info.tsurface;
}

EINTERN Eina_Bool
e_hwc_window_set_state(E_Hwc_Window *hwc_window, E_Hwc_Window_State state)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->state != state)
     hwc_window->state = state;

   return EINA_TRUE;
}

EINTERN E_Hwc_Window_State
e_hwc_window_get_state(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, E_HWC_WINDOW_STATE_NONE);

   return hwc_window->state;
}
