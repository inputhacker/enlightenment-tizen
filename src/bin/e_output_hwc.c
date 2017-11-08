#include "e.h"


static Eina_Bool
_need_target_window(E_Output *eo)
{
   Eina_List *l;
   E_Output_Hwc_Window *window;

   EINA_LIST_FOREACH(eo->windows, l, window)
     {
        if (window->skip_flag) continue;

        if (e_output_hwc_window_is_target(eo, window)) return EINA_TRUE;

        if (!e_output_hwc_window_is_on_hw_overlay(eo, window))
          return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_hwc_re_evaluate_init(E_Output *eout)
{
   const Eina_List *windows, *l;
   E_Output_Hwc_Window *window = NULL;

   windows = e_output_windows_get(eout);
   EINA_LIST_FOREACH(windows, l, window)
     e_output_hwc_window_mark_unvisible(eout, window);

   return EINA_TRUE;
}

static uint64_t
_get_evas_renderer_delay(tbm_surface_queue_h queue)
{
   tbm_surface_queue_error_e err;
   int dequeue_num = 0;
   int enqueue_num = 0;

   err = tbm_surface_queue_get_trace_surface_num(queue, TBM_SURFACE_QUEUE_TRACE_DEQUEUE, &dequeue_num);
   if (err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail to tbm_surface_queue_get_trace_surface_num (TBM_SURFACE_QUEUE_TRACE_DEQUEUE)");
        return 0;
     }

   err = tbm_surface_queue_get_trace_surface_num(queue, TBM_SURFACE_QUEUE_TRACE_ENQUEUE, &enqueue_num);
   if (err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        ERR("fail to tbm_surface_queue_get_trace_surface_num (TBM_SURFACE_QUEUE_TRACE_ENQUEUE)");
        return 0;
     }

   return dequeue_num + enqueue_num;
}

static void
_hwc_get_notified_about_need_unset_cc_type(E_Output *output, const Eina_List *windows)
{
   const Eina_List *l;
   E_Output_Hwc_Window *window;
   uint64_t delay;

   window = eina_list_data_get(windows);
   delay = _get_evas_renderer_delay(e_output_hwc_window_get_target_window(window->output)->queue);

   EINA_LIST_FOREACH(windows, l, window)
     if (e_output_hwc_window_get_state(output, window) == E_OUTPUT_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
       if (!window->get_notified_about_need_unset_cc_type && !window->is_deleted)
         e_output_hwc_window_get_notified_about_need_unset_cc_type(output, window, delay);

}

static Eina_Bool
_are_e_windows_with_client_candidate_state(E_Output *output, const Eina_List *windows)
{
   const Eina_List *l;
   E_Output_Hwc_Window *window;

   EINA_LIST_FOREACH(windows, l, window)
     if (e_output_hwc_window_get_state(output, window) == E_OUTPUT_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
       return EINA_TRUE;

   return EINA_FALSE;
}

static Eina_Bool
_hwc_update_e_windows_state(E_Output *output, const Eina_List *windows)
{
   const Eina_List *l;
   E_Output_Hwc_Window *window;

   EINA_LIST_FOREACH(windows, l, window)
     {
        if (window->is_deleted) continue;
        if (e_output_hwc_window_is_target(output, window)) continue;

        /* if an e_client got invisible or is invisible already/yet */
        if (window->skip_flag)
          {
             e_output_hwc_window_set_state(output, window, E_OUTPUT_HWC_WINDOW_STATE_NONE);
             continue;
          }

        switch (window->type)
          {
           case TDM_COMPOSITION_CLIENT:
             e_output_hwc_window_set_state(output, window, E_OUTPUT_HWC_WINDOW_STATE_CLIENT);
             break;

           case TDM_COMPOSITION_DEVICE:
             e_output_hwc_window_set_state(output, window, E_OUTPUT_HWC_WINDOW_STATE_DEVICE);
             break;

           case TDM_COMPOSITION_CLIENT_CANDIDATE:
             e_output_hwc_window_set_state(output, window, E_OUTPUT_HWC_WINDOW_STATE_CLIENT_CANDIDATE);
             break;

           case TDM_COMPOSITION_VIDEO:
             e_output_hwc_window_set_state(output, window, E_OUTPUT_HWC_WINDOW_STATE_VIDEO);
             break;

           default:
             e_output_hwc_window_set_state(output, window, E_OUTPUT_HWC_WINDOW_STATE_NONE);
             ERR("hwc-opt: unknown state of hwc_window.");
          }
     }

   return EINA_TRUE;
}

static const char*
_get_name_of_wnd_state(E_Output_Hwc_Window_State window_state)
{
    switch (window_state)
    {
     case E_OUTPUT_HWC_WINDOW_STATE_NONE:
       return "NONE";

     case E_OUTPUT_HWC_WINDOW_STATE_CLIENT:
       return "CLIENT";

     case E_OUTPUT_HWC_WINDOW_STATE_DEVICE:
       return "DEVICE";

     case E_OUTPUT_HWC_WINDOW_STATE_CLIENT_CANDIDATE:
       return "CLIENT_CANDIDATE";

     case E_OUTPUT_HWC_WINDOW_STATE_VIDEO:
       return "VIDEO";

     default:
       return "UNKNOWN";
    }
}

static int
_sort_cb(const void *d1, const void *d2)
{
   E_Output_Hwc_Window *window_1 = (E_Output_Hwc_Window *)d1;
   E_Output_Hwc_Window *window_2 = (E_Output_Hwc_Window *)d2;

   return window_1->zpos > window_2->zpos;
}

static void
_print_wnds_state(E_Output *output, const Eina_List *wnds)
{
    const Eina_List *l;
    Eina_List *sort_wnds;
    E_Output_Hwc_Window *window;

    sort_wnds = eina_list_clone(wnds);
    sort_wnds = eina_list_sort(sort_wnds, eina_list_count(sort_wnds), _sort_cb);

    EINA_LIST_FOREACH(sort_wnds, l, window)
      {
         if (window->skip_flag) continue;

         if (e_output_hwc_window_is_target(output, window))
           INF("hwc-opt:   ew:%p -- target_window, visible:%s", window, window->is_visible ? "yes" : "no");
         else
           INF("hwc-opt:   ew:%p -- ec:%p {name:%16s}, state:%s, visible:%s, deleted:%s, zpos:%d",
                   window, window->ec, window->ec->icccm.name, _get_name_of_wnd_state(window->state),
                   window->is_visible ? "yes" : "no", window->is_deleted ? "yes" : "no", window->zpos);
      }

    eina_list_free(sort_wnds);
}

static void
_update_skip_state(E_Output *output, const Eina_List *wnds)
{
   const Eina_List *l;
   E_Output_Hwc_Window *window;

   EINA_LIST_FOREACH(wnds, l, window)
     {
        if (window->is_visible)
          e_output_hwc_window_unset_skip_flag(output, window);
        else
          e_output_hwc_window_set_skip_flag(output, window);
     }
}

/* cl_list - list of e_clients that contains ALL visible e_clients for this
 * output ('eo')
 */
static Eina_Bool
_hwc_prepare(E_Output *eo, Eina_List *cl_list)
{
   const Eina_List *l;
   E_Client *ec;
   uint32_t num_changes;
   int zpos = 0;
   E_Output_Hwc_Window *window;
   const Eina_List *windows;
   Eina_Bool result;
   tdm_error tdm_err;

   /* clients are sorted in reverse order */
   EINA_LIST_REVERSE_FOREACH(cl_list, l, ec)
     {
        E_Output_Hwc_Window *window;
        window = e_output_hwc_window_find_window_by_ec(eo, ec);
        if (!window)
          {
             ERR("hwc-opt: cannot find the window by ec(%p)", ec);
             continue;
          }

        result = e_output_hwc_window_mark_visible(eo, window);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot mark an E_Output_Hwc_Window(%p) as visible", window);
             continue;
          }

        result = e_output_hwc_window_set_zpos(eo, window, zpos);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot set zpos for E_Output_Hwc_Window(%p)", window);
             continue;
          }

        result = e_output_hwc_window_update(eo, window);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot update E_Output_Hwc_Window(%p)", window);
             continue;
          }
        zpos++;
     }

   windows = e_output_windows_get(eo);
   if (!windows)
     {
        ERR("hwc-opt: cannot get list of windows for output(%p)", eo);
        return EINA_FALSE;
     }

   _update_skip_state(eo, windows);

   INF("hwc-opt: windows state before validate:");
   _print_wnds_state(eo, windows);

   /* make hwc extension choose which clients will own hw overlays */
   tdm_err = tdm_output_hwc_validate(eo->toutput, &num_changes);
   if (tdm_err != TDM_ERROR_NONE)
     {
        ERR("hwc-opt: failed to validate the output(%p)", eo->toutput);
        return EINA_FALSE;
     }

   if (num_changes)
     {
        int i;
        tdm_hwc_window **changed_hwc_window = NULL;
        tdm_hwc_window_composition *composition_types = NULL;
        E_Output_Hwc_Window *window;

        INF("hwc-opt: hwc extension required to change composition types.");

        changed_hwc_window = E_NEW(tdm_hwc_window *, num_changes);
        EINA_SAFETY_ON_NULL_RETURN_VAL(changed_hwc_window, EINA_FALSE);

        composition_types = E_NEW(tdm_hwc_window_composition, num_changes);
        EINA_SAFETY_ON_NULL_RETURN_VAL(composition_types, EINA_FALSE);

        tdm_err = tdm_output_hwc_get_changed_composition_types(eo->toutput,
                                              &num_changes, changed_hwc_window,
                                              composition_types);
        if (tdm_err != TDM_ERROR_NONE)
          {
             ERR("hwc-opt: failed to get changed composition types");
             free(changed_hwc_window);
             free(composition_types);
             return EINA_FALSE;
          }

        for (i = 0; i < num_changes; ++i)
          {
             window = e_output_hwc_window_find_by_twin(eo, changed_hwc_window[i]);
             if (!window)
               {
                  ERR("hwc-opt: cannot find the E_Output_Hwc_Window by hwc window");
                  free(changed_hwc_window);
                  free(composition_types);
                  return EINA_FALSE;
               }

             window->type = composition_types[i];
          }

        free(changed_hwc_window);
        free(composition_types);

        tdm_err = tdm_output_hwc_accept_changes(eo->toutput);
        if (tdm_err != TDM_ERROR_NONE)
          {
             ERR("hwc-opt: failed to accept changes required by the hwc extension");
             return EINA_FALSE;
          }
     }

   /* to keep a state of e_windows up to date we have to update their states
    * according to the changes wm and/or hw made */
   _hwc_update_e_windows_state(eo, windows);

   if (_are_e_windows_with_client_candidate_state(eo, windows))
     _hwc_get_notified_about_need_unset_cc_type(eo, windows);

   if (_need_target_window(eo))
     {
        E_Output_Hwc_Window_Target *target_window;

        INF("hwc-opt: hybrid composition");

        target_window = e_output_hwc_window_get_target_window(eo);
        if (!target_window)
          {
             ERR("hwc-opt: cannot get target window for output(%p)", eo);
             return EINA_FALSE;
          }

        result = e_output_hwc_window_mark_visible(eo, (E_Output_Hwc_Window*)target_window);
        if (!result)
          {
             ERR("hwc-opt: cannot mark target_window as visible");
             return EINA_FALSE;
          }

        _update_skip_state(eo, windows);

        /* target window is enabled, means compositor is enabled */
        ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
     }
   else
     {
        INF("hwc-opt: full composition");
        /* target window is disabled, means compositor is disabled */
        ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
     }

   INF("hwc-opt: windows state after validate:");
   _print_wnds_state(eo, windows);

   EINA_LIST_FOREACH(windows, l, window)
     {
        if (window->is_deleted) continue;
        if (e_output_hwc_window_is_target(eo, window)) continue;

        if (e_output_hwc_window_is_on_hw_overlay(eo, window))
          /* notify the window that it will be displayed on hw layer */
          e_output_hwc_window_activate(eo, window);
        else
          /* notify the window that it will be composite on the target buffer */
          e_output_hwc_window_deactivate(eo, window);
      }

     return EINA_TRUE;
}

static void
_e_output_hwc_output_commit_handler(tdm_output *output, unsigned int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec,
                                  void *user_data)
{
   const Eina_List *l;
   E_Output_Hwc_Window *window;

   E_Output *eo = (E_Output *)user_data;

   EINA_SAFETY_ON_NULL_RETURN(eo);

   EINA_LIST_FOREACH(e_output_windows_get(eo), l, window)
     {
         if (!e_output_hwc_window_commit_data_release(eo, window)) continue;
         if (e_output_hwc_window_is_video(eo, window))
           e_video_commit_data_release(window->ec, sequence, tv_sec, tv_usec);
     }

   /* 'wait_commit' is mechanism to make 'fetch and commit' no more than one time per a frame;
    * a 'page flip' happened so it's time to allow to make 'fetch and commit' for the e_output */
   eo->wait_commit = EINA_FALSE;
}

static Eina_Bool
_e_output_hwc_output_commit(E_Output *output)
{
   E_Output_Hwc_Window *window = NULL;
   Eina_List *l;
   int need_tdm_commit = 0;
   Eina_Bool fb_commit = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   if (!output->config.enabled)
     {
        WRN("E_Output disconnected");
        return EINA_FALSE;
     }

   EINA_LIST_FOREACH(output->windows, l, window)
     {
        /* an underlying hwc_window still occupies a hw overlay, so we can't
         * allow to change buffer (make fetch) as hwc_window in a client_candidate state */
        if (e_output_hwc_window_get_state(output, window) == E_OUTPUT_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
          continue;

        /* fetch the surface to the window */
        if (!e_output_hwc_window_fetch(output, window)) continue;

        if (e_output_hwc_window_is_target(output, window)) fb_commit = EINA_TRUE;

        if (output->dpms == E_OUTPUT_DPMS_OFF)
          e_output_hwc_window_offscreen_commit(output, window);
     }

   if (output->dpms == E_OUTPUT_DPMS_OFF) return EINA_TRUE;

   EINA_LIST_FOREACH(output->windows, l, window)
     {
        if (e_output_hwc_window_prepare_commit(output, window))
          need_tdm_commit = 1;

        // TODO: to be fixed. check fps of fb_target currently.
        if (fb_commit) e_output_update_fps();
     }


   if (need_tdm_commit)
     {
        tdm_error error = TDM_ERROR_NONE;

        error = tdm_output_commit(output->toutput, 0, _e_output_hwc_output_commit_handler, output);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

        output->wait_commit = EINA_TRUE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_output_hwc_window_target_render(E_Output *output, E_Output_Hwc_Window_Target *target_window)
{
    if (target_window->window.skip_flag) return EINA_TRUE;

    if (e_comp_canvas_norender_get() > 0)
      {
          return EINA_TRUE;
      }

   /* render the ecore_evas and
      update_ee is to be true at post_render_cb when the render is successful. */
   TRACE_DS_BEGIN(MANUAL RENDER);

   if (e_output_hwc_window_target_surface_queue_can_dequeue(output, target_window) || !target_window->queue)
     ecore_evas_manual_render(target_window->ee);

   TRACE_DS_END();

   return EINA_TRUE;
}

/* gets called at the beginning of an ecore_main_loop iteration */
static void
_tdm_output_need_validate_handler(tdm_output *output)
{
   INF("hwc-opt: backend asked to make the revalidation.");

   /* TODO: think how to force revalidation only for the output an event came for */
   /* TODO: maybe it'd be better to throw another job, which makes ONLY revalidation? */
   /* throw the update_job to revalidate as backend asked us */
   e_comp_render_queue();
}

EINTERN Eina_Bool
e_output_hwc_init(E_Output *output)
{
   tdm_error err;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   if (!e_output_hwc_window_init(output))
     {
        ERR("hwc_opt: E_Output_Hwc_Window init failed");
        return EINA_FALSE;
     }

   /* get backend a shot to ask us for the revalidation */
   err = tdm_output_hwc_set_need_validate_handler(output->toutput, _tdm_output_need_validate_handler);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

   INF("hwc-opt: register a need_validate_handler for the eo:%p.", output);

   return EINA_TRUE;
}

static Eina_List *
_e_output_hwc_vis_ec_list_get(E_Output *eout)
{
   Eina_List *ec_list = NULL;
   E_Client  *ec;
   Evas_Object *o;
   Eina_Bool opt_hwc; // whether an output(zona) managed by opt-hwc

   opt_hwc = e_output_hwc_opt_hwc_enabled(eout);

   // TODO: check if eout is available to use hwc policy
   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        int x, y, w, h;
        int scr_w, scr_h;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;

        if (opt_hwc)
          {
             /* skip all small clients except the video clients */
             if ((ec->w == 1 || ec->h == 1) && !(ec->comp_data && ec->comp_data->video_client))
               continue;
          }

        // check clients to skip composite
        if (e_client_util_ignored_get(ec) || (!evas_object_visible_get(ec->frame)))
          continue;

        // check geometry if located out of screen such as quick panel
        ecore_evas_geometry_get(e_comp->ee, NULL, NULL, &scr_w, &scr_h);
        if (!E_INTERSECTS(0, 0, scr_w, scr_h,
                          ec->client.x, ec->client.y, ec->client.w, ec->client.h))
          continue;

        if (evas_object_data_get(ec->frame, "comp_skip"))
          continue;

        ec_list = eina_list_append(ec_list, ec);

        // find full opaque win and excludes below wins from the visible list.
        e_client_geometry_get(ec, &x, &y, &w, &h);
        if (!E_CONTAINS(x, y, w, h,
                        0, 0, scr_w, scr_h))
           continue;

        /* for hwc optimized we need full stack of visible windows */
        if (!opt_hwc)
          if (!ec->argb)
            break;
     }

   return ec_list;
}

EINTERN Eina_Bool
e_output_hwc_re_evaluate(E_Output *output)
{
   Eina_Bool ret = EINA_FALSE;
   Eina_Bool result;
   int n_cur = 0;
   Eina_List *hwc_ok_clist = NULL, *vis_clist = NULL;
   E_Output_Hwc_Window_Target *target_window = NULL;

   INF("hwc-opt: we have something which causes to reevaluate 'E_Output_Hwc_Window to hw_layer' mapping.");

   vis_clist = _e_output_hwc_vis_ec_list_get(output);

   INF("hwc-opt: number of visible clients:%d.", eina_list_count(vis_clist));

   /* by demand of window manager to prevent some e_clients to be shown by hw directly */
   hwc_ok_clist = e_comp_filter_cl_by_wm(vis_clist, &n_cur, EINA_TRUE);

   /* mark all windows as an invisible */
   result = _hwc_re_evaluate_init(output);
   EINA_SAFETY_ON_FALSE_GOTO(result, done);

   /* if we don't have visible client we will enable target window */
   if (!hwc_ok_clist)
     {
        target_window = e_output_hwc_window_get_target_window(output);
        if (!target_window)
          {
             ERR("we don't have the target window");
             goto done;
          }

        result = e_output_hwc_window_mark_visible(output, (E_Output_Hwc_Window*)target_window);
        EINA_SAFETY_ON_FALSE_GOTO(result, done);
     }

   ret |= _hwc_prepare(output, hwc_ok_clist);

done:
   eina_list_free(hwc_ok_clist);
   eina_list_free(vis_clist);

   return ret;
}

EINTERN Eina_Bool
e_output_hwc_commit(E_Output *output)
{
   E_Output_Hwc_Window_Target *target_window;

   if (!_e_output_hwc_output_commit(output))
     ERR("fail to commit output(%p).", output);

   target_window = e_output_hwc_window_get_target_window(output);
   if (!target_window)
     {
        ERR("fail to get target window for output(%p).", output);
        return EINA_FALSE;
     }

   if (!_e_output_hwc_window_target_render(output, target_window))
     ERR("fail to render output(%p).", output);

   return EINA_TRUE;
}

EINTERN void
e_output_hwc_opt_hwc_set(E_Output *output, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(output);

   output->config.opt_hwc = set;
}

EINTERN Eina_Bool
e_output_hwc_opt_hwc_enabled(E_Output *output)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   return output->config.opt_hwc;
}