#include "e.h"

static Eina_Bool _hwc_available_get(E_Client *ec);

static Eina_Bool
_opt_hwc_need_target_window(E_Output *eo)
{
   Eina_List *l;
   E_Output_Hwc_Window *window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(eo->output_hwc, EINA_FALSE);

   EINA_LIST_FOREACH(eo->output_hwc->windows, l, window)
     {
        if (window->skip_flag) continue;

        if (e_output_hwc_window_is_target(window)) return EINA_TRUE;

        if (!e_output_hwc_window_is_on_hw_overlay(window))
          return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_opt_hwc_re_evaluate_init(E_Output *eout)
{
   const Eina_List *windows, *l;
   E_Output_Hwc_Window *window = NULL;

   windows = e_output_hwc_windows_get(eout->output_hwc);
   EINA_LIST_FOREACH(windows, l, window)
     e_output_hwc_window_mark_unvisible(window);

   return EINA_TRUE;
}

static uint64_t
_opt_hwc_get_evas_renderer_delay(tbm_surface_queue_h queue)
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

static E_Output_Hwc_Window_Target *
_e_output_hwc_window_get_target_window(E_Output *eout)
{
   Eina_List *l;
   E_Output_Hwc_Window *window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout->output_hwc, NULL);

   EINA_LIST_FOREACH(eout->output_hwc->windows, l, window)
     {
        if (window->is_target) return (E_Output_Hwc_Window_Target *)window;
     }

   return NULL;
}

static void
_opt_hwc_get_notified_about_need_unset_cc_type(E_Output *output, const Eina_List *windows)
{
   const Eina_List *l;
   E_Output_Hwc_Window *window;
   uint64_t delay;
   E_Output_Hwc_Window_Target *target_window = _e_output_hwc_window_get_target_window(output);

   delay = _opt_hwc_get_evas_renderer_delay(target_window->queue);

   EINA_LIST_FOREACH(windows, l, window)
     if (e_output_hwc_window_get_state(window) == E_OUTPUT_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
       if (!window->get_notified_about_need_unset_cc_type && !window->is_deleted)
         e_output_hwc_window_get_notified_about_need_unset_cc_type(window, target_window, delay);

}

static Eina_Bool
_opt_hwc_are_e_windows_with_client_candidate_state(const Eina_List *windows)
{
   const Eina_List *l;
   E_Output_Hwc_Window *window;

   EINA_LIST_FOREACH(windows, l, window)
     if (e_output_hwc_window_get_state(window) == E_OUTPUT_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
       return EINA_TRUE;

   return EINA_FALSE;
}

static Eina_Bool
_opt_hwc_update_e_windows_state(E_Output *output, const Eina_List *windows)
{
   const Eina_List *l;
   E_Output_Hwc_Window *window;

   EINA_LIST_FOREACH(windows, l, window)
     {
        if (window->is_deleted) continue;
        if (e_output_hwc_window_is_target(window)) continue;

        /* if an e_client got invisible or is invisible already/yet */
        if (window->skip_flag)
          {
             e_output_hwc_window_set_state(window, E_OUTPUT_HWC_WINDOW_STATE_NONE);
             continue;
          }

        switch (window->type)
          {
           case TDM_COMPOSITION_CLIENT:
             e_output_hwc_window_set_state(window, E_OUTPUT_HWC_WINDOW_STATE_CLIENT);
             break;

           case TDM_COMPOSITION_DEVICE:
             e_output_hwc_window_set_state(window, E_OUTPUT_HWC_WINDOW_STATE_DEVICE);
             break;

           case TDM_COMPOSITION_CLIENT_CANDIDATE:
             e_output_hwc_window_set_state(window, E_OUTPUT_HWC_WINDOW_STATE_CLIENT_CANDIDATE);
             break;

           case TDM_COMPOSITION_VIDEO:
             e_output_hwc_window_set_state(window, E_OUTPUT_HWC_WINDOW_STATE_VIDEO);
             break;

           default:
             e_output_hwc_window_set_state(window, E_OUTPUT_HWC_WINDOW_STATE_NONE);
             ERR("hwc-opt: unknown state of hwc_window.");
          }
     }

   return EINA_TRUE;
}

static const char*
_opt_hwc_get_name_of_wnd_state(E_Output_Hwc_Window_State window_state)
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
_opt_hwc_sort_cb(const void *d1, const void *d2)
{
   E_Output_Hwc_Window *window_1 = (E_Output_Hwc_Window *)d1;
   E_Output_Hwc_Window *window_2 = (E_Output_Hwc_Window *)d2;

   return window_1->zpos > window_2->zpos;
}

static void
_opt_hwc_print_wnds_state(const Eina_List *wnds)
{
    const Eina_List *l;
    Eina_List *sort_wnds;
    E_Output_Hwc_Window *window;

    sort_wnds = eina_list_clone(wnds);
    sort_wnds = eina_list_sort(sort_wnds, eina_list_count(sort_wnds), _opt_hwc_sort_cb);

    EINA_LIST_FOREACH(sort_wnds, l, window)
      {
         if (window->skip_flag) continue;

         if (e_output_hwc_window_is_target(window))
           INF("hwc-opt:   ew:%p -- target_window, visible:%s", window, window->is_visible ? "yes" : "no");
         else
           INF("hwc-opt:   ew:%p -- ec:%p {name:%16s}, state:%s, visible:%s, deleted:%s, zpos:%d",
                   window, window->ec, window->ec->icccm.name, _opt_hwc_get_name_of_wnd_state(window->state),
                   window->is_visible ? "yes" : "no", window->is_deleted ? "yes" : "no", window->zpos);
      }

    eina_list_free(sort_wnds);
}

static void
_opt_hwc_update_skip_state(const Eina_List *wnds)
{
   const Eina_List *l;
   E_Output_Hwc_Window *window;

   EINA_LIST_FOREACH(wnds, l, window)
     {
        if (window->is_visible)
          e_output_hwc_window_unset_skip_flag(window);
        else
          e_output_hwc_window_set_skip_flag(window);
     }
}

static E_Output_Hwc_Window *
_e_output_hwc_window_find_by_twin(E_Output_Hwc *output_hwc, tdm_hwc_window *hwc_win)
{
   Eina_List *l;
   E_Output_Hwc_Window *window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_win, NULL);

   EINA_LIST_FOREACH(output_hwc->windows, l, window)
     {
        if (window->hwc_wnd == hwc_win) return window;
     }

   return NULL;
}

typedef enum
{
   HWC_OPT_COMP_MODE_FULL_HWC,
   HWC_OPT_COMP_MODE_HYBRID,    /* either all windows or some are composited by sw compositor */
} _hwc_opt_comp_mode;

/* cl_list - list of e_clients that contains ALL visible e_clients for this
 * output ('eo')
 */
static Eina_Bool
_opt_hwc_hwc_prepare(E_Output *eo, Eina_List *cl_list)
{
   const Eina_List *l;
   E_Client *ec;
   uint32_t num_changes;
   int zpos = 0;
   E_Output_Hwc_Window *window;
   const Eina_List *windows;
   Eina_Bool result;
   tdm_error tdm_err;

   /* sw compositor is turned on at the start */
   static _hwc_opt_comp_mode prev_comp_mode = HWC_OPT_COMP_MODE_HYBRID;
   _hwc_opt_comp_mode comp_mode;

   /* clients are sorted in reverse order */
   EINA_LIST_REVERSE_FOREACH(cl_list, l, ec)
     {
        E_Output_Hwc_Window *window;
        window = e_output_hwc_find_window_by_ec(eo->output_hwc, ec);
        if (!window)
          {
             ERR("hwc-opt: cannot find the window by ec(%p)", ec);
             continue;
          }

        result = e_output_hwc_window_mark_visible(window);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot mark an E_Output_Hwc_Window(%p) as visible", window);
             continue;
          }

        result = e_output_hwc_window_set_zpos(window, zpos);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot set zpos for E_Output_Hwc_Window(%p)", window);
             continue;
          }

        result = e_output_hwc_window_update(window);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot update E_Output_Hwc_Window(%p)", window);
             continue;
          }
        zpos++;
     }

   windows = e_output_hwc_windows_get(eo->output_hwc);
   if (!windows)
     {
        ERR("hwc-opt: cannot get list of windows for output(%p)", eo);
        return EINA_FALSE;
     }

   _opt_hwc_update_skip_state(windows);

   INF("hwc-opt: windows state before validate:");
   _opt_hwc_print_wnds_state(windows);

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
             window = _e_output_hwc_window_find_by_twin(eo->output_hwc, changed_hwc_window[i]);
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
   _opt_hwc_update_e_windows_state(eo, windows);

   if (_opt_hwc_are_e_windows_with_client_candidate_state(windows))
     _opt_hwc_get_notified_about_need_unset_cc_type(eo, windows);

   comp_mode = _opt_hwc_need_target_window(eo) ?
           HWC_OPT_COMP_MODE_HYBRID : HWC_OPT_COMP_MODE_FULL_HWC;

   if (comp_mode == HWC_OPT_COMP_MODE_HYBRID)
     {
        E_Output_Hwc_Window_Target *target_window;

        INF("hwc-opt: hybrid composition");

        target_window = _e_output_hwc_window_get_target_window(eo);
        if (!target_window)
          {
             ERR("hwc-opt: cannot get target window for output(%p)", eo);
             return EINA_FALSE;
          }

        result = e_output_hwc_window_mark_visible((E_Output_Hwc_Window*)target_window);
        if (!result)
          {
             ERR("hwc-opt: cannot mark target_window as visible");
             return EINA_FALSE;
          }

        _opt_hwc_update_skip_state(windows);
     }
   else
     INF("hwc-opt: full hw composition");

   if (prev_comp_mode != comp_mode)
     {
        if (comp_mode == HWC_OPT_COMP_MODE_FULL_HWC)
           ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
        else if(comp_mode == HWC_OPT_COMP_MODE_HYBRID)
           ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);

        prev_comp_mode = comp_mode;
     }

   INF("hwc-opt: windows state after validate:");
   _opt_hwc_print_wnds_state(windows);

   EINA_LIST_FOREACH(windows, l, window)
     {
        if (window->is_deleted) continue;
        if (e_output_hwc_window_is_target(window)) continue;

        if (e_output_hwc_window_is_on_hw_overlay(window))
          /* notify the window that it will be displayed on hw layer */
          e_output_hwc_window_activate(window);
        else
          /* notify the window that it will be composite on the target buffer */
          e_output_hwc_window_deactivate(window);
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

   if (e_output_hwc_window_target_surface_queue_can_dequeue(target_window) || !target_window->queue)
     ecore_evas_manual_render(target_window->ee);

   TRACE_DS_END();

   return EINA_TRUE;
}

