#include "e.h"


static Eina_Bool
_need_target_window(E_Output *eo)
{
   Eina_List *l;
   E_Hwc_Window *window;

   EINA_LIST_FOREACH(eo->windows, l, window)
     {
        if (window->skip_flag) continue;

        if (e_hwc_window_is_target(window)) return EINA_TRUE;

        if (!e_hwc_window_is_on_hw_overlay(window))
          return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_hwc_re_evaluate_init(E_Output *eout)
{
   const Eina_List *windows, *l;
   E_Hwc_Window *window = NULL;

   windows = e_output_windows_get(eout);
   EINA_LIST_FOREACH(windows, l, window)
     e_hwc_window_mark_unvisible(window);

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
_hwc_get_notified_about_need_unset_cc_type(const Eina_List *windows)
{
   const Eina_List *l;
   E_Hwc_Window *window;
   uint64_t delay;

   window = eina_list_data_get(windows);
   delay = _get_evas_renderer_delay(e_output_get_target_window(window->output)->queue);

   EINA_LIST_FOREACH(windows, l, window)
     if (e_hwc_window_get_state(window) == E_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
       if (!window->get_notified_about_need_unset_cc_type && !window->is_deleted)
         e_hwc_window_get_notified_about_need_unset_cc_type(window, delay);

}

static Eina_Bool
_are_e_windows_with_client_candidate_state(const Eina_List *windows)
{
   const Eina_List *l;
   E_Hwc_Window *window;

   EINA_LIST_FOREACH(windows, l, window)
     if (e_hwc_window_get_state(window) == E_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
       return EINA_TRUE;

   return EINA_FALSE;
}

static Eina_Bool
_hwc_update_e_windows_state(const Eina_List *windows)
{
   const Eina_List *l;
   E_Hwc_Window *window;

   EINA_LIST_FOREACH(windows, l, window)
     {
        if (window->is_deleted) continue;
        if (e_hwc_window_is_target(window)) continue;

        /* if an e_client got invisible or is invisible already/yet */
        if (window->skip_flag)
          {
             e_hwc_window_set_state(window, E_HWC_WINDOW_STATE_NONE);
             continue;
          }

        switch (window->type)
          {
           case TDM_COMPOSITION_CLIENT:
             e_hwc_window_set_state(window, E_HWC_WINDOW_STATE_CLIENT);
             break;

           case TDM_COMPOSITION_DEVICE:
             e_hwc_window_set_state(window, E_HWC_WINDOW_STATE_DEVICE);
             break;

           case TDM_COMPOSITION_CLIENT_CANDIDATE:
             e_hwc_window_set_state(window, E_HWC_WINDOW_STATE_CLIENT_CANDIDATE);
             break;

           case TDM_COMPOSITION_VIDEO:
             e_hwc_window_set_state(window, E_HWC_WINDOW_STATE_VIDEO);
             break;

           default:
             e_hwc_window_set_state(window, E_HWC_WINDOW_STATE_NONE);
             ERR("hwc-opt: unknown state of hwc_window.");
          }
     }

   return EINA_TRUE;
}

static const char*
_get_name_of_wnd_state(E_Hwc_Window_State window_state)
{
    switch (window_state)
    {
     case E_HWC_WINDOW_STATE_NONE:
       return "NONE";

     case E_HWC_WINDOW_STATE_CLIENT:
       return "CLIENT";

     case E_HWC_WINDOW_STATE_DEVICE:
       return "DEVICE";

     case E_HWC_WINDOW_STATE_CLIENT_CANDIDATE:
       return "CLIENT_CANDIDATE";

     case E_HWC_WINDOW_STATE_VIDEO:
       return "VIDEO";

     default:
       return "UNKNOWN";
    }
}

static int
_sort_cb(const void *d1, const void *d2)
{
   E_Hwc_Window *window_1 = (E_Hwc_Window *)d1;
   E_Hwc_Window *window_2 = (E_Hwc_Window *)d2;

   return window_1->zpos > window_2->zpos;
}

static void
_print_wnds_state(const Eina_List *wnds)
{
    const Eina_List *l;
    Eina_List *sort_wnds;
    E_Hwc_Window *window;

    sort_wnds = eina_list_clone(wnds);
    sort_wnds = eina_list_sort(sort_wnds, eina_list_count(sort_wnds), _sort_cb);

    EINA_LIST_FOREACH(sort_wnds, l, window)
      {
         if (window->skip_flag) continue;

         if (e_hwc_window_is_target(window))
           INF("hwc-opt:   ew:%p -- target_window, visible:%s", window, window->is_visible ? "yes" : "no");
         else
           INF("hwc-opt:   ew:%p -- ec:%p {name:%16s}, state:%s, visible:%s, deleted:%s, zpos:%d",
                   window, window->ec, window->ec->icccm.name, _get_name_of_wnd_state(window->state),
                   window->is_visible ? "yes" : "no", window->is_deleted ? "yes" : "no", window->zpos);
      }

    eina_list_free(sort_wnds);
}

static void
_update_skip_state(const Eina_List *wnds)
{
   const Eina_List *l;
   E_Hwc_Window *window;

   EINA_LIST_FOREACH(wnds, l, window)
     {
        if (window->is_visible)
          e_hwc_window_unset_skip_flag(window);
        else
          e_hwc_window_set_skip_flag(window);
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
   E_Hwc_Window *window;
   const Eina_List *windows;
   Eina_Bool result;
   tdm_error tdm_err;

   /* clients are sorted in reverse order */
   EINA_LIST_REVERSE_FOREACH(cl_list, l, ec)
     {
        E_Hwc_Window *window;
        window = e_output_find_window_by_ec(eo, ec);
        if (!window)
          {
             ERR("hwc-opt: cannot find the window by ec(%p)", ec);
             continue;
          }

        result = e_hwc_window_mark_visible(window);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot mark an E_Hwc_Window(%p) as visible", window);
             continue;
          }

        result = e_hwc_window_set_zpos(window, zpos);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot set zpos for E_Hwc_Window(%p)", window);
             continue;
          }

        result = e_hwc_window_update(window);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot update E_Hwc_Window(%p)", window);
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

   _update_skip_state(windows);

   INF("hwc-opt: windows state before validate:");
   _print_wnds_state(windows);

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
        E_Hwc_Window *window;

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
             window = e_output_find_window_by_hwc_win(eo, changed_hwc_window[i]);
             if (!window)
               {
                  ERR("hwc-opt: cannot find the E_Hwc_Window by hwc window");
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
   _hwc_update_e_windows_state(windows);

   if (_are_e_windows_with_client_candidate_state(windows))
     _hwc_get_notified_about_need_unset_cc_type(windows);

   if (_need_target_window(eo))
     {
        E_Hwc_Window_Target *target_window;

        INF("hwc-opt: hybrid composition");

        target_window = e_output_get_target_window(eo);
        if (!target_window)
          {
             ERR("hwc-opt: cannot get target window for output(%p)", eo);
             return EINA_FALSE;
          }

        result = e_hwc_window_mark_visible((E_Hwc_Window*)target_window);
        if (!result)
          {
             ERR("hwc-opt: cannot mark target_window as visible");
             return EINA_FALSE;
          }

        _update_skip_state(windows);

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
   _print_wnds_state(windows);

   EINA_LIST_FOREACH(windows, l, window)
     {
        if (window->is_deleted) continue;
        if (e_hwc_window_is_target(window)) continue;

        if (e_hwc_window_is_on_hw_overlay(window))
          /* notify the window that it will be displayed on hw layer */
          e_hwc_window_activate(window);
        else
          /* notify the window that it will be composite on the target buffer */
          e_hwc_window_deactivate(window);
      }

     return EINA_TRUE;
}

static void
_e_hwc_output_commit_handler(tdm_output *output, unsigned int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec,
                                  void *user_data)
{
   const Eina_List *l;
   E_Hwc_Window *window;

   E_Output *eo = (E_Output *)user_data;

   EINA_SAFETY_ON_NULL_RETURN(eo);

   EINA_LIST_FOREACH(e_output_windows_get(eo), l, window)
     {
         if (!e_hwc_window_commit_data_release(window)) continue;
         if (e_hwc_window_is_video(window))
           e_video_commit_data_release(window->ec, sequence, tv_sec, tv_usec);
     }

   /* 'wait_commit' is mechanism to make 'fetch and commit' no more than one time per a frame;
    * a 'page flip' happened so it's time to allow to make 'fetch and commit' for the e_output */
   eo->wait_commit = EINA_FALSE;
}

static Eina_Bool
_e_hwc_output_commit(E_Output *output)
{
   E_Hwc_Window *window = NULL;
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
        if (e_hwc_window_get_state(window) == E_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
          continue;

        /* fetch the surface to the window */
        if (!e_hwc_window_fetch(window)) continue;

        if (e_hwc_window_is_target(window)) fb_commit = EINA_TRUE;

        if (output->dpms == E_OUTPUT_DPMS_OFF)
          e_hwc_window_offscreen_commit(window);
     }

   if (output->dpms == E_OUTPUT_DPMS_OFF) return EINA_TRUE;

   EINA_LIST_FOREACH(output->windows, l, window)
     {
        if (e_hwc_window_prepare_commit(window))
          need_tdm_commit = 1;

        // TODO: to be fixed. check fps of fb_target currently.
        if (fb_commit) e_output_update_fps();
     }


   if (need_tdm_commit)
     {
        tdm_error error = TDM_ERROR_NONE;

        error = tdm_output_commit(output->toutput, 0, _e_hwc_output_commit_handler, output);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

        output->wait_commit = EINA_TRUE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_hwc_window_target_render(E_Hwc_Window_Target *target_window)
{
    if (target_window->window.skip_flag) return EINA_TRUE;

    if (e_comp_canvas_norender_get() > 0)
      {
          return EINA_TRUE;
      }

   /* render the ecore_evas and
      update_ee is to be true at post_render_cb when the render is successful. */
   TRACE_DS_BEGIN(MANUAL RENDER);

   if (e_hwc_window_target_surface_queue_can_dequeue(target_window) || !target_window->queue)
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


/* get backend a shot to ask us for the revalidation */
static Eina_Bool
_e_hwc_register_need_validate_handlers(Eina_List *eos)
{
   E_Output *output;
   Eina_List *l;
   tdm_error err;

   EINA_LIST_FOREACH(eos, l, output)
     {
        if (!output->config.enabled) continue;
        if (!output->config.managed_by_opt_hwc) continue;

        err = tdm_output_hwc_set_need_validate_handler(output->toutput, _tdm_output_need_validate_handler);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

        INF("hwc-opt: register a need_validate_handler for the eo:%p.", output);
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_init(void)
{
   if (!e_hwc_window_init())
     {
        ERR("hwc_opt: E_Hwc_Window init failed");
        return EINA_FALSE;
     }

   if (!_e_hwc_register_need_validate_handlers(e_comp->e_comp_screen->outputs))
     {
        ERR("hwc_opt: _e_hwc_register_need_validate_handlers failed");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_re_evaluate()
{
   Eina_List *l;
   E_Zone *zone;
   Eina_Bool ret = EINA_FALSE;
   Eina_Bool result;

   EINA_SAFETY_ON_FALSE_RETURN_VAL(e_comp->hwc, EINA_FALSE);

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        E_Output *output;
        int n_cur = 0;
        Eina_List *hwc_ok_clist = NULL, *vis_clist = NULL;

        if (!zone || !zone->output_id) continue;  // no hw layer

        output = e_output_find(zone->output_id);
        if (!output) continue;

        if (!output->config.managed_by_opt_hwc) continue;

        INF("hwc-opt: we have something which causes to reevaluate 'E_Hwc_Window to hw_layer' mapping.");

        vis_clist = e_comp_vis_ec_list_get(zone);

        INF("hwc-opt: number of visible clients:%d.", eina_list_count(vis_clist));

        /* by demand of window manager to prevent some e_clients to be shown by hw directly */
        hwc_ok_clist = e_comp_filter_cl_by_wm(vis_clist, &n_cur, EINA_TRUE);

        /* mark all windows as an invisible */
        result = _hwc_re_evaluate_init(output);
        EINA_SAFETY_ON_FALSE_GOTO(result, next_zone);

        /* if we don't have visible client we will enable target window */
        if (!hwc_ok_clist)
          {
             E_Hwc_Window_Target *target_window = NULL;

             target_window = e_output_get_target_window(output);
             if (!target_window)
               {
                  ERR("we don't have the target window");
                  goto next_zone;
               }

             result = e_hwc_window_mark_visible((E_Hwc_Window*)target_window);
             EINA_SAFETY_ON_FALSE_GOTO(result, next_zone);
          }

        ret |= _hwc_prepare(output, hwc_ok_clist);

next_zone:
        eina_list_free(hwc_ok_clist);
        eina_list_free(vis_clist);
     }

   return ret;
}

EINTERN Eina_Bool
e_hwc_commit()
{
   Eina_List *l, *ll;
   E_Comp_Screen *e_comp_screen = NULL;
   E_Output *output = NULL;

   if (!e_comp->e_comp_screen) return EINA_FALSE;

   e_comp_screen = e_comp->e_comp_screen;

   EINA_LIST_FOREACH_SAFE(e_comp_screen->outputs, l, ll, output)
     {
        E_Hwc_Window_Target *target_window;

        if (!output) continue;
        if (!output->config.enabled) continue;
        if (!output->config.managed_by_opt_hwc) continue;

        if (!_e_hwc_output_commit(output))
          ERR("fail to commit output(%p).", output);

        target_window = e_output_get_target_window(output);
        if (!target_window)
          {
             ERR("fail to get target window for output(%p).", output);
             continue;
          }

        if (!_e_hwc_window_target_render(target_window))
          ERR("fail to render output(%p).", output);
     }

   return EINA_TRUE;
}
