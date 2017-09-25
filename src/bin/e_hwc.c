#include "e.h"


static Eina_Bool
_need_target_window(E_Output *eo)
{
   Eina_List *l;
   E_Window *window;

   EINA_LIST_FOREACH(eo->windows, l, window)
     {
        if (window->skip_flag) continue;

        if (e_window_is_target(window)) return EINA_TRUE;

        if (window->type != TDM_COMPOSITION_DEVICE)
          return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_hwc_re_evaluate_init(E_Output *eout)
{
   const Eina_List *windows = NULL, *l ;
   E_Window *win = NULL;
   Eina_Bool result;

   windows = e_output_windows_get(eout);
   EINA_LIST_FOREACH(windows, l, win)
     {
        result = e_window_set_skip_flag(win);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(result, EINA_FALSE);
     }

   return EINA_TRUE;
}

/* cl_list - list of e_clients that contains ALL visible e_clients for this
 * output ('eo')
 */
static Eina_Bool
_hwc_prepare(E_Output *eo, Eina_List *cl_list)
{
   const Eina_List *l;
   E_Client *ec;
   int zpos = 0, num_changes;
   E_Window *window;
   Eina_List *windows;
   Eina_Bool result;
   tdm_error tdm_err;

   /* clients are sorted in reverse order */
   EINA_LIST_REVERSE_FOREACH(cl_list, l, ec)
     {
        E_Window *window;
        window = e_output_find_window_by_ec(eo, ec);
        if (!window)
          {
             ERR("hwc-opt: cannot find the window by ec(%p)", ec);
             continue;
          }

        result = e_window_unset_skip_flag(window);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot unset skip flag for e_window(%p)", window);
             continue;
          }
        result = e_window_set_zpos(window, zpos);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot set zpos for e_window(%p)", window);
             continue;
          }

        result = e_window_update(window);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot update e_window(%p)", window);
             continue;
          }
        zpos++;
     }

   /* make hwc extension choose which clients will own hw overlays */
   tdm_err = tdm_output_validate(eo->toutput, &num_changes);
   if (tdm_err != TDM_ERROR_NONE)
     {
        ERR("hwc-opt: failed to validate the output(%p)", eo->toutput);
        return EINA_FALSE;
     }

   if (num_changes)
     {
        int i;
        tdm_hwc_window **changed_hwc_window = NULL;
        tdm_hwc_window_composition_t *composition_types = NULL;
        E_Window *window;

        INF("hwc-opt: hwc extension required to change composition types.");

        changed_hwc_window = E_NEW(tdm_hwc_window *, num_changes);
        EINA_SAFETY_ON_NULL_RETURN_VAL(changed_hwc_window, EINA_FALSE);

        composition_types = E_NEW(tdm_hwc_window_composition_t, num_changes);
        EINA_SAFETY_ON_NULL_RETURN_VAL(composition_types, EINA_FALSE);

        tdm_err = tdm_output_get_changed_composition_types(eo->toutput,
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
                  ERR("hwc-opt: cannot find the e_window by hwc window");
                  free(changed_hwc_window);
                  free(composition_types);
                  return EINA_FALSE;
               }

             window->type = composition_types[i];
          }

        free(changed_hwc_window);
        free(composition_types);

        tdm_err = tdm_output_accept_changes(eo->toutput);
        if (tdm_err != TDM_ERROR_NONE)
          {
             ERR("hwc-opt: failed to accept changes required by the hwc extension");
             return EINA_FALSE;
          }
     }

     if (_need_target_window(eo))
       {
          E_Window *window;

          INF("hwc-opt: hybrid composition");

          window = e_output_get_target_window(eo);
          if (!window)
            {
               ERR("hwc-opt: cannot get target window for output(%p)", eo);
               return EINA_FALSE;
            }
          result = e_window_unset_skip_flag(window);
          if (!window)
            {
               ERR("hwc-opt: cannot unset skip flag for target_window(%p)", window);
               return EINA_FALSE;
            }

          /* target window is enabled, means compositor is enabled */
          ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
       }
     else
       {
          INF("hwc-opt: full composition");
          /* target window is disabled, means compositor is disabled */
          ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
       }

     windows = e_output_windows_get(eo);
     if (!windows)
       {
          ERR("hwc-opt: cannot get list of windows for output(%p)", eo);
          return EINA_FALSE;
       }

     EINA_LIST_FOREACH(windows, l, window)
       {
          if (window->is_deleted) continue;
          if (e_window_is_target(window)) continue;

          if (window->type != TDM_COMPOSITION_DEVICE || window->skip_flag)
            /* notify the window that it will be displayed on hw layer */
            e_window_deactivate(window);
          else
            /* notify the window that it will be composite on the target buffer */
            e_window_activate(window);
       }

     return EINA_TRUE;
}