/* gets called at the beginning of an ecore_main_loop iteration */
static void
_tdm_output_need_validate_handler(tdm_output *output)
{
   INF("hwc-opt: backend asked to make the revalidation.");

   /* TODO: I'm not sure should we keep this function at all, 'cause now it's not
    * necessary - revalidate (reevaluate) will be scheduled any way (within idle_enterer),
    * so if 'reevaluation each idle_enterer' is what we want this function is useless
    * (though to remove it we have to change TDM API too) */
}

static Eina_List *
_e_output_hwc_vis_ec_list_get(E_Output_Hwc *output_hwc)
{
   Eina_List *ec_list = NULL;
   E_Client  *ec;
   Evas_Object *o;
   Eina_Bool opt_hwc; // whether an output(zona) managed by opt-hwc

   opt_hwc = e_output_hwc_opt_hwc_enabled(output_hwc);

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

/* TODO: no-opt hwc has to be forced to use this function too... */

/* filter visible clients by the window manager
 *
 * returns list of clients which are acceptable to be composited by hw,
 * it's a caller responsibility to free it
 *
 * is_output_opt_hwc - whether clients are filtered for the output
 *                                managed by opt-hwc
 *
 * for optimized hwc the returned list contains ALL clients
 */
static Eina_List *
_e_comp_filter_cl_by_wm(Eina_List *vis_cl_list, int *n_cur, Eina_Bool is_output_opt_hwc)
{
   Eina_List *hwc_acceptable_cl_list = NULL;
   Eina_List *l, *l_inner;
   E_Client *ec, *ec_inner;
   int n_ec = 0;

   if (!n_cur) return NULL;

   *n_cur = 0;

   INF("hwc-opt: filter e_clients by wm.");

   if (is_output_opt_hwc)
     {
        /* let's hope for the best... */
        EINA_LIST_FOREACH(vis_cl_list, l, ec)
        {
          ec->hwc_acceptable = EINA_TRUE;
          INF("hwc-opt: ec:%p (name:%s, title:%s) is gonna be hwc_acceptable.",
                  ec, ec->icccm.name, ec->icccm.title);
        }
     }

   EINA_LIST_FOREACH(vis_cl_list, l, ec)
     {
        // check clients not able to use hwc

        /* window manager required full GLES composition */
        if (is_output_opt_hwc && e_comp->nocomp_override > 0)
          {
             ec->hwc_acceptable = EINA_FALSE;
             INF("hwc-opt: prevent ec:%p (name:%s, title:%s) to be hwc_acceptable (nocomp_override > 0).",
                                  ec, ec->icccm.name, ec->icccm.title);
          }

        // if ec->frame is not for client buffer (e.g. launchscreen)
        if (e_comp_object_content_type_get(ec->frame) != E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE ||

            // if there is UI subfrace, it means need to composite
            e_client_normal_client_has(ec))
          {
             if (!is_output_opt_hwc) goto no_hwc;

             /* we have to let hwc know about ALL clients(buffers) in case we're using
              * optimized hwc, that's why it can be called optimized :), but also we have to provide
              * the ability for wm to prevent some clients to be shown by hw directly */
             EINA_LIST_FOREACH(vis_cl_list, l_inner, ec_inner)
               {
                  ec_inner->hwc_acceptable = EINA_FALSE;
                  INF("hwc-opt: prevent ec:%p (name:%s, title:%s) to be hwc_acceptable (UI subsurface).",
                          ec_inner, ec_inner->icccm.name, ec_inner->icccm.title);
               }
          }

        // if ec has invalid buffer or scaled( transformed ) or forced composite(never_hwc)
        if (!_hwc_available_get(ec))
          {
             if (!is_output_opt_hwc)
               {
                  if (!n_ec) goto no_hwc;
                  break;
               }

             ec->hwc_acceptable = EINA_FALSE;
             INF("hwc-opt: prevent ec:%p (name:%s, title:%s) to be hwc_acceptable.",
                     ec, ec->icccm.name, ec->icccm.title);
          }

        // listup as many as possible from the top most visible order
        n_ec++;
        if (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role)) (*n_cur)++;
        hwc_acceptable_cl_list = eina_list_append(hwc_acceptable_cl_list, ec);
     }

   return hwc_acceptable_cl_list;

no_hwc:

   eina_list_free(hwc_acceptable_cl_list);

   return NULL;
}

