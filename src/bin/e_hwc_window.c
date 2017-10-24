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
_e_hwc_window_target_surface_queue_clear(E_Hwc_Window_Target *target_window)
{
   tbm_surface_queue_h tqueue = NULL;
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_window, EINA_FALSE);

   tqueue = target_window->queue;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, EINA_FALSE);

   while ((tsurface = e_hwc_window_target_surface_queue_acquire(target_window)))
     e_hwc_window_target_surface_queue_release(target_window, tsurface);

  return EINA_TRUE;
}

static tbm_surface_h
_e_hwc_window_surface_from_ecore_evas_acquire(E_Hwc_Window_Target *target_window)
{
   tbm_surface_h tsurface = NULL;
   tbm_surface_queue_h queue = NULL;
   E_Output *output = ((E_Hwc_Window *)target_window)->output;

   if (!target_window->queue)
     {
        if(!(queue = _get_tbm_surface_queue(e_comp)))
          {
             WRN("fail to _get_tbm_surface_queue");
             return NULL;
          }

        target_window->queue = queue;

        /* dpms on at the first */
        if (!e_output_dpms_set(output, E_OUTPUT_DPMS_ON))
          WRN("fail to set the dpms on.");
     }

   tsurface = e_hwc_window_target_surface_queue_acquire(target_window);

   return tsurface;
}

