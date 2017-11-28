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

static void
_new_buffer_is_acquired_from_evas_renderer_queue(E_Hwc_Window_Target *target_hwc_window)
{
   E_Hwc_Window *hwc_window;
   const Eina_List *l;

   EINA_LIST_FOREACH(e_output_hwc_windows_get(((E_Hwc_Window *)target_hwc_window)->output->output_hwc), l, hwc_window)
     {
        if (hwc_window->is_deleted) continue;

        if (hwc_window->get_notified_about_need_unset_cc_type && hwc_window->got_composited)
          {
             hwc_window->delay--;
             if (hwc_window->delay > 0) continue;

             hwc_window->get_notified_about_need_unset_cc_type = EINA_FALSE;

             hwc_window->need_unset_cc_type = EINA_TRUE;

             ELOGF("HWC-OPT", "the composition buffer with {name:%s} will be displayed"
                   "in the next frame.",
                   hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
                   hwc_window->ec ? hwc_window->ec->icccm.name : "UNKNOWN");
        }
     }
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
   if (tsurface)
     _new_buffer_is_acquired_from_evas_renderer_queue(target_hwc_window);

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

   if (!e_comp_object_hwc_update_exists(ec->frame)) return NULL;

   e_comp_object_hwc_update_set(ec->frame, EINA_FALSE);

   tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
   if (!tsurface)
     {
        ERR("fail to wayland_tbm_server_get_surface");
        return NULL;
     }

   return tsurface;
}

/* get current tbm_surface (surface which has been committed last) for the e_client */
static tbm_surface_h
_e_comp_get_current_surface_for_cl(E_Client *ec)
{
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   if (!wl_comp_data) return NULL;

   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   if (!cdata) return NULL;

   E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata->buffer_ref;

   E_Comp_Wl_Buffer *e_wl_buff = buffer_ref->buffer;
   if (!e_wl_buff) return NULL;

   return wayland_tbm_server_get_surface(wl_comp_data->tbm.server, e_wl_buff->resource);
}