static void
_e_hwc_output_commit_hanler(tdm_output *output, unsigned int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec,
                                  void *user_data)
{
   Eina_List *l;
   E_Window *ew;

   E_Output *eo = (E_Output *)user_data;

   EINA_SAFETY_ON_NULL_RETURN(eo);

   EINA_LIST_FOREACH(e_output_windows_get(eo), l, ew)
     {
         e_window_commit_data_release(ew);
         if (e_window_is_video(ew))
           e_video_commit_data_release(ew->ec, sequence, tv_sec, tv_usec);
     }

   /* 'wait_commit' is mechanism to make 'fetch and commit' no more than one time per a frame;
    * a 'page flip' happened so it's time to allow to make 'fetch and commit' for all e_windows */
   EINA_LIST_FOREACH(e_output_windows_get(eo), l, ew)
     ew->wait_commit = EINA_FALSE;
}

static Eina_Bool
_e_hwc_output_commit(E_Output *output)
{
   E_Window *window = NULL;
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
        /* fetch the surface to the window */
        if (!e_window_fetch(window)) continue;

        if (e_window_is_target(window)) fb_commit = EINA_TRUE;

        if (output->dpms == E_OUTPUT_DPMS_OFF)
           e_window_offscreen_commit(window);
     }

   if (output->dpms == E_OUTPUT_DPMS_OFF) return EINA_TRUE;

   EINA_LIST_FOREACH(output->windows, l, window)
     {
        if (e_window_prepare_commit(window))
          need_tdm_commit = 1;

        // TODO: to be fixed. check fps of fb_target currently.
        if (fb_commit) e_output_update_fps();
     }


   if (need_tdm_commit)
     {
        tdm_error error = TDM_ERROR_NONE;

        EINA_LIST_FOREACH(output->windows, l, window)
          {
             /* if window was placed on hw layer we need to release the commit_data */
             if (!window->activated && window->display_info.tsurface)
               window->need_commit_data_release = EINA_TRUE;
             else
               window->need_commit_data_release = EINA_FALSE;
          }

        error = tdm_output_commit(output->toutput, 0, _e_hwc_output_commit_hanler, output);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_hwc_window_target_render(E_Window_Target *window_target)
{
    if (window_target->window.skip_flag) return EINA_TRUE;;

    if (e_comp_canvas_norender_get() > 0)
      {
          return EINA_TRUE;
      }

   /* render the ecore_evas and
      update_ee is to be true at post_render_cb when the render is successful. */
   TRACE_DS_BEGIN(MANUAL RENDER);

   if (e_window_target_surface_queue_can_dequeue(window_target) || !window_target->queue)
     ecore_evas_manual_render(window_target->ee);

   TRACE_DS_END();

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_init(void)
{
   if (!e_window_init())
     {
        ERR("hwc_opt: e_window init failed");
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

   INF("hwc-opt: we have something which causes to reevaluate 'e_window to hw_layer' mapping.");

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        E_Output *output;
        int n_vis = 0, n_cur = 0, n_skip = 0;
        Eina_List *hwc_ok_clist = NULL, *vis_clist = NULL;
        E_Client fake_ec;

        if (!zone || !zone->output_id) continue;  // no hw layer

        output = e_output_find(zone->output_id);
        if (!output) continue;

        vis_clist = e_comp_vis_ec_list_get(zone);

        INF("hwc-opt: number of visible clients:%d.", eina_list_count(vis_clist));

        /* by demand of window manager to prevent some e_clients to be shown by hw directly */
        hwc_ok_clist = e_comp_filter_cl_by_wm(vis_clist, &n_cur);

        INF("hwc-opt: number of clients which are gonna own hw overlays:%d.",
            eina_list_count(hwc_ok_clist));

        /* set skip flag for all window */
        result = _hwc_re_evaluate_init(output);
        EINA_SAFETY_ON_FALSE_GOTO(result, next_zone);

        /* if we don't have visible client we will enable target window */
        if (!hwc_ok_clist)
          {
             E_Window *window = NULL;

             window = e_output_get_target_window(output);
             if (!window)
               {
                  ERR("we don't have the target window");
                  goto next_zone;
               }

             result  = e_window_unset_skip_flag(window);
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
   Eina_List *l, *ll, *l_windows;
   E_Comp_Screen *e_comp_screen = NULL;
   E_Output *output = NULL;
   E_Window *window;

   if (!e_comp->e_comp_screen) return EINA_FALSE;

   e_comp_screen = e_comp->e_comp_screen;

   EINA_LIST_FOREACH_SAFE(e_comp_screen->outputs, l, ll, output)
     {
        E_Window_Target *window_target;

        if (!output) continue;
        if (!output->config.enabled) continue;

        if (!_e_hwc_output_commit(output))
          ERR("fail to commit output.", output);

        window_target = e_output_get_target_window(output);
        if (!window_target)
          {
             ERR("fail to get target window for output(%p).", output);
             continue;
          }

        if (!_e_hwc_window_target_render(window_target))
          ERR("fail to render output(%p).", output);

     }

   return EINA_TRUE;
}