static Eina_Bool
_e_output_hwc_re_evaluate(E_Output_Hwc *output_hwc)
{
   Eina_Bool ret = EINA_FALSE;
   Eina_Bool result;
   int n_cur = 0;
   Eina_List *hwc_ok_clist = NULL, *vis_clist = NULL;
   E_Output_Hwc_Window_Target *target_window = NULL;
   E_Output *output = output_hwc->output;

   INF("hwc-opt: we have something which causes to reevaluate 'E_Output_Hwc_Window to hw_layer' mapping.");

   vis_clist = _e_output_hwc_vis_ec_list_get(output_hwc);

   INF("hwc-opt: number of visible clients:%d.", eina_list_count(vis_clist));

   /* by demand of window manager to prevent some e_clients to be shown by hw directly */
   hwc_ok_clist = _e_comp_filter_cl_by_wm(vis_clist, &n_cur, EINA_TRUE);

   /* mark all windows as an invisible */
   result = _opt_hwc_re_evaluate_init(output);
   EINA_SAFETY_ON_FALSE_GOTO(result, done);

   /* if we don't have visible client we will enable target window */
   if (!hwc_ok_clist)
     {
        target_window = _e_output_hwc_window_get_target_window(output);
        if (!target_window)
          {
             ERR("we don't have the target window");
             goto done;
          }

        result = e_output_hwc_window_mark_visible((E_Output_Hwc_Window*)target_window);
        EINA_SAFETY_ON_FALSE_GOTO(result, done);
     }

   ret |= _opt_hwc_hwc_prepare(output, hwc_ok_clist);

done:
   eina_list_free(hwc_ok_clist);
   eina_list_free(vis_clist);

   return ret;
}