static tbm_surface_h
_e_hwc_window_surface_from_client_acquire(E_Hwc_Window *window)
{
   E_Client *ec = window->ec;
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
   E_Hwc_Window *window;
   E_Zone *zone;
   Eina_Bool result;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   zone = ec->zone;
   EINA_SAFETY_ON_NULL_RETURN(zone);
   EINA_SAFETY_ON_NULL_RETURN(zone->output_id);

   output = e_output_find(zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN(output);

   window = e_hwc_window_new(output);
   EINA_SAFETY_ON_NULL_RETURN(window);

   result = e_hwc_window_set_ec(window, ec);
   EINA_SAFETY_ON_TRUE_RETURN(result != EINA_TRUE);

   result = e_hwc_window_mark_unvisible(window);
   EINA_SAFETY_ON_TRUE_RETURN(result != EINA_TRUE);

   result = e_hwc_window_set_skip_flag(window);
   EINA_SAFETY_ON_TRUE_RETURN(result != EINA_TRUE);

   result = e_hwc_window_set_state(window, E_HWC_WINDOW_STATE_NONE);
   EINA_SAFETY_ON_TRUE_RETURN(result != EINA_TRUE);

   INF("E_Hwc_Window: new window(%p)", window);

   return;
}

static void
_e_hwc_window_client_cb_del(void *data EINA_UNUSED, E_Client *ec)
{
   E_Hwc_Window *window;

   window = e_output_find_window_by_ec_in_all_outputs(ec);

   e_hwc_window_free(window);

   INF("E_Hwc_Window: free window(%p)", window);
}

static Eina_Bool
_e_hwc_window_client_cb_zone_set(void *data, int type, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;
   E_Zone *zone;
   E_Output *output;
   E_Hwc_Window *window = NULL;
   Eina_Bool result;

   ev = event;
   EINA_SAFETY_ON_NULL_GOTO(ev, fail);

   ec = ev->ec;
   EINA_SAFETY_ON_NULL_GOTO(ec, fail);

   zone = ec->zone;
   EINA_SAFETY_ON_NULL_GOTO(zone, fail);
   EINA_SAFETY_ON_NULL_GOTO(zone->output_id, fail);

   output = e_output_find(zone->output_id);
   EINA_SAFETY_ON_NULL_GOTO(output, fail);

   window = e_output_find_window_by_ec_in_all_outputs(ec);

   /* we manage the video window in the video module */
   if (e_hwc_window_is_video(window)) goto end;

   if (window)
     {
        if (window->output == output) goto end;

        e_hwc_window_free(window);
     }

   window = e_hwc_window_new(output);
   EINA_SAFETY_ON_NULL_GOTO(window, fail);

   result = e_hwc_window_set_ec(window, ec);
   EINA_SAFETY_ON_TRUE_GOTO(result != EINA_TRUE, fail);

   result = e_hwc_window_mark_unvisible(window);
   EINA_SAFETY_ON_TRUE_GOTO(result != EINA_TRUE, fail);

   result = e_hwc_window_set_skip_flag(window);
   EINA_SAFETY_ON_TRUE_GOTO(result != EINA_TRUE, fail);

   result = e_hwc_window_set_state(window, E_HWC_WINDOW_STATE_NONE);
   EINA_SAFETY_ON_TRUE_GOTO(result != EINA_TRUE, fail);

   INF("E_Hwc_Window: output is changed for ec(%p)", ec);

end:
   return ECORE_CALLBACK_PASS_ON;
fail:
   if (window)
     e_hwc_window_free(window);

   return ECORE_CALLBACK_PASS_ON;
}

/* gets called as evas_renderer enqueues a new buffer into the queue */
static void
_e_hwc_window_target_queue_acquirable_cb(tbm_surface_queue_h surface_queue, void *data)
{
    E_Hwc_Window_Target *target_window = (E_Hwc_Window_Target *)data;
    uint64_t value = 1;
    int ret;

    INF("hwc-opt: evas_renderer enqueued a new buffer into the queue");

    ret = write(target_window->event_fd, &value, sizeof(value));
    if (ret == -1)
      ERR("failed to send acquirable event:%m");
}

static void
_evas_renderer_queue_has_new_composited_buffer(void *data)
{
   E_Hwc_Window_Target *target_window = (E_Hwc_Window_Target *)data;
   E_Hwc_Window *window;
   const Eina_List *l;

   target_window->render_cnt++;

   INF("hwc-opt: evas_renderer has a new buffer in the queue, renderer_cnt:%d",
           target_window->render_cnt);

   EINA_LIST_FOREACH(e_output_windows_get(((E_Hwc_Window *)target_window)->output), l, window)
     {
        if (window->is_deleted) continue;

        if (window->get_notified_about_composition_end)
          {
             if (window->frame_num >= target_window->render_cnt)
               {
                  window->get_notified_about_composition_end = EINA_FALSE;

                  INF("hwc-opt: the composition for ec:%p {name:%s} is done, throw the update_job.",
                          window->ec, window->ec ? window->ec->icccm.name : "none");

                  /* an e_client got composited to the fb_target so we have to
                   * inform the hwc extension about this by throwing the update_job */
                  window->got_composited = EINA_TRUE;
                  e_comp_render_queue();
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

   INF("hwc-opt: ecore_main_loop: the new iteration.");

   fd = ecore_main_fd_handler_fd_get(fd_handler);
   len = read(fd, buffer, sizeof(buffer));
   if (len == -1)
     ERR("failed to read queue acquire event fd:%m");

   _evas_renderer_queue_has_new_composited_buffer(data);

   return ECORE_CALLBACK_RENEW;
}

static E_Hwc_Window_Target *
_e_hwc_window_target_new(E_Output *output)
{
   const char *name = NULL;
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);
   E_Hwc_Window_Target *target_window = NULL;
   tdm_error error = TDM_ERROR_NONE;
   Ecore_Fd_Handler *event_hdlr = NULL;

   name = ecore_evas_engine_name_get(e_comp->ee);
   EINA_SAFETY_ON_NULL_RETURN_VAL(name, NULL);

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

   target_window = E_NEW(E_Hwc_Window_Target, 1);
   EINA_SAFETY_ON_NULL_GOTO(target_window, fail);

   ((E_Hwc_Window *)target_window)->is_target = EINA_TRUE;
   /* the target window is always displayed on hw layer */
   ((E_Hwc_Window *)target_window)->type = TDM_COMPOSITION_DEVICE;
   ((E_Hwc_Window *)target_window)->output = output;

   target_window->window.hwc_wnd = tdm_output_hwc_create_window(output->toutput, &error);
   EINA_SAFETY_ON_TRUE_GOTO(error != TDM_ERROR_NONE, fail);

   /* don't change type for this hwc_wnd. This hwc window need to enable
    * the target window if we don't have the clients */
   error = tdm_hwc_window_set_composition_type(target_window->window.hwc_wnd,
                                               TDM_COMPOSITION_CLIENT);
   EINA_SAFETY_ON_TRUE_GOTO(error != TDM_ERROR_NONE, fail);

   target_window->ee = e_comp->ee;
   target_window->evas = ecore_evas_get(target_window->ee);
   target_window->event_fd = eventfd(0, EFD_NONBLOCK);
   event_hdlr =
            ecore_main_fd_handler_add(target_window->event_fd, ECORE_FD_READ,
                                      _evas_renderer_finished_composition_cb,
                                      (void *)target_window, NULL, NULL);

   ecore_evas_manual_render(target_window->ee);

   target_window->queue = _get_tbm_surface_queue();

   /* as evas_renderer has finished its work (to provide a composited buffer) it enqueues
    * the result buffer into this queue and acquirable cb gets called; this cb does nothing
    * except the writing into the event_fd object, this writing causes the new ecore_main loop
    * iteration to be triggered ('cause we've registered ecore_main fd handler to check this writing);
    * so it's just a way to inform E20's HWC that evas_renderer has done its work */
   tbm_surface_queue_add_acquirable_cb(target_window->queue, _e_hwc_window_target_queue_acquirable_cb,
           (void *)target_window);

   return target_window;

fail:
   ecore_evas_manual_render_set(e_comp->ee, 0);

   if (target_window)
     {
        if (event_hdlr)
          ecore_main_fd_handler_del(event_hdlr);

        if (target_window->event_fd)
          close(target_window->event_fd);

        free(target_window);
     }

   return NULL;
}

static Eina_Bool
_e_hwc_window_set_redirected(E_Hwc_Window *window, Eina_Bool redirected)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(window->ec, EINA_FALSE);

   e_client_redirected_set(window->ec, redirected);

   return EINA_TRUE;
}

static void
_e_hwc_window_recover_ec(E_Hwc_Window *window)
{
   E_Client *ec = window->ec;
   E_Comp_Wl_Client_Data *cdata = NULL;
   E_Comp_Wl_Buffer *buffer = NULL;
   tbm_surface_h tsurface =NULL;

   if (!ec) return;

   cdata = ec->comp_data;
   if (!cdata) return;

   buffer = cdata->buffer_ref.buffer;

   if (!buffer)
     {
        tsurface = e_hwc_window_get_displaying_surface(window);
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
e_hwc_window_set_ec(E_Hwc_Window *window, E_Client *ec)
{
   tdm_error error;
   tdm_output *toutput;

   if (window->ec == ec)
     return EINA_TRUE;

   toutput = window->output->toutput;
   EINA_SAFETY_ON_NULL_RETURN_VAL(toutput, EINA_FALSE);

   window->hwc_wnd = tdm_output_hwc_create_window(toutput, &error);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

   window->ec = ec;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_init(void)
{
   E_Hwc_Window_Target *target_window;
   Eina_List *l;
   E_Output *output;

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

   EINA_LIST_FOREACH(e_comp->e_comp_screen->outputs, l, output)
     {
        Eina_Bool result;

        if (!output->config.enabled) continue;

        target_window = _e_hwc_window_target_new(output);
        EINA_SAFETY_ON_NULL_RETURN_VAL(target_window, EINA_FALSE);

        result = e_hwc_window_set_skip_flag(target_window);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(result != EINA_TRUE, EINA_FALSE);

        output->windows = eina_list_append(output->windows, target_window);
     }

   return EINA_TRUE;
}

EINTERN E_Hwc_Window *
e_hwc_window_new(E_Output *output)
{
   E_Hwc_Window *window = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   window = E_NEW(E_Hwc_Window, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, NULL);

   window->output = output;

   output->windows = eina_list_append(output->windows, window);

   INF("E_Hwc_Window: window(%p), output(%p)", window, output);

   return window;
}

EINTERN void
e_hwc_window_free(E_Hwc_Window *window)
{
   EINA_SAFETY_ON_NULL_RETURN(window);
   tdm_output *toutput;

   /* we cannot remove the window because we need to release the commit_data */
   if (e_hwc_window_get_displaying_surface(window))
     {  /* mark as deleted and delete when commit_data will be released */
        window->is_deleted = EINA_TRUE;
        window->ec = NULL;
        return;
     }

   toutput = window->output->toutput;
   EINA_SAFETY_ON_NULL_RETURN(toutput);

   if (window->hwc_wnd)
      tdm_output_hwc_destroy_window(toutput, window->hwc_wnd);

   window->output->windows = eina_list_remove(window->output->windows, window);

   free(window);
}

EINTERN Eina_Bool
e_hwc_window_set_zpos(E_Hwc_Window *window, int zpos)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   window->zpos = zpos;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_set_skip_flag(E_Hwc_Window *window)
{
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   if (window->skip_flag)
     return EINA_TRUE;

   if (window->hwc_wnd)
     {
        error = tdm_hwc_window_set_flags(window->hwc_wnd, TDM_HWC_WINDOW_FLAG_SKIP);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);
     }

   window->skip_flag = 1;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_unset_skip_flag(E_Hwc_Window *window)
{
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   if (!window->skip_flag)
     return EINA_TRUE;

   if (window->hwc_wnd)
     {
        error = tdm_hwc_window_unset_flags(window->hwc_wnd, TDM_HWC_WINDOW_FLAG_SKIP);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);
     }

   window->skip_flag = 0;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_mark_visible(E_Hwc_Window *window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   window->is_visible = EINA_TRUE;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_mark_unvisible(E_Hwc_Window *window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   window->is_visible = EINA_FALSE;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_update(E_Hwc_Window *window)
{
   tdm_hwc_window_info info = {0};
   tdm_hwc_window *hwc_wnd;
   tbm_surface_h surface;
   E_Client *ec;
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   ec = window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   hwc_wnd = window->hwc_wnd;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_wnd, EINA_FALSE);

   error = tdm_hwc_window_set_zpos(hwc_wnd, window->zpos);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

   /* for video we update the geometry and buffer in the video module */
   if (e_hwc_window_is_video(window) && (e_hwc_window_get_state(window) != E_HWC_WINDOW_STATE_CLIENT_CANDIDATE))
     {
        if (!window->tsurface)
           e_hwc_window_set_skip_flag(window);

        /* we always try to display the video window on the hw layer */
        error = tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_VIDEO);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

        window->type = TDM_COMPOSITION_VIDEO;

        return EINA_TRUE;
     }

   if (e_hwc_window_get_state(window) == E_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
     {
        /* as the e_client got composited on the fb_target we have to inform the hwc
         * extension to allow it does its work, so we set the TDM_COMPOSITION_CLIENT type */
        if (window->got_composited)
          {
             INF("hwc-opt: ew:%p -- ec%p {name:%s} - buffer's been composited, inform hwc-extension.",
                     window, window->ec, window->ec->icccm.name);

             /* reset for the next DEVICE -> CLIENT_CANDIDATE transition */
             window->got_composited = EINA_FALSE;

             error = tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_CLIENT);
             EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

             window->type = TDM_COMPOSITION_CLIENT;
          }

        /* TODO: what's about z-pos? */
        /* if the E_Hwc_Window is in the E_WINDOW_STATE_CLIENT_CANDIDATE state there's no
         * reason to set info, update buffer, etc..., 'cause an underlying hwc_window is
         * in a blocked state and it'll ignore any changes anyway */
        return EINA_TRUE;
     }
   else
     {
        /* window manager could ask to prevent some e_clients being shown by hw directly */
        if (ec->hwc_acceptable)
          {
             error = tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_DEVICE);
             EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

             window->type = TDM_COMPOSITION_DEVICE;
          }
        else
          {
             error = tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_CLIENT);
             EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

             window->type = TDM_COMPOSITION_CLIENT;
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
e_hwc_window_is_target(E_Hwc_Window *window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   return window->is_target;
}

EINTERN Eina_Bool
e_hwc_window_is_video(E_Hwc_Window *window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   return window->is_video;
}

EINTERN Eina_Bool
e_hwc_window_fetch(E_Hwc_Window *window)
{
   tbm_surface_h tsurface = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   if (e_comp_canvas_norender_get() > 0)
     return EINA_FALSE;

   if (window->output->wait_commit)
     return EINA_FALSE;

   /* for video we set buffer in the video module */
   if (e_hwc_window_is_video(window)) return EINA_FALSE;

   if (window->skip_flag)
     {
        if (e_hwc_window_is_target(window))
          {
             if (!_e_hwc_window_target_surface_queue_clear((E_Hwc_Window_Target *)window))
               ERR("fail to _e_hwc_window_target_surface_queue_clear");
          }
        return EINA_FALSE;
     }

   /* we can use the tbm_surface_queue owned by "gl_drm/gl_tbm" evas engine,
    * for both optimized and no-optimized hwc, at least now */
   if (e_hwc_window_is_target(window))
     {
        /* acquire the surface */
        tsurface = _e_hwc_window_surface_from_ecore_evas_acquire((E_Hwc_Window_Target *)window);
     }
   else
     {
        /* acquire the surface */
        tsurface = _e_hwc_window_surface_from_client_acquire(window);

        /* For send frame::done to client */
        if (!tsurface)
          e_pixmap_image_clear(window->ec->pixmap, 1);
     }

   if (!tsurface) {
      return EINA_FALSE;
   }

   /* exist tsurface for update window */
   window->tsurface = tsurface;

   /* FIXME: we don't worry about the rotation */
   if (e_hwc_window_is_target(window))
     {
        tdm_hwc_region fb_damage;

        /* the damage isn't supported by hwc extension yet */
        memset(&fb_damage, 0, sizeof(fb_damage));

        tdm_output_hwc_set_client_target_buffer(window->output->toutput, tsurface, fb_damage);
        INF("hwc-opt: set surface:%p on the fb_target.", tsurface);
     }
   else
     {
        tdm_hwc_window_set_buffer(window->hwc_wnd, tsurface);
        INF("hwc-opt: set surface:%p (ec:%p, title:%s, name:%s) on the hwc_wnd:%p.",
                tsurface, window->ec, window->ec->icccm.title, window->ec->icccm.name,
                window->hwc_wnd);
     }

   window->update_exist = EINA_TRUE;

   return EINA_TRUE;
}

EINTERN void
e_hwc_window_unfetch(E_Hwc_Window *window)
{
   EINA_SAFETY_ON_NULL_RETURN(window);
   EINA_SAFETY_ON_NULL_RETURN(window->tsurface);

   if (!e_hwc_window_is_on_hw_overlay(window)) return;

   if (e_hwc_window_is_target(window))
     {
        e_hwc_window_target_surface_queue_release((E_Hwc_Window_Target *)window, window->tsurface);
     }

   window->tsurface = e_hwc_window_get_displaying_surface(window);

   if (e_hwc_window_is_target(window))
     {
        tdm_hwc_region fb_damage;

        /* the damage isn't supported by hwc extension yet */
        memset(&fb_damage, 0, sizeof(fb_damage));

        tdm_output_hwc_set_client_target_buffer(window->output->toutput, window->tsurface, fb_damage);
        INF("hwc-opt: (unfetch) set surface:%p on the fb_target.", window->tsurface);
     }
   else
     {
        tdm_hwc_window_set_buffer(window->hwc_wnd, window->tsurface);
        INF("hwc-opt: (unfetch) set surface:%p on the hwc_wnd:%p.", window->tsurface, window->hwc_wnd);
     }

   window->update_exist = EINA_FALSE;
}

/* we can do commit if we set surface at least to one window which displayed on
 * the hw layer*/
static Eina_Bool
_can_commit(E_Output *output)
{
   Eina_List *l;
   E_Hwc_Window *window;

   EINA_LIST_FOREACH(output->windows, l, window)
     {
        if (!e_hwc_window_is_on_hw_overlay(window)) continue;

        if (window->update_exist) return EINA_TRUE;

        if (window->commit_data && window->commit_data->tsurface) return EINA_TRUE;
        if (e_hwc_window_get_displaying_surface(window)) return EINA_TRUE;
     }

   return EINA_FALSE;
}

EINTERN E_Hwc_Window_Commit_Data *
e_hwc_window_commit_data_aquire(E_Hwc_Window *window)
{
   E_Hwc_Window_Commit_Data *commit_data = NULL;

   /* we can't unref a buffer till it being composited to the fb_target */
   if (e_hwc_window_get_state(window) == E_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
     return NULL;

   if (!e_hwc_window_is_on_hw_overlay(window))
     {
        window->update_exist = EINA_FALSE;

        /* if the window unset is needed and we can do commit */
        if (e_hwc_window_get_displaying_surface(window) && _can_commit(window->output))
          {
             commit_data = E_NEW(E_Hwc_Window_Commit_Data, 1);
             EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, NULL);

             return commit_data;
          }

        return NULL;
     }

   /* check update_exist */
   if (!window->update_exist)
     {
        return NULL;
     }

   if (window->tsurface == e_hwc_window_get_displaying_surface(window))
     return NULL;

   commit_data = E_NEW(E_Hwc_Window_Commit_Data, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, NULL);

   window->update_exist = EINA_FALSE;

   if (e_hwc_window_is_target(window) || e_hwc_window_is_video(window))
     {
        commit_data->tsurface = window->tsurface;
        tbm_surface_internal_ref(commit_data->tsurface);
     }
   else
     {
        commit_data->tsurface = window->tsurface;
        tbm_surface_internal_ref(commit_data->tsurface);

        e_comp_wl_buffer_reference(&commit_data->buffer_ref,
                                   _get_comp_wl_buffer(window->ec));
     }

   return commit_data;
}

EINTERN Eina_Bool
e_hwc_window_commit_data_release(E_Hwc_Window *window)
{
   tbm_surface_h tsurface = NULL;
   tbm_surface_h displaying_surface;

   /* we don't have data to release */
   if (!window->commit_data) return EINA_FALSE;

   tsurface = window->commit_data->tsurface;

   if (!tsurface)
     {
        e_comp_wl_buffer_reference(&window->display_info.buffer_ref, NULL);
     }
   else if (e_hwc_window_is_target(window) || e_hwc_window_is_video(window))
     {
        e_comp_wl_buffer_reference(&window->display_info.buffer_ref, NULL);
     }
   else
     {
        e_comp_wl_buffer_reference(&window->display_info.buffer_ref, window->commit_data->buffer_ref.buffer);
     }

   e_comp_wl_buffer_reference(&window->commit_data->buffer_ref, NULL);

   displaying_surface = e_hwc_window_get_displaying_surface(window);
   if (displaying_surface)
     {
        if (e_hwc_window_is_target(window))
          {
             e_hwc_window_target_surface_queue_release((E_Hwc_Window_Target *)window, displaying_surface);
          }
     }

   /* update window display info */
   if (displaying_surface)
     tbm_surface_internal_unref(displaying_surface);

   window->display_info.tsurface = tsurface;

   free(window->commit_data);
   window->commit_data = NULL;

   if (window->is_deleted && !e_hwc_window_get_displaying_surface(window))
     e_hwc_window_free(window);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_target_surface_queue_can_dequeue(E_Hwc_Window_Target *target_window)
{
   tbm_surface_queue_h tqueue = NULL;
   int num_free = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_window, EINA_FALSE);

   tqueue = target_window->queue;
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
e_hwc_window_target_surface_queue_acquire(E_Hwc_Window_Target *target_window)
{
   tbm_surface_queue_h queue = NULL;
   tbm_surface_h surface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(target_window, NULL);

   queue = target_window->queue;
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
e_hwc_window_target_surface_queue_release(E_Hwc_Window_Target *target_window, tbm_surface_h tsurface)
{
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   tbm_surface_queue_h tqueue = NULL;

   EINA_SAFETY_ON_NULL_RETURN(target_window);
   EINA_SAFETY_ON_NULL_RETURN(tsurface);

   tqueue = target_window->queue;
   EINA_SAFETY_ON_NULL_RETURN(tqueue);

   tsq_err = tbm_surface_queue_release(tqueue, tsurface);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("Failed to release tbm_surface(%p) from tbm_surface_queue(%p): tsq_err = %d", tsurface, tqueue, tsq_err);
        return;
     }
}

EINTERN uint64_t
e_hwc_window_target_get_current_renderer_cnt(E_Hwc_Window_Target *target_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_window, 0);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(target_window->window.is_target, 0);

   return target_window->render_cnt;
}

EINTERN Eina_Bool
e_hwc_window_prepare_commit(E_Hwc_Window *window)
{
   E_Hwc_Window_Commit_Data *data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   if (window->output->wait_commit) return EINA_FALSE;

   data = e_hwc_window_commit_data_aquire(window);
   if (!data) return EINA_FALSE;

   window->commit_data = data;

   /* send frame event enlightenment dosen't send frame evnet in nocomp */
   if (window->ec)
     e_pixmap_image_clear(window->ec->pixmap, 1);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_offscreen_commit(E_Hwc_Window *window)
{
   E_Hwc_Window_Commit_Data *data = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   data = e_hwc_window_commit_data_aquire(window);

   if (!data) return EINA_TRUE;

   window->commit_data = data;

   e_hwc_window_commit_data_release(window);

   /* send frame event enlightenment doesn't send frame event in nocomp */
   if (window->ec)
     e_pixmap_image_clear(window->ec->pixmap, 1);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_activate(E_Hwc_Window *window)
{
   struct wayland_tbm_client_queue *cqueue = NULL;
   E_Client *ec;

   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   if (window->activation_state == E_HWC_WINDOW_ACTIVATION_STATE_ACTIVATED)
     return EINA_TRUE;

   ec = window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   _e_hwc_window_set_redirected(window, EINA_FALSE);

   if (e_hwc_window_is_video(window))
   {
      window->activation_state = E_HWC_WINDOW_ACTIVATION_STATE_ACTIVATED;

      window->ec->comp_data->video_is_on_hw_layer = EINA_TRUE;
      /* to try set the video UI on hw layer */
      e_comp_render_queue();

      return EINA_TRUE;
   }

   cqueue = _e_hwc_window_wayland_tbm_client_queue_get(ec);

   if (cqueue)
     wayland_tbm_server_client_queue_activate(cqueue, 0, 0, 0);

   e_comp_object_hwc_update_set(ec->frame, EINA_TRUE);

   window->activation_state = E_HWC_WINDOW_ACTIVATION_STATE_ACTIVATED;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_deactivate(E_Hwc_Window *window)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   if (window->activation_state == E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED)
     return EINA_TRUE;

   ec = window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   _e_hwc_window_set_redirected(window, EINA_TRUE);

   if (e_hwc_window_is_video(window))
   {
      e_video_prepare_window_to_compositing(window);

      window->activation_state = E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED;
      window->ec->comp_data->video_is_on_hw_layer = EINA_FALSE;

      return EINA_TRUE;
   }

   cqueue = _e_hwc_window_wayland_tbm_client_queue_get(ec);

   if (cqueue)
     /* TODO: do we have to immediately inform a wayland client
      *       that an e_client got redirected or wait till it's being composited
      *       on the fb_target and a hw overlay owned by it gets free? */
     wayland_tbm_server_client_queue_deactivate(cqueue);

   _e_hwc_window_recover_ec(window);

   window->activation_state = E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_is_on_hw_overlay(E_Hwc_Window *window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   if (window->skip_flag) return EINA_FALSE;
   if (window->type != TDM_COMPOSITION_DEVICE && window->type != TDM_COMPOSITION_VIDEO)
     return EINA_FALSE;

   return EINA_TRUE;
}

EINTERN tbm_surface_h
e_hwc_window_get_displaying_surface(E_Hwc_Window *window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, NULL);

   return window->display_info.tsurface;
}

EINTERN Eina_Bool
e_hwc_window_set_state(E_Hwc_Window *window, E_Hwc_Window_State state)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   window->state = state;

   return EINA_TRUE;
}

EINTERN E_Hwc_Window_State
e_hwc_window_get_state(E_Hwc_Window *window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, E_HWC_WINDOW_STATE_NONE);

   return window->state;
}

/* offset - relative offset of frame the notification should be issued for */
EINTERN Eina_Bool
e_hwc_window_get_notified_about_composition_end(E_Hwc_Window *window, uint64_t offset)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(window, EINA_FALSE);

   E_Hwc_Window_Target *target_window = e_output_get_target_window(window->output);

   window->get_notified_about_composition_end = EINA_TRUE;
   window->frame_num = e_hwc_window_target_get_current_renderer_cnt(target_window) + offset + 1;

   INF("hwc-opt: ew:%p -- ec:%p {name:%s} asked to be notified about a %llu composited frame,"
           " current render_cnt:%llu, delay:%llu.", window, window->ec, window->ec ? window->ec->icccm.name : "none",
                    window->frame_num, target_window->render_cnt, offset);

   return EINA_TRUE;
}