static void
_e_hwc_window_client_cb_new(void *data EINA_UNUSED, E_Client *ec)
{
   E_Output *output;
   E_Hwc_Window *hwc_window;
   E_Zone *zone;
   Eina_Bool result;

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

   hwc_window = e_hwc_window_new(output->output_hwc, ec);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   result = e_hwc_window_set_state(hwc_window, E_HWC_WINDOW_STATE_NONE);
   EINA_SAFETY_ON_TRUE_RETURN(result != EINA_TRUE);

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
   Eina_Bool result;

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

   hwc_window = e_hwc_window_new(output->output_hwc, ec);
   EINA_SAFETY_ON_NULL_GOTO(hwc_window, fail);

   result = e_hwc_window_set_state(hwc_window, E_HWC_WINDOW_STATE_NONE);
   EINA_SAFETY_ON_TRUE_GOTO(result != EINA_TRUE, fail);

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

static int
_get_enqueued_surface_num(tbm_surface_queue_h queue)
{
   tbm_surface_queue_error_e err;
   int enqueue_num = 0;

   err = tbm_surface_queue_get_trace_surface_num(queue, TBM_SURFACE_QUEUE_TRACE_ENQUEUE, &enqueue_num);
   if (err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail to tbm_surface_queue_get_trace_surface_num (TBM_SURFACE_QUEUE_TRACE_ENQUEUE)");
        return 0;
     }

   return enqueue_num;
}

static void
_evas_renderer_queue_has_new_composited_buffer(void *data)
{
   E_Hwc_Window_Target *target_hwc_window = (E_Hwc_Window_Target *)data;
   E_Hwc_Window *hwc_window;
   const Eina_List *l;
   int enqueued_surface_num;

   target_hwc_window->render_cnt++;

   ELOGF("HWC-OPT", "evas_renderer has a new buffer in the queue, renderer_cnt:%llu",
         NULL, NULL, target_hwc_window->render_cnt);

   enqueued_surface_num = _get_enqueued_surface_num(target_hwc_window->queue);
   EINA_LIST_FOREACH(e_output_hwc_windows_get(((E_Hwc_Window *)target_hwc_window)->output->output_hwc), l, hwc_window)
     {
        if (hwc_window->is_deleted) continue;

        if (hwc_window->get_notified_about_need_unset_cc_type)
          {
             if (hwc_window->frame_num <= target_hwc_window->render_cnt)
               {
                  hwc_window->got_composited = EINA_TRUE;

                  if (enqueued_surface_num > 1)
                    {
                       hwc_window->delay = enqueued_surface_num - 1;

                       ELOGF("HWC-OPT", "the composition for {name:%s} is done, but render queue has %d buffers before",
                            hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
                            hwc_window->ec ? hwc_window->ec->icccm.name : "UNKNOWN",
                            hwc_window->delay);

                       continue;
                    }

                  hwc_window->get_notified_about_need_unset_cc_type = EINA_FALSE;

                  hwc_window->need_unset_cc_type = EINA_TRUE;

                  ELOGF("HWC-OPT", "the composition for {name:%s} is done.",
                        hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
                        hwc_window->ec ? hwc_window->ec->icccm.name : "UNKNOWN");
               }
          }
     }
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

   _evas_renderer_queue_has_new_composited_buffer(data);

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

static Eina_Bool
_e_hwc_window_set_redirected(E_Hwc_Window *hwc_window, Eina_Bool redirected)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window->ec, EINA_FALSE);

   e_client_redirected_set(hwc_window->ec, redirected);

   return EINA_TRUE;
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
e_hwc_window_new(E_Output_Hwc *output_hwc, E_Client *ec)
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

   hwc_window->hwc_wnd = tdm_output_hwc_create_window(toutput, &error);
   if (error != TDM_ERROR_NONE)
     {
        ERR("cannot create tdm_hwc_window for toutput(%p)", toutput);
        E_FREE(hwc_window);
        return NULL;
     }

   hwc_window->is_excluded = EINA_TRUE;
   error = tdm_hwc_window_set_composition_type(hwc_window->hwc_wnd, TDM_COMPOSITION_NONE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, NULL);

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

EINTERN Eina_Bool
e_hwc_window_update(E_Hwc_Window *hwc_window)
{
   tdm_hwc_window_info info = {0};
   tdm_hwc_window *hwc_wnd;
   tbm_surface_h surface;
   E_Client *ec;
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   hwc_wnd = hwc_window->hwc_wnd;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_wnd, EINA_FALSE);

   error = tdm_hwc_window_set_zpos(hwc_wnd, hwc_window->zpos);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

   /* for video we update the geometry and buffer in the video module */
   if (e_hwc_window_is_video(hwc_window) && (e_hwc_window_get_state(hwc_window) != E_HWC_WINDOW_STATE_CLIENT_CANDIDATE))
     {
        if (!hwc_window->tsurface) {
           error = tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_NONE);
           EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

           hwc_window->type = TDM_COMPOSITION_NONE;

           hwc_window->is_excluded = EINA_TRUE;
        } else {
           /* we always try to display the video hwc_window on the hw layer */
           error = tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_VIDEO);
           EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

           hwc_window->type = TDM_COMPOSITION_VIDEO;
        }

        return EINA_TRUE;
     }

   if (e_hwc_window_get_state(hwc_window) == E_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
     {
        /* as the e_client got composited on the fb_target we have to inform the hwc
         * extension to allow it does its work, so we set the TDM_COMPOSITION_CLIENT type */
        if (hwc_window->need_unset_cc_type)
          {

             ELOGF("HWC-OPT", "ew:%p -- {name:%s} - buffer's been composited, inform hwc-extension.",
                   hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec,
                   hwc_window, hwc_window->ec ? hwc_window->ec->icccm.name : "UNKNOWN");

             /* reset for the next DEVICE -> CLIENT_CANDIDATE transition */
             hwc_window->got_composited = EINA_FALSE;
             hwc_window->need_unset_cc_type = EINA_FALSE;

             error = tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_CLIENT);
             EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

             hwc_window->type = TDM_COMPOSITION_CLIENT;
          }

        /* TODO: what's about z-pos? */
        /* if the E_Hwc_Window is in the E_WINDOW_STATE_CLIENT_CANDIDATE state there's no
         * reason to set info, update buffer, etc..., 'cause an underlying hwc_window is
         * in a blocked state and it'll ignore any changes anyway */
        return EINA_TRUE;
     }
   else
     {
        /* hwc_window manager could ask to prevent some e_clients being shown by hw directly */
        if (hwc_window->hwc_acceptable)
          {
             error = tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_DEVICE);
             EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

             hwc_window->type = TDM_COMPOSITION_DEVICE;
          }
        else
          {
             error = tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_CLIENT);
             EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

             hwc_window->type = TDM_COMPOSITION_CLIENT;
          }
      }

    info.src_config.pos.x = 0;
    info.src_config.pos.y = 0;
    info.src_config.pos.w = ec->w;
    info.src_config.pos.h = ec->h;

    /* do we have to fill out these? */
    info.src_config.size.h = ec->w;
    info.src_config.size.v = ec->h;

    /* do we have to fill out these? */
    info.src_config.format = TBM_FORMAT_ARGB8888;

    info.dst_pos.x = ec->x;
    info.dst_pos.y = ec->y;
    info.dst_pos.w = ec->w;
    info.dst_pos.h = ec->h;

    info.transform = TDM_TRANSFORM_NORMAL;

    error = tdm_hwc_window_set_info(hwc_wnd, &info);
    EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

    /* if e_client is in cl_list it means it has attached/committed
     * tbm_surface anyway
     *
     * NB: only an applicability of the e_client to own the hw overlay
     *     is checked here, no buffer fetching happens here however */
    surface = _e_comp_get_current_surface_for_cl(ec);

    error = tdm_hwc_window_set_buffer(hwc_wnd, surface);
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
e_hwc_window_fetch(E_Hwc_Window *hwc_window)
{
   tbm_surface_h tsurface = NULL;
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (e_comp_canvas_norender_get() > 0)
     return EINA_FALSE;

   output = hwc_window->output;

   if (output->wait_commit)
     return EINA_FALSE;

   /* for video we set buffer in the video module */
   if (e_hwc_window_is_video(hwc_window)) return EINA_FALSE;

   if (hwc_window->is_excluded)
     {
        if (e_hwc_window_is_target(hwc_window))
          {
             if (!_e_hwc_window_target_surface_queue_clear((E_Hwc_Window_Target *)hwc_window))
               ERR("fail to _e_hwc_window_target_surface_queue_clear");
          }
        return EINA_FALSE;
     }

   /* we can use the tbm_surface_queue owned by "gl_drm/gl_tbm" evas engine,
    * for both optimized and no-optimized hwc, at least now */
   if (e_hwc_window_is_target(hwc_window))
     {
        /* acquire the surface */
        tsurface = _e_hwc_window_surface_from_ecore_evas_acquire((E_Hwc_Window_Target *)hwc_window);
     }
   else
     {
        /* acquire the surface */
        tsurface = _e_hwc_window_surface_from_client_acquire(hwc_window);

        /* For send frame::done to client */
        if (!tsurface)
          e_pixmap_image_clear(hwc_window->ec->pixmap, 1);
     }

   if (!tsurface)
     {
        ELOGF("HWC-OPT", "fail to fetch hwc_window",
              hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec);
        return EINA_FALSE;
     }

   /* exist tsurface for update hwc_window */
   hwc_window->tsurface = tsurface;

   if (e_hwc_window_is_target(hwc_window))
     {
        tdm_hwc_region fb_damage;

        /* the damage isn't supported by hwc extension yet */
        memset(&fb_damage, 0, sizeof(fb_damage));

        tdm_output_hwc_set_client_target_buffer(output->toutput, tsurface, fb_damage);
        ELOGF("HWC-OPT", "set surface:%p on the fb_target",
              hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec, tsurface);
     }
   else
     {
        tdm_hwc_window_set_buffer(hwc_window->hwc_wnd, tsurface);
        ELOGF("HWC-OPT", "set surface:%p (title:%s, name:%s) on the hwc_wnd:%p.",
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

   /* we can't unref a buffer till it being composited to the fb_target */
   if (e_hwc_window_get_state(hwc_window) == E_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
     return EINA_FALSE;

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

   /* check update_exist */
   if (!hwc_window->update_exist)
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

EINTERN uint64_t
e_hwc_window_target_get_current_renderer_cnt(E_Hwc_Window_Target *target_hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_hwc_window, 0);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(target_hwc_window->hwc_window.is_target, 0);

   return target_hwc_window->render_cnt;
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

   _e_hwc_window_set_redirected(hwc_window, EINA_FALSE);

   if (e_hwc_window_is_video(hwc_window))
   {
      hwc_window->activation_state = E_HWC_WINDOW_ACTIVATION_STATE_ACTIVATED;

      /* to try set the video UI on hw layer */
      e_comp_render_queue();

      return EINA_TRUE;
   }

   cqueue = _e_hwc_window_wayland_tbm_client_queue_get(ec);

   if (cqueue)
     wayland_tbm_server_client_queue_activate(cqueue, 0, 0, 0);

   e_comp_object_hwc_update_set(ec->frame, EINA_TRUE);

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

   _e_hwc_window_set_redirected(hwc_window, EINA_TRUE);

   if (e_hwc_window_is_video(hwc_window))
   {
      e_video_prepare_hwc_window_to_compositing(hwc_window);

      hwc_window->activation_state = E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED;

      return EINA_TRUE;
   }

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
   if (hwc_window->type != TDM_COMPOSITION_DEVICE && hwc_window->type != TDM_COMPOSITION_VIDEO)
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

   hwc_window->state = state;

   return EINA_TRUE;
}

EINTERN E_Hwc_Window_State
e_hwc_window_get_state(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, E_HWC_WINDOW_STATE_NONE);

   return hwc_window->state;
}

/* offset - relative offset of frame the notification should be issued for */
EINTERN Eina_Bool
e_hwc_window_get_notified_about_need_unset_cc_type(E_Hwc_Window *hwc_window, E_Hwc_Window_Target *target_hwc_window, uint64_t offset)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   hwc_window->get_notified_about_need_unset_cc_type = EINA_TRUE;
   hwc_window->frame_num = e_hwc_window_target_get_current_renderer_cnt(target_hwc_window) + offset + 1;

   ELOGF("HWC-OPT", "ew:%p -- {name:%s} asked to be notified about a %llu composited frame will be displayed in the next frame,"
         " current render_cnt:%llu, delay:%llu.",
         hwc_window->ec ? ec->pixmap : NULL, hwc_window->ec,
         hwc_window, hwc_window->ec ? hwc_window->ec->icccm.name : "UNKNOWN",
         hwc_window->frame_num, target_hwc_window->render_cnt, offset);

   return EINA_TRUE;
}