EINTERN Eina_Bool
e_output_hwc_render(E_Output_Hwc *output_hwc)
{
   E_Output *output = output_hwc->output;
   E_Output_Hwc_Window_Target *target_window;

   target_window = _e_output_hwc_window_get_target_window(output);
   if (!target_window)
     {
        ERR("fail to get target window for output(%p).", output);
        return EINA_FALSE;
     }

   if (!_e_output_hwc_window_target_render(output, target_window))
     ERR("fail to render output(%p).", output);

   return EINA_TRUE;
}

EINTERN E_Output_Hwc_Window *
e_output_hwc_find_window_by_ec(E_Output_Hwc *output_hwc, E_Client *ec)
{
   Eina_List *l;
   E_Output_Hwc_Window *window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);

   EINA_LIST_FOREACH(output_hwc->windows, l, window)
     {
        if (window->ec == ec) return window;
     }

   return NULL;
}

EINTERN Eina_Bool
e_output_hwc_opt_hwc_enabled(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);

   return output_hwc->opt_hwc;
}

EINTERN const Eina_List *
e_output_hwc_windows_get(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, NULL);

   return output_hwc->windows;
}

static Eina_Bool
_hwc_available_get(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   E_Output *eout;
   int minw = 0, minh = 0;

   if ((!cdata) ||
       (!cdata->buffer_ref.buffer) ||
       (cdata->width_from_buffer != cdata->width_from_viewport) ||
       (cdata->height_from_buffer != cdata->height_from_viewport) ||
       cdata->never_hwc)
     {
        return EINA_FALSE;
     }

   if (e_client_transform_core_enable_get(ec)) return EINA_FALSE;

   switch (cdata->buffer_ref.buffer->type)
     {
      case E_COMP_WL_BUFFER_TYPE_NATIVE:
         break;
      case E_COMP_WL_BUFFER_TYPE_TBM:
         if (cdata->buffer_ref.buffer->resource)
           break;
      case E_COMP_WL_BUFFER_TYPE_SHM:
         if (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role))
           break;

      default:
         return EINA_FALSE;
     }

   if (e_comp_wl_tbm_buffer_sync_timeline_used(cdata->buffer_ref.buffer))
     return EINA_FALSE;

   eout = e_output_find(ec->zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);

   tdm_output_get_available_size(eout->toutput, &minw, &minh, NULL, NULL, NULL);

   if ((minw > 0) && (minw > cdata->buffer_ref.buffer->w))
     return EINA_FALSE;
   if ((minh > 0) && (minh > cdata->buffer_ref.buffer->h))
     return EINA_FALSE;

   /* If a client doesn't watch the ignore_output_transform events, we can't show
    * a client buffer to HW overlay directly when the buffer transform is not same
    * with output transform. If a client watch the ignore_output_transform events,
    * we can control client's buffer transform. In this case, we don't need to
    * check client's buffer transform here.
    */
   if (!e_comp_screen_rotation_ignore_output_transform_watch(ec))
     {
        int transform = e_comp_wl_output_buffer_transform_get(ec);

        if ((eout->config.rotation / 90) != transform)
          return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_hwc_prepare_init(E_Output_Hwc *output_hwc)
{
   const Eina_List *ep_l = NULL, *l ;
   E_Plane *ep = NULL;
   E_Output *eout = output_hwc->output;

   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   ep_l = e_output_planes_get(eout);
   EINA_LIST_FOREACH(ep_l, l, ep)
     {
        if (!output_hwc->hwc_use_multi_plane &&
            !e_plane_is_cursor(ep) &&
            !e_plane_is_fb_target(ep))
          continue;

        e_plane_ec_prepare_set(ep, NULL);
     }
}

static int
_hwc_prepare_cursor(E_Output *eout, int n_cur, Eina_List *hwc_clist)
{
   // policy for cursor layer
   const Eina_List *ep_l = NULL, *l ;
   Eina_List *cur_ly = NULL;
   E_Plane *ep = NULL;
   int n_skip = 0;
   int n_curly = 0;
   int nouse = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_clist, EINA_FALSE);

   // list up cursor only layers
   ep_l = e_output_planes_get(eout);
   EINA_LIST_FOREACH(ep_l, l, ep)
     {
        if (e_plane_is_cursor(ep))
          {
             cur_ly = eina_list_append(cur_ly, ep);
             continue;
          }
     }

   if (!cur_ly) return 0;
   n_curly = eina_list_count(cur_ly);

   if (n_cur > 0 && n_curly > 0)
     {
        if (n_cur >= n_curly) nouse = 0;
        else nouse = n_curly - n_cur;

        //assign cursor on cursor only layers
        EINA_LIST_REVERSE_FOREACH(cur_ly, l, ep)
          {
             E_Client *ec = NULL;
             if (nouse > 0)
               {
                  nouse--;
                  continue;
               }
             if (hwc_clist) ec = eina_list_data_get(hwc_clist);
             if (ec && e_plane_ec_prepare_set(ep, ec))
               {
                  n_skip += 1;
                  hwc_clist = eina_list_next(hwc_clist);
               }
          }
     }

   eina_list_free(cur_ly);

   return n_skip;
}

static Eina_Bool
_hwc_prepare(E_Output_Hwc *output_hwc, int n_vis, int n_skip, Eina_List *hwc_clist)
{
   const Eina_List *ep_l = NULL, *l ;
   Eina_List *hwc_ly = NULL;
   E_Plane *ep = NULL, *ep_fb = NULL;
   int n_ly = 0, n_ec = 0;
   E_Client *ec = NULL;
   Eina_Bool ret = EINA_FALSE;
   int nouse = 0;
   E_Output *eout = output_hwc->output;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_clist, EINA_FALSE);

   n_ec = eina_list_count(hwc_clist);
   if (n_skip > 0)
     {
        int i;
        for (i = 0; i < n_skip; i++)
          hwc_clist = eina_list_next(hwc_clist);

        n_ec -= n_skip;
        n_vis -= n_skip;
     }

   if (n_ec <= 0) return EINA_FALSE;

   // list up available_hw layers E_Client can be set
   // if e_comp->hwc_use_multi_plane FALSE, than use only fb target plane
   ep_l = e_output_planes_get(eout);
   EINA_LIST_FOREACH(ep_l, l, ep)
     {
        if (!ep_fb)
          {
             if (e_plane_is_fb_target(ep))
               {
                  ep_fb = ep;
                  hwc_ly = eina_list_append(hwc_ly, ep);
               }
             continue;
          }
        if (!output_hwc->hwc_use_multi_plane) continue;
        if (e_plane_is_cursor(ep)) continue;
        if (ep->zpos > ep_fb->zpos)
          hwc_ly = eina_list_append(hwc_ly, ep);
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_ly, EINA_FALSE);

   // finally, assign client on available_hw layers
   n_ly = eina_list_count(hwc_ly);
   if ((n_ec == n_vis) &&
       (n_ec <= n_ly)) // fully hwc
     {
        nouse = n_ly - n_ec;
     }
   else if ((n_ly < n_vis) || // e_comp->evas on fb target plane
            (n_ec < n_vis))
     {
        if (n_ec <= n_ly) nouse = n_ly - n_ec - 1;
        else nouse = 0;
     }

   EINA_LIST_REVERSE_FOREACH(hwc_ly, l, ep)
     {
        ec = NULL;
        if (nouse > 0)
          {
             nouse--;
             continue;
          }
        if (hwc_clist) ec = eina_list_data_get(hwc_clist);
        if (ec && e_plane_ec_prepare_set(ep, ec))
          {
             ret = EINA_TRUE;

             hwc_clist = eina_list_next(hwc_clist);
             n_ec--; n_vis--;
          }
        if (e_plane_is_fb_target(ep))
          {
             if (n_ec > 0 || n_vis > 0) e_plane_ec_prepare_set(ep, NULL);
             break;
          }
     }

   eina_list_free(hwc_ly);

   return ret;
}

static void
_hwc_cancel(E_Output_Hwc *output_hwc)
{
   Eina_List *l ;
   E_Plane *ep;
   E_Output *eout = output_hwc->output;

   EINA_LIST_FOREACH(eout->planes, l, ep)
     {
        if (!output_hwc->hwc_use_multi_plane &&
            !e_plane_is_cursor(ep) &&
            !e_plane_is_fb_target(ep))
             continue;

        e_plane_ec_prepare_set(ep, NULL);
        e_plane_ec_set(ep, NULL);
     }
}

static Eina_Bool
_hwc_reserved_clean(E_Output_Hwc *output_hwc)
{
   Eina_List *l;
   E_Plane *ep;
   E_Output *eout = output_hwc->output;

   EINA_LIST_FOREACH(eout->planes, l, ep)
     {
        if (!output_hwc->hwc_use_multi_plane &&
            !e_plane_is_cursor(ep) &&
            !e_plane_is_fb_target(ep))
          continue;

        if (e_plane_is_reserved(ep))
            e_plane_reserved_set(ep, 0);
     }

   return EINA_TRUE;
}

static void
_hwc_plane_unset(E_Plane *ep)
{
   if (e_plane_is_reserved(ep))
     e_plane_reserved_set(ep, 0);

   e_plane_ec_prepare_set(ep, NULL);
   e_plane_ec_set(ep, NULL);

   ELOGF("HWC", "unset plane %d to NULL", NULL, NULL, ep->zpos);
}

static Eina_Bool
_hwc_plane_change_ec(E_Plane *ep, E_Client *new_ec)
{
   if (!e_plane_ec_set(ep, new_ec))
     {
        ELOGF("HWC", "failed to set new_ec(%s) on %d",
              NULL, new_ec,
              new_ec ? (new_ec->icccm.name ? new_ec->icccm.name : "no name") : "NULL",
              ep->zpos);
        return EINA_FALSE;
     }

   if (new_ec)
     ELOGF("HWC", "new_ec(%s) is set on %d",
           new_ec->pixmap, new_ec,
           e_client_util_name_get(new_ec) ? new_ec->icccm.name : "no name", ep->zpos);
   else
     ELOGF("HWC", "NULL is set on %d", NULL, NULL, ep->zpos);

   return EINA_TRUE;
}

static void
_e_output_hwc_changed(E_Output_Hwc *output_hwc)
{
   Eina_Bool ret = EINA_FALSE;
   E_Plane *ep = NULL;
   const Eina_List *ep_l = NULL, *p_l;
   Eina_Bool assign_success = EINA_TRUE;
   int mode = E_OUTPUT_HWC_MODE_NO;
   E_Output *eout = output_hwc->output;

   ep_l = e_output_planes_get(eout);
   /* check the planes from top to down */
   EINA_LIST_REVERSE_FOREACH(ep_l, p_l, ep)
     {
        if (!assign_success)
          {
             //unset planes from 'assign_success' became EINA_FALSE to the fb target
             _hwc_plane_unset(ep);
             continue;
          }

        if (e_plane_is_reserved(ep) &&
            ep->prepare_ec == NULL)
          {
             e_plane_reserved_set(ep, 0);
             ELOGF("HWC", "unset reserved mem on %d", NULL, NULL, ep->zpos);
          }

        if (ep->ec != ep->prepare_ec)
          {
             assign_success = _hwc_plane_change_ec(ep, ep->prepare_ec);
             ret = EINA_TRUE;
          }

        if (ep->ec) mode = E_OUTPUT_HWC_MODE_HYBRID;

        if (e_plane_is_fb_target(ep))
          {
             if (ep->ec) mode = E_OUTPUT_HWC_MODE_FULL;
             break;
          }
   }

   if (output_hwc->hwc_mode != mode)
     {
        ELOGF("HWC", "mode changed (from %d to %d) due to surface changes",
              NULL, NULL,
              output_hwc->hwc_mode, mode);

        if (mode == E_OUTPUT_HWC_MODE_FULL)
          {
             // fb target is occupied by a client surface, means compositor disabled
             ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
          }
        else if (output_hwc->hwc_mode == E_OUTPUT_HWC_MODE_FULL)
          {
             // fb target is occupied by a client surface, means compositor disabled
             ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
          }

        output_hwc->hwc_mode = mode;
     }

   if (ret)
     {
        if (output_hwc->hwc_mode == E_OUTPUT_HWC_MODE_NO)
          ELOGF("HWC", " End...  due to surface changes", NULL, NULL);
        else
          ELOGF("HWC", " hwc surface changed", NULL, NULL);
     }
}

static Eina_Bool
_e_output_hwc_prepare(E_Output_Hwc *output_hwc, E_Zone *zone)
{
   Eina_List *vl;
   Eina_Bool ret = EINA_FALSE;
   E_Client *ec;
   int n_vis = 0, n_ec = 0, n_cur = 0, n_skip = 0;
   Eina_List *hwc_ok_clist = NULL, *vis_clist = NULL;
   E_Output *output = output_hwc->output;

   vis_clist = e_comp_vis_ec_list_get(zone);
   if (!vis_clist) return EINA_FALSE;

   EINA_LIST_FOREACH(vis_clist, vl, ec)
     {
        // check clients not able to use hwc
        // if ec->frame is not for client buffer (e.g. launchscreen)
        if (e_comp_object_content_type_get(ec->frame) != E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE)
           goto done;

        // if there is UI subfrace, it means need to composite
        if (e_client_normal_client_has(ec))
           goto done;

        // if ec has invalid buffer or scaled( transformed ) or forced composite(never_hwc)
        if (!_hwc_available_get(ec))
          {
             if (!n_ec) goto done;
             break;
          }

        // listup as many as possible from the top most visible order
        n_ec++;
        if (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role)) n_cur++;
        hwc_ok_clist = eina_list_append(hwc_ok_clist, ec);
     }

   n_vis = eina_list_count(vis_clist);
   if ((n_vis < 1) || (n_ec < 1))
     goto done;

   _hwc_prepare_init(output_hwc);

   if (n_cur >= 1)
     n_skip = _hwc_prepare_cursor(output, n_cur, hwc_ok_clist);

   if (n_skip > 0) ret = EINA_TRUE;

   ret |= _hwc_prepare(output_hwc, n_vis, n_skip, hwc_ok_clist);

done:
   eina_list_free(hwc_ok_clist);
   eina_list_free(vis_clist);

   return ret;
}

static Eina_Bool
_e_output_hwc_usable(E_Output_Hwc *output_hwc)
{
   E_Output *eout = output_hwc->output;
   E_Comp_Wl_Buffer *buffer = NULL;
   E_Zone *zone = NULL;
   int bw = 0, bh = 0;
   Eina_Bool all_null = EINA_TRUE;
   E_Plane *ep = NULL;
   const Eina_List *ep_l = NULL, *p_l;

   zone = e_comp_zone_find(e_output_output_id_get(eout));
   EINA_SAFETY_ON_NULL_RETURN_VAL(zone, EINA_FALSE);

   // check whether to use hwc and prepare the core assignment policy
   if (!_e_output_hwc_prepare(output_hwc, zone)) return EINA_FALSE;

   // extra policy can replace core policy
   e_comp_hook_call(E_COMP_HOOK_PREPARE_PLANE, NULL);

   // It is not hwc_usable if cursor is shown when the hw cursor is not supported by libtdm.
   if (!e_pointer_is_hidden(e_comp->pointer) &&
       (eout->cursor_available.max_w == -1 || eout->cursor_available.max_h == -1))
     return EINA_FALSE;

   // check the hwc is avaliable.
   ep_l = e_output_planes_get(eout);
   EINA_LIST_FOREACH(ep_l, p_l, ep)
     {
        if (!ep->prepare_ec) continue;

        // It is not hwc_usable if attached buffer is not valid.
        buffer = e_pixmap_resource_get(ep->prepare_ec->pixmap);
        if (!buffer) return EINA_FALSE;

        if (e_plane_is_fb_target(ep))
          {
             // It is not hwc_usable if the geometry of the prepare_ec at the ep_fb is not proper.
             e_pixmap_size_get(ep->prepare_ec->pixmap, &bw, &bh);

             // if client and zone's geometry is not match with, or
             // if plane with reserved_memory(esp. fb target) has assigned smaller buffer,
             // won't support hwc properly, than let's composite
             if (ep->reserved_memory &&
                 ((bw != zone->w) || (bh != zone->h) ||
                 (ep->prepare_ec->x != zone->x) || (ep->prepare_ec->y != zone->y) ||
                 (ep->prepare_ec->w != zone->w) || (ep->prepare_ec->h != zone->h)))
               {
                  DBG("Cannot use HWC if geometry is not 1 on 1 match with reserved_memory");
                  return EINA_FALSE;
               }
          }

        all_null = EINA_FALSE;
        break;
     }

   // It is not hwc_usable if the all prepare_ec in every plane are null
   if (all_null) return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_output_hwc_can_hwcompose(E_Output *eout)
{
   const Eina_List *ep_l = NULL, *l;
   E_Plane *ep = NULL, *ep_fb = NULL;

   ep_l = e_output_planes_get(eout);
   /* check the planes from down to top */
   EINA_LIST_FOREACH(ep_l, l, ep)
     {
        if (e_plane_is_fb_target(ep))
          {
             /* can hwcompose if fb_target has a ec. */
             if (ep->prepare_ec != NULL) return EINA_TRUE;
             else ep_fb = ep;
          }
        else
          {
             /* can hwcompose if ep has a ec and zpos is higher than ep_fb */
             if (ep->prepare_ec != NULL &&
                 ep_fb &&
                 ep->zpos > ep_fb->zpos)
               return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

static void
_e_output_hwc_begin(E_Output_Hwc *output_hwc)
{
   const Eina_List *ep_l = NULL, *l;
   E_Output *eout = output_hwc->output;
   E_Plane *ep = NULL;
   E_Output_Hwc_Mode mode = E_OUTPUT_HWC_MODE_NO;
   Eina_Bool set = EINA_FALSE;

   if (e_comp->nocomp_override > 0) return;

   if (_e_output_hwc_can_hwcompose(eout))
     {
        ep_l = e_output_planes_get(eout);

        /* set the prepare_ec to the e_plane */
        /* check the planes from top to down */
        EINA_LIST_REVERSE_FOREACH(ep_l, l , ep)
          {
             if (!ep->prepare_ec) continue;

             set = e_plane_ec_set(ep, ep->prepare_ec);
             if (!set) break;

             if (e_plane_is_fb_target(ep))
               {
                  ELOGF("HWC", "is set on fb_target( %d)", ep->prepare_ec->pixmap, ep->prepare_ec, ep->zpos);
                  mode = E_OUTPUT_HWC_MODE_FULL;

                  // fb target is occupied by a client surface, means compositor disabled
                  ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
               }
             else
               {
                  ELOGF("HWC", "is set on %d", ep->prepare_ec->pixmap, ep->prepare_ec, ep->zpos);
                  mode = E_OUTPUT_HWC_MODE_HYBRID;
               }
          }

        if (mode == E_OUTPUT_HWC_MODE_NO)
           ELOGF("HWC", " Begin is not available yet ...", NULL, NULL);
        else
           ELOGF("HWC", " Begin ...", NULL, NULL);
     }

   output_hwc->hwc_mode = mode;
}

static E_Output_Hwc_Mode
_e_output_hwc_current_hwc_mode_check(E_Output_Hwc *output_hwc)
{
   const Eina_List *ll = NULL, *l;
   E_Output *output = output_hwc->output;
   E_Plane *plane = NULL;

   /* check the planes from down to top */
   EINA_LIST_FOREACH_SAFE(output->planes, l, ll, plane)
     {
        if (!plane->ec) continue;
        if (e_plane_is_fb_target(plane)) return E_OUTPUT_HWC_MODE_FULL;

        return E_OUTPUT_HWC_MODE_HYBRID;
     }

   return E_OUTPUT_HWC_MODE_NO;
}

EINTERN void
e_output_hwc_end(E_Output_Hwc *output_hwc, const char *location)
{
   E_Output_Hwc_Mode new_mode = E_OUTPUT_HWC_MODE_NO;

   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   /* clean the reserved planes(clean the candidate ecs) */
   _hwc_reserved_clean(output_hwc);

   if (!output_hwc->hwc_mode) return;

   /* set null to the e_planes */
   _hwc_cancel(output_hwc);

   /* check the current mode */
   new_mode = _e_output_hwc_current_hwc_mode_check(output_hwc);

   if (output_hwc->hwc_mode == E_OUTPUT_HWC_MODE_FULL &&
       new_mode != E_OUTPUT_HWC_MODE_FULL)
     ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);

   output_hwc->hwc_mode = new_mode;

   ELOGF("HWC", " End...  at %s.", NULL, NULL, location);
}

EINTERN void
e_output_hwc_multi_plane_set(E_Output_Hwc *output_hwc, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   e_output_hwc_end(output_hwc, __FUNCTION__);
   output_hwc->hwc_use_multi_plane = set;

   ELOGF("HWC", "e_output_hwc_multi_plane_set : %d", NULL, NULL, set);
}

EINTERN Eina_Bool
e_output_hwc_multi_plane_get(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);

   return output_hwc->hwc_use_multi_plane;
}

EINTERN void
e_output_hwc_deactive_set(E_Output_Hwc *output_hwc, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   e_output_hwc_end(output_hwc, __FUNCTION__);
   output_hwc->hwc_deactive = set;

   ELOGF("HWC", "e_output_hwc_deactive_set : %d", NULL, NULL, set);
}

EINTERN Eina_Bool
e_output_hwc_deactive_get(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);

   return output_hwc->hwc_deactive;
}

EINTERN void
e_output_hwc_apply(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN(output_hwc);
   EINA_SAFETY_ON_NULL_RETURN(output_hwc->output);

   if (e_output_hwc_deactive_get(output_hwc))
     {
        if (output_hwc->hwc_mode != E_OUTPUT_HWC_MODE_NO)
          e_output_hwc_end(output_hwc, "deactive set.");
        return;
     }

   if (e_output_hwc_opt_hwc_enabled(output_hwc))
     {
        /* evaluate which e_output_hwc_window will be composited by hwc and wich by GLES */
        if (!_e_output_hwc_re_evaluate(output_hwc))
           ERR("fail to _e_output_hwc_re_evaluate.");
     }
   else
     {
        if (!_e_output_hwc_usable(output_hwc))
          {
             e_output_hwc_end(output_hwc, __FUNCTION__);
             return;
          }

        if (output_hwc->hwc_mode == E_OUTPUT_HWC_MODE_NO)
          _e_output_hwc_begin(output_hwc);
        else
          _e_output_hwc_changed(output_hwc);
     }
}

EINTERN E_Output_Hwc_Mode
e_output_hwc_mode_get(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, E_OUTPUT_HWC_MODE_NO);

   return output_hwc->hwc_mode;
}

EINTERN E_Output_Hwc *
e_output_hwc_new(E_Output *output)
{
   E_Output_Hwc *output_hwc = NULL;
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   output_hwc = E_NEW(E_Output_Hwc, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, NULL);

   output_hwc->output = output;

   /* HWC, in terms of E20's architecture, is a part of E20 responsible for hardware compositing
    *
    * - no-optimized HWC takes away, from the evas engine compositor, a part of the composition
    * work without an assumption was that part worthy(optimally) to be delegated to hardware;
    * - optimized HWC makes this assumption (delegate it to tdm-backend, to be exact);
    *
    * of course if the tdm-backend makes no optimization these HWCs behave equally...
    *
    * optimized and no-optimized hwc-s can so-exist together to manage different outputs;
    * as E20 may handle several outputs by different hwcs we let them both work simultaneously,
    * so we initialize both hwcs, let both hwcs reevaluate and let both hwcs make a commit;
    * one hwc handles only outputs managed by it, so other outputs are handled by the another hwc :)
    */
   if (output->tdm_hwc)
     {
        output_hwc->opt_hwc = EINA_TRUE;

        /* get backend a shot to ask us for the revalidation */
        error = tdm_output_hwc_set_need_validate_handler(output->toutput, _tdm_output_need_validate_handler);
        EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, fail);
        INF("hwc-opt: register a need_validate_handler for the eo:%p.", output);

        if (!e_output_hwc_window_init(output_hwc))
          {
             ERR("hwc_opt: E_Output_Hwc_Window init failed");
             goto fail;
          }

        /* turn on sw compositor at the start */
        ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
     }

   return output_hwc;

fail:
   if (output_hwc) E_FREE(output_hwc);

   return NULL;
}

EINTERN void
e_output_hwc_del(E_Output_Hwc *output_hwc)
{
   if (!output_hwc) return;

   E_FREE(output_hwc);
}
