#include "e.h"
#include "services/e_service_quickpanel.h"

typedef enum
{
   HWC_OPT_COMP_MODE_FULL_HWC,
   HWC_OPT_COMP_MODE_HYBRID,    /* either all hwc_windows or some are composited by sw compositor */
} _hwc_opt_comp_mode;

static void
_e_output_hwc_canvas_render_flush_post(void *data EINA_UNUSED, Evas *e EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Output_Hwc *output_hwc = (E_Output_Hwc *)data;
   E_Hwc_Window_Target *target_hwc_window = output_hwc->target_hwc_window;

   target_hwc_window->post_render_flush_cnt--;
   ELOGF("HWC-OPT", "[soolim] render_flush_post -- the target_hwc_window(%p) post_render_flush_cnt(%d) e_comp->evas(%p) evas(%p)",
           NULL, NULL, target_hwc_window, target_hwc_window->post_render_flush_cnt, e_comp->evas, e);
}

static Eina_Bool
_e_output_hwc_ec_check(E_Client *ec)
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

static Eina_Bool
_e_output_hwc_windows_need_target_hwc_window(E_Output_Hwc *output_hwc)
{
   Eina_List *l;
   E_Hwc_Window *hwc_window;

   EINA_LIST_FOREACH(output_hwc->hwc_windows, l, hwc_window)
     {
        if (hwc_window->is_excluded) continue;

        if (e_hwc_window_is_target(hwc_window)) return EINA_TRUE;

        if (!e_hwc_window_is_on_hw_overlay(hwc_window))
          return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_output_hwc_windows_exclude_all_hwc_windows(E_Output *eout)
{
   const Eina_List *hwc_windows, *l;
   E_Hwc_Window *hwc_window = NULL;

   hwc_windows = e_output_hwc_windows_get(eout->output_hwc);
   EINA_LIST_FOREACH(hwc_windows, l, hwc_window)
     {
        if (e_hwc_window_is_video(hwc_window))
          continue;
        hwc_window->is_excluded = EINA_TRUE;
        tdm_hwc_window_set_composition_type(hwc_window->hwc_wnd, TDM_COMPOSITION_NONE);
     }

   return EINA_TRUE;
}

static uint64_t
_e_output_hwc_windows_get_evas_renderer_delay(tbm_surface_queue_h queue)
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
_e_output_hwc_windows_get_notified_about_need_unset_cc_type(E_Output_Hwc *output_hwc)
{
   const Eina_List *l;
   E_Hwc_Window *hwc_window;
   uint64_t delay;
   E_Hwc_Window_Target *target_hwc_window = output_hwc->target_hwc_window;

   delay = _e_output_hwc_windows_get_evas_renderer_delay(target_hwc_window->queue);

   EINA_LIST_FOREACH(output_hwc->hwc_windows, l, hwc_window)
     if (e_hwc_window_get_state(hwc_window) == E_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
       if (!hwc_window->get_notified_about_need_unset_cc_type && !hwc_window->is_deleted)
         e_hwc_window_get_notified_about_need_unset_cc_type(hwc_window, target_hwc_window, delay);

}

static Eina_Bool
_e_output_hwc_windows_candidate_state_check(E_Output_Hwc *output_hwc)
{
   const Eina_List *l;
   E_Hwc_Window *hwc_window;

   EINA_LIST_FOREACH(output_hwc->hwc_windows, l, hwc_window)
     if (e_hwc_window_get_state(hwc_window) == E_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
       return EINA_TRUE;

   return EINA_FALSE;
}

static Eina_Bool
_e_output_hwc_windows_states_update(E_Output_Hwc *output_hwc)
{
   const Eina_List *l;
   E_Hwc_Window *hwc_window;

   const Eina_List *hwc_windows = e_output_hwc_windows_get(output_hwc);

   EINA_LIST_FOREACH(hwc_windows, l, hwc_window)
     {
        if (hwc_window->is_deleted) continue;
        if (e_hwc_window_is_target(hwc_window)) continue;

        /* if an e_client got invisible or is invisible already/yet */
        if (hwc_window->is_excluded)
          {
             e_hwc_window_set_state(hwc_window, E_HWC_WINDOW_STATE_NONE);
             continue;
          }

        switch (hwc_window->type)
          {
           case TDM_COMPOSITION_CLIENT:
             e_hwc_window_set_state(hwc_window, E_HWC_WINDOW_STATE_CLIENT);
             break;

           case TDM_COMPOSITION_DEVICE:
             e_hwc_window_set_state(hwc_window, E_HWC_WINDOW_STATE_DEVICE);
             break;

           case TDM_COMPOSITION_CLIENT_CANDIDATE:
             e_hwc_window_set_state(hwc_window, E_HWC_WINDOW_STATE_CLIENT_CANDIDATE);
             break;

           case TDM_COMPOSITION_DEVICE_CANDIDATE:
             e_hwc_window_set_state(hwc_window, E_HWC_WINDOW_STATE_DEVICE_CANDIDATE);
             break;

           default:
             e_hwc_window_set_state(hwc_window, E_HWC_WINDOW_STATE_NONE);
             ERR("hwc-opt: unknown state of hwc_window.");
          }
     }

   return EINA_TRUE;
}

static const char*
_e_output_hwc_windows_get_name_of_wnd_state(E_Hwc_Window_State hwc_window_state)
{
    switch (hwc_window_state)
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

     case E_HWC_WINDOW_STATE_DEVICE_CANDIDATE:
       return "DEVICE_CANDIDATE";

     default:
       return "UNKNOWN";
    }
}

static int
_e_output_hwc_windows_sort_cb(const void *d1, const void *d2)
{
   E_Hwc_Window *hwc_window_1 = (E_Hwc_Window *)d1;
   E_Hwc_Window *hwc_window_2 = (E_Hwc_Window *)d2;

   if (!hwc_window_1) return(-1);
   if (!hwc_window_2) return(1);

   return (hwc_window_2->zpos - hwc_window_1->zpos);
}

static void
_e_output_hwc_windows_print_wnds_state(E_Output_Hwc *output_hwc)
{
    const Eina_List *l;
    Eina_List *sort_wnds;
    E_Hwc_Window *hwc_window;

    sort_wnds = eina_list_clone(output_hwc->hwc_windows);
    sort_wnds = eina_list_sort(sort_wnds, eina_list_count(sort_wnds), _e_output_hwc_windows_sort_cb);

    EINA_LIST_FOREACH(sort_wnds, l, hwc_window)
      {
         if (hwc_window->is_excluded) continue;

         if (e_hwc_window_is_target(hwc_window))
           ELOGF("HWC-OPT", "ew:%p -- target_hwc_window, type:%d",
                 NULL, NULL, hwc_window, hwc_window->type);
         else
           ELOGF("HWC-OPT", "ew:%p -- {name:%25s, title:%25s}, state:%s, deleted:%s, zpos:%d",
                 hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
                 hwc_window, hwc_window->ec ? hwc_window->ec->icccm.name : "UNKNOWN",
                 hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
                 _e_output_hwc_windows_get_name_of_wnd_state(hwc_window->state),
                 hwc_window->is_deleted ? "yes" : "no", hwc_window->zpos);
      }

    eina_list_free(sort_wnds);
}

static E_Hwc_Window *
_e_output_hwc_windows_window_find_by_twin(E_Output_Hwc *output_hwc, tdm_hwc_window *hwc_win)
{
   Eina_List *l;
   E_Hwc_Window *hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_win, NULL);

   EINA_LIST_FOREACH(output_hwc->hwc_windows, l, hwc_window)
     {
        if (hwc_window->hwc_wnd == hwc_win) return hwc_window;
     }

   return NULL;
}

static Eina_Bool
_e_output_hwc_windows_unset_cc_type_for_all_unvis_hwc_windows(E_Output *eout)
{
   const Eina_List *hwc_windows, *l;
   E_Hwc_Window *hwc_window = NULL;

   hwc_windows = e_output_hwc_windows_get(eout->output_hwc);
   EINA_LIST_FOREACH(hwc_windows, l, hwc_window)
     {
        if (hwc_window->is_excluded &&
            e_hwc_window_get_state(hwc_window) == E_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
          {
             /* reset for the next DEVICE -> CLIENT_CANDIDATE transition */
             hwc_window->got_composited = EINA_FALSE;
             hwc_window->need_unset_cc_type = EINA_FALSE;
             hwc_window->get_notified_about_need_unset_cc_type = EINA_FALSE;

             tdm_hwc_window_set_composition_type(hwc_window->hwc_wnd, TDM_COMPOSITION_CLIENT);
             tdm_hwc_window_set_composition_type(hwc_window->hwc_wnd, TDM_COMPOSITION_NONE);

             hwc_window->type = TDM_COMPOSITION_NONE;
          }
     }

   return EINA_TRUE;
}

// cl_list - list of e_clients that contains ALL visible e_clients for this output ('eo')
 static Eina_Bool
_e_output_hwc_windows_prepare(E_Output_Hwc *output_hwc, Eina_List *cl_list)
{
   const Eina_List *l;
   E_Client *ec;
   uint32_t num_changes;
   int zpos = 0;
   E_Hwc_Window *hwc_window;
   Eina_Bool result;
   tdm_error tdm_err;
   E_Output *eo = output_hwc->output;
   tdm_output *toutput = eo->toutput;

   /* sw compositor is turned on at the start */
   static _hwc_opt_comp_mode prev_comp_mode = HWC_OPT_COMP_MODE_HYBRID;
   _hwc_opt_comp_mode comp_mode;

   /* clients are sorted in reverse order */
   EINA_LIST_REVERSE_FOREACH(cl_list, l, ec)
     {
        hwc_window = ec->hwc_window;
        if (!hwc_window)
          {
             ERR("hwc-opt: cannot find the hwc_window by ec(%p)", ec);
             continue;
          }

        hwc_window->is_excluded = EINA_FALSE;

        result = e_hwc_window_set_zpos(hwc_window, zpos);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot set zpos for E_Hwc_Window(%p)", hwc_window);
             continue;
          }

        result = e_hwc_window_update(hwc_window);
        if (result != EINA_TRUE)
          {
             ERR("hwc-opt: cannot update E_Hwc_Window(%p)", hwc_window);
             continue;
          }
        zpos++;
     }

   /* FIXME: it is quick fix for the TDM_COMPOSITION_CANDIDATE_CLIENT type freezing
    * in the invisible hwc_windows. The CANDIDATE_CLIENT logic will be reworked and
    * this kludge function removed */
   _e_output_hwc_windows_unset_cc_type_for_all_unvis_hwc_windows(output_hwc->output);

   /* to keep a state of e_hwc_windows up to date we have to update their states
    * according to the changes wm and/or hw made */
   _e_output_hwc_windows_states_update(output_hwc);

   ELOGF("HWC-OPT", "Request HWC Validation to TDM HWC:", NULL, NULL);
   _e_output_hwc_windows_print_wnds_state(output_hwc);

   /* make hwc extension choose which clients will own hw overlays */
   tdm_err = tdm_output_hwc_validate(toutput, &num_changes);
   if (tdm_err != TDM_ERROR_NONE)
     {
        ERR("hwc-opt: failed to validate the output(%p)", toutput);
        return EINA_FALSE;
     }

   if (num_changes)
     {
        int i;
        tdm_hwc_window **changed_hwc_window = NULL;
        tdm_hwc_window_composition *composition_types = NULL;

        ELOGF("HWC-OPT", "TDM HWC required to change composition types.", NULL, NULL);

        changed_hwc_window = E_NEW(tdm_hwc_window *, num_changes);
        EINA_SAFETY_ON_NULL_RETURN_VAL(changed_hwc_window, EINA_FALSE);

        composition_types = E_NEW(tdm_hwc_window_composition, num_changes);
        EINA_SAFETY_ON_NULL_RETURN_VAL(composition_types, EINA_FALSE);

        tdm_err = tdm_output_hwc_get_changed_composition_types(toutput,
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
             hwc_window = _e_output_hwc_windows_window_find_by_twin(output_hwc, changed_hwc_window[i]);
             if (!hwc_window)
               {
                  ERR("hwc-opt: cannot find the E_Hwc_Window by hwc hwc_window");
                  free(changed_hwc_window);
                  free(composition_types);
                  return EINA_FALSE;
               }

             hwc_window->type = composition_types[i];
          }

        free(changed_hwc_window);
        free(composition_types);

        tdm_err = tdm_output_hwc_accept_changes(toutput);
        if (tdm_err != TDM_ERROR_NONE)
          {
             ERR("hwc-opt: failed to accept changes required by the hwc extension");
             return EINA_FALSE;
          }

        /* to keep a state of e_hwc_windows up to date we have to update their states
         * according to the changes wm and/or hw made */
        _e_output_hwc_windows_states_update(output_hwc);

        ELOGF("HWC-OPT", "Modified after HWC Validation:", NULL, NULL);
        _e_output_hwc_windows_print_wnds_state(output_hwc);
     }

   if (_e_output_hwc_windows_candidate_state_check(output_hwc))
     _e_output_hwc_windows_get_notified_about_need_unset_cc_type(output_hwc);

   comp_mode = _e_output_hwc_windows_need_target_hwc_window(output_hwc) ?
           HWC_OPT_COMP_MODE_HYBRID : HWC_OPT_COMP_MODE_FULL_HWC;

   if (comp_mode == HWC_OPT_COMP_MODE_HYBRID)
     {
        E_Hwc_Window_Target *target_hwc_window;

        ELOGF("HWC-OPT", "HWC_MODE is HYBRID composition.", NULL, NULL);

        target_hwc_window = output_hwc->target_hwc_window;
        if (!target_hwc_window)
          {
             ERR("hwc-opt: cannot get target hwc_window for output(%p)", output_hwc->output);
             return EINA_FALSE;
          }

        hwc_window = (E_Hwc_Window*)target_hwc_window;

        hwc_window->is_excluded = EINA_FALSE;
        /* don't change a composition type for the e_target_hwc_window here, 'cause
         * such action makes us to call tdm_output_validate() again;
         *
         * a hwc_window owned by e_target_hwc_window object is the fake hwc_window,
         * which is used only to force tdm provide us fb_target_window, so if got here
         * it means that tdm decided to use fb_target_window yet, this choice can be
         * considered like if we set a CLIENT composition type for the hwc_window */
     }
   else
     ELOGF("HWC-OPT", "HWC_MODE is HYBRID composition.", NULL, NULL);

   if (prev_comp_mode != comp_mode)
     {
        if (comp_mode == HWC_OPT_COMP_MODE_FULL_HWC)
           ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
        else if(comp_mode == HWC_OPT_COMP_MODE_HYBRID)
           ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);

        prev_comp_mode = comp_mode;
     }

   /* mark the active/deactive on hwc_window */
   EINA_LIST_FOREACH(e_output_hwc_windows_get(output_hwc), l, hwc_window)
     {
        if (hwc_window->is_deleted) continue;
        if (e_hwc_window_is_target(hwc_window)) continue;

        if (e_hwc_window_is_on_hw_overlay(hwc_window))
          /* notify the hwc_window that it will be displayed on hw layer */
          e_hwc_window_activate(hwc_window);
        else
          /* notify the hwc_window that it will be composite on the target buffer */
          e_hwc_window_deactivate(hwc_window);
      }

     return EINA_TRUE;
}

static Eina_Bool
_e_output_hwc_windows_target_window_render(E_Output *output, E_Hwc_Window_Target *target_hwc_window)
{
    if (target_hwc_window->hwc_window.is_excluded) return EINA_TRUE;

    if (e_comp_canvas_norender_get() > 0)
      {
          return EINA_TRUE;
      }

   /* render the ecore_evas and
      update_ee is to be true at post_render_cb when the render is successful. */
   TRACE_DS_BEGIN(MANUAL RENDER);

   if (e_hwc_window_target_surface_queue_can_dequeue(target_hwc_window) || !target_hwc_window->queue)
     {
        ELOGF("HWC-OPT", "[soolim] before manual_render the target_hwc_window(%p) post_render_flush_cnt(%d)", NULL, NULL, target_hwc_window, target_hwc_window->post_render_flush_cnt);
        ecore_evas_manual_render(target_hwc_window->ee);
        ELOGF("HWC-OPT", "[soolim] after  manual_render the target_hwc_window(%p) post_render_flush_cnt(%d)", NULL, NULL, target_hwc_window, target_hwc_window->post_render_flush_cnt);
     }

   TRACE_DS_END();

   return EINA_TRUE;
}

/* gets called at the beginning of an ecore_main_loop iteration */
static void
_e_output_hwc_windows_need_validate_handler(tdm_output *output)
{
   ELOGF("HWC-OPT", "backend asked to make the revalidation.", NULL, NULL);

   /* TODO: I'm not sure should we keep this function at all, 'cause now it's not
    * necessary - revalidate (reevaluate) will be scheduled any way (within idle_enterer),
    * so if 'reevaluation each idle_enterer' is what we want this function is useless
    * (though to remove it we have to change TDM API too) */
}

static Eina_List *
_e_output_hwc_windows_vis_ec_list_get(E_Output_Hwc *output_hwc)
{
   Eina_List *ec_list = NULL;
   E_Client  *ec;
   Evas_Object *o;
   int x, y, w, h;
   int scr_w, scr_h;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;

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

        /* skip all small clients except the video clients */
        if ((ec->w == 1 || ec->h == 1) && !e_hwc_window_is_video(ec->hwc_window))
          continue;

        ec_list = eina_list_append(ec_list, ec);
     }

   return ec_list;
}

/* TODO: no-opt hwc has to be forced to use this function too... */

/* filter visible clients by the hwc_window manager
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
_e_output_hwc_windows_filter_cl_by_wm(Eina_List *vis_cl_list)
{
   Eina_List *hwc_acceptable_cl_list = NULL;
   Eina_List *l, *l_inner;
   E_Client *ec, *ec_inner;
   E_Hwc_Window *hwc_window = NULL;

   /* let's hope for the best... */
   EINA_LIST_FOREACH(vis_cl_list, l, ec)
     {
        hwc_window = ec->hwc_window;
        hwc_window->hwc_acceptable = EINA_TRUE;
     }

   EINA_LIST_FOREACH(vis_cl_list, l, ec)
     {
        // check clients not able to use hwc

        /* hwc_window manager required full GLES composition */
        if (e_comp->nocomp_override > 0)
          {
             hwc_window = ec->hwc_window;
             hwc_window->hwc_acceptable = EINA_FALSE;
             ELOGF("HWC-OPT", "Prevent (name:%s, title:%s) to be hwc_acceptable (nocomp_override > 0).",
                   ec->pixmap, ec, ec->icccm.name, ec->icccm.title);
          }
//TODO: we have to find what to do in this case.??
//      we have to set the nocomp_override in this case below...(quickpanel case...)
#if 0
        // if there is a ec which is lower than quickpanel and quickpanel is opened.
        if (E_POLICY_QUICKPANEL_LAYER >= evas_object_layer_get(ec->frame))
          {
             // check whether quickpanel is open than break
             if (e_qp_visible_get()) goto done;
          }
#endif
        // if ec->frame is not for client buffer (e.g. launchscreen)
        if (e_comp_object_content_type_get(ec->frame) != E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE ||

            // if there is UI subfrace, it means need to composite
            e_client_normal_client_has(ec))
          {
             /* we have to let hwc know about ALL clients(buffers) in case we're using
              * optimized hwc, that's why it can be called optimized :), but also we have to provide
              * the ability for wm to prevent some clients to be shown by hw directly */
             EINA_LIST_FOREACH(vis_cl_list, l_inner, ec_inner)
               {
                  hwc_window = ec_inner->hwc_window;
                  hwc_window->hwc_acceptable = EINA_FALSE;
                  ELOGF("HWC-OPT", "Prevent (name:%s, title:%s) to be hwc_acceptable (UI subsurface).",
                        ec_inner->pixmap, ec_inner, ec_inner->icccm.name, ec_inner->icccm.title);
               }
          }

        // if ec has invalid buffer or scaled( transformed ) or forced composite(never_hwc)
        if (!_e_output_hwc_ec_check(ec))
          {
             hwc_window = ec->hwc_window;
             hwc_window->hwc_acceptable = EINA_FALSE;
             ELOGF("HWC-OPT", "Prevent (name:%s, title:%s) to be hwc_acceptable.",
                   ec->pixmap, ec, ec->icccm.name, ec->icccm.title);
          }

        if (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role))
          {
             hwc_window = ec->hwc_window;
             hwc_window->hwc_acceptable = EINA_FALSE;
             ELOGF("HWC-OPT", "Prevent (name:%s, title:%s) to be hwc_acceptable.(Cursor)",
                   ec->pixmap, ec, ec->icccm.name, ec->icccm.title);
          }

        // listup as many as possible from the top most visible order
        hwc_acceptable_cl_list = eina_list_append(hwc_acceptable_cl_list, ec);
     }

   return hwc_acceptable_cl_list;
}

static Eina_Bool
_e_output_hwc_windows_is_video_window(E_Output_Hwc *output_hwc)
{
   Eina_List *l;
   E_Hwc_Window *hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);

   EINA_LIST_FOREACH(output_hwc->hwc_windows, l, hwc_window)
     if (e_hwc_window_is_video(hwc_window))
       return EINA_TRUE;

   return EINA_FALSE;
}

static Eina_Bool
_e_output_hwc_windows_re_evaluate(E_Output_Hwc *output_hwc)
{
   Eina_Bool ret = EINA_FALSE;
   Eina_Bool result;
   Eina_List *hwc_ok_clist = NULL, *vis_clist = NULL;
   E_Hwc_Window_Target *target_hwc_window = NULL;
   E_Output *output = output_hwc->output;
   tdm_error err;

   ELOGF("HWC-OPT", "=== Output HWC Apply (evaluate) ===", NULL, NULL);

   vis_clist = _e_output_hwc_windows_vis_ec_list_get(output_hwc);

   ELOGF("HWC-OPT", "The number of visible clients:%d.", NULL, NULL, eina_list_count(vis_clist));

   /* by demand of hwc_window manager to prevent some e_clients to be shown by hw directly */
   hwc_ok_clist = _e_output_hwc_windows_filter_cl_by_wm(vis_clist);

   /* exclude all hwc_windows from being considered by hwc */
   result = _e_output_hwc_windows_exclude_all_hwc_windows(output);
   EINA_SAFETY_ON_FALSE_GOTO(result, done);

   /* if we don't have visible client or we have video hwc_window we will
    * enable target hwc_window */
   if (!hwc_ok_clist || _e_output_hwc_windows_is_video_window(output_hwc))
     {
        E_Hwc_Window *hwc_window;

        target_hwc_window = output_hwc->target_hwc_window;
        if (!target_hwc_window)
          {
             ERR("we don't have the target hwc_window");
             goto done;
          }

        hwc_window = (E_Hwc_Window*)target_hwc_window;

        hwc_window->is_excluded = EINA_FALSE;
        err = tdm_hwc_window_set_composition_type(hwc_window->hwc_wnd, TDM_COMPOSITION_CLIENT);
        if (err != TDM_ERROR_NONE)
          {
             ERR("fail to set CLIENT composition type for target_window");
             goto done;
          }
     }

   ret = _e_output_hwc_windows_prepare(output_hwc, hwc_ok_clist);

done:
   eina_list_free(hwc_ok_clist);
   eina_list_free(vis_clist);

   return ret;
}


static void
_e_output_hwc_windows_update_fps()
{
   static double time = 0.0;
   static double lapse = 0.0;
   static int cframes = 0;
   static int flapse = 0;

   if (e_comp->calc_fps)
     {
        double dt;
        double tim = ecore_time_get();

        dt = tim - e_comp->frametimes[0];
        e_comp->frametimes[0] = tim;

        time += dt;
        cframes++;

        if (lapse == 0.0)
          {
             lapse = tim;
             flapse = cframes;
          }
        else if ((tim - lapse) >= 0.5)
          {
             e_comp->fps = (cframes - flapse) / (tim - lapse);
             lapse = tim;
             flapse = cframes;
             time = 0.0;
          }
     }
}

static void
_e_output_hwc_windows_commit_handler(tdm_output *toutput, unsigned int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec,
                                  void *user_data)
{
   const Eina_List *l;
   E_Hwc_Window *window;
   E_Output *output = NULL;
   E_Output_Hwc *output_hwc = (E_Output_Hwc *)user_data;

   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   output = output_hwc->output;

   EINA_LIST_FOREACH(e_output_hwc_windows_get(output_hwc), l, window)
     {
         if (!e_hwc_window_commit_data_release(window)) continue;
         if (e_hwc_window_is_video(window))
           e_video_commit_data_release(window->ec, sequence, tv_sec, tv_usec);
     }

   /* 'wait_commit' is mechanism to make 'fetch and commit' no more than one time per a frame;
    * a 'page flip' happened so it's time to allow to make 'fetch and commit' for the e_output */
   output->wait_commit = EINA_FALSE;
}

/* we can do commit if we set surface at least to one window which displayed on
 * the hw layer*/
static Eina_Bool
_e_output_hwc_windows_can_commit(E_Output *output)
{
   Eina_List *l;
   E_Hwc_Window *hwc_window;
   Eina_Bool can_commit = EINA_TRUE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output->output_hwc, EINA_FALSE);

   EINA_LIST_FOREACH(output->output_hwc->hwc_windows, l, hwc_window)
     {
        if (!e_hwc_window_is_on_hw_overlay(hwc_window)) continue;

        if (!hwc_window->tsurface) can_commit = EINA_FALSE;
     }

   return can_commit;
}

static Eina_Bool
_e_output_hwc_windows_prepare_commit(E_Output *output, E_Hwc_Window *hwc_window)
{
   if (output->wait_commit) return EINA_FALSE;

   if (!e_hwc_window_commit_data_aquire(hwc_window))
     return EINA_FALSE;

   /* send frame event enlightenment dosen't send frame evnet in nocomp */
   if (hwc_window->ec)
     e_pixmap_image_clear(hwc_window->ec->pixmap, 1);

   return EINA_TRUE;
}

static Eina_Bool
_e_output_hwc_windows_offscreen_commit(E_Output *output, E_Hwc_Window *hwc_window)
{
   if (!e_hwc_window_commit_data_aquire(hwc_window))
     return EINA_FALSE;

   /* send frame event enlightenment doesn't send frame event in nocomp */
   if (hwc_window->ec)
     e_pixmap_image_clear(hwc_window->ec->pixmap, 1);

   e_hwc_window_commit_data_release(hwc_window);

   return EINA_TRUE;
}

static void
_e_output_hwc_planes_prepare_init(E_Output_Hwc *output_hwc)
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
_e_output_hwc_planes_prepare_cursor(E_Output *eout, int n_cur, Eina_List *hwc_clist)
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
_e_output_hwc_planes_prepare_plane(E_Output_Hwc *output_hwc, int n_vis, int n_skip, Eina_List *hwc_clist)
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
_e_output_hwc_planes_cancel(E_Output_Hwc *output_hwc)
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
_e_output_hwc_planes_reserved_clean(E_Output_Hwc *output_hwc)
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
_e_output_hwc_planes_unset(E_Plane *ep)
{
   if (e_plane_is_reserved(ep))
     e_plane_reserved_set(ep, 0);

   e_plane_ec_prepare_set(ep, NULL);
   e_plane_ec_set(ep, NULL);

   ELOGF("HWC", "unset plane %d to NULL", NULL, NULL, ep->zpos);
}

static Eina_Bool
_e_output_hwc_planes_change_ec(E_Plane *ep, E_Client *new_ec)
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
_e_output_hwc_planes_changed(E_Output_Hwc *output_hwc)
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
             _e_output_hwc_planes_unset(ep);
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
             assign_success = _e_output_hwc_planes_change_ec(ep, ep->prepare_ec);
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
_e_output_hwc_planes_prepare(E_Output_Hwc *output_hwc, E_Zone *zone)
{
   Eina_List *vl;
   Eina_Bool ret = EINA_FALSE;
   E_Client *ec;
   int n_vis = 0, n_ec = 0, n_cur = 0, n_skip = 0;
   Eina_List *hwc_ok_clist = NULL, *vis_clist = NULL;
   E_Output *output = output_hwc->output;

   vis_clist = e_comp_vis_ec_list_get(zone);
   if (!vis_clist) return EINA_FALSE;

   // check clients not able to use hwc
   EINA_LIST_FOREACH(vis_clist, vl, ec)
     {
        // if there is a ec which is lower than quickpanel and quickpanel is opened.
        if (E_POLICY_QUICKPANEL_LAYER >= evas_object_layer_get(ec->frame))
          {
             // check whether quickpanel is open than break
             if (e_qp_visible_get()) goto done;
          }

        // if ec->frame is not for client buffer (e.g. launchscreen)
        if (e_comp_object_content_type_get(ec->frame) != E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE)
           goto done;

        // if there is UI subfrace, it means need to composite
        if (e_client_normal_client_has(ec))
           goto done;

        // if ec has invalid buffer or scaled( transformed ) or forced composite(never_hwc)
        if (!_e_output_hwc_ec_check(ec))
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

   _e_output_hwc_planes_prepare_init(output_hwc);

   if (n_cur >= 1)
     n_skip = _e_output_hwc_planes_prepare_cursor(output, n_cur, hwc_ok_clist);

   if (n_skip > 0) ret = EINA_TRUE;

   ret |= _e_output_hwc_planes_prepare_plane(output_hwc, n_vis, n_skip, hwc_ok_clist);

done:
   eina_list_free(hwc_ok_clist);
   eina_list_free(vis_clist);

   return ret;
}

static Eina_Bool
_e_output_hwc_planes_usable(E_Output_Hwc *output_hwc)
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
   if (!_e_output_hwc_planes_prepare(output_hwc, zone)) return EINA_FALSE;

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
_e_output_hwc_planes_can_hwcompose(E_Output *eout)
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
_e_output_hwc_planes_begin(E_Output_Hwc *output_hwc)
{
   const Eina_List *ep_l = NULL, *l;
   E_Output *eout = output_hwc->output;
   E_Plane *ep = NULL;
   E_Output_Hwc_Mode mode = E_OUTPUT_HWC_MODE_NO;
   Eina_Bool set = EINA_FALSE;

   if (e_comp->nocomp_override > 0) return;

   if (_e_output_hwc_planes_can_hwcompose(eout))
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
_e_output_hwc_mode_get(E_Output_Hwc *output_hwc)
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
        error = tdm_output_hwc_set_need_validate_handler(output->toutput, _e_output_hwc_windows_need_validate_handler);
        EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, fail);

        ELOGF("HWC-OPT", "register a need_validate_handler for the eo:%p.", NULL, NULL, output);

        if (!e_hwc_window_init(output_hwc))
          {
             ERR("hwc_opt: E_Hwc_Window init failed");
             goto fail;
          }

        /* turn on sw compositor at the start */
        ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);

        evas_event_callback_add(e_comp->evas, EVAS_CALLBACK_RENDER_FLUSH_POST, _e_output_hwc_canvas_render_flush_post, output_hwc);
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

   if (e_output_hwc_windows_enabled(output_hwc))
     {
        /* evaluate which e_output_hwc_window will be composited by hwc and wich by GLES */
        if (!_e_output_hwc_windows_re_evaluate(output_hwc))
           ERR("fail to _e_output_hwc_windows_re_evaluate.");
     }
   else
     {
        if (!_e_output_hwc_planes_usable(output_hwc))
          {
             e_output_hwc_end(output_hwc, __FUNCTION__);
             return;
          }

        if (output_hwc->hwc_mode == E_OUTPUT_HWC_MODE_NO)
          _e_output_hwc_planes_begin(output_hwc);
        else
          _e_output_hwc_planes_changed(output_hwc);
     }
}

EINTERN E_Output_Hwc_Mode
e_output_hwc_mode_get(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, E_OUTPUT_HWC_MODE_NO);

   return output_hwc->hwc_mode;
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
e_output_hwc_end(E_Output_Hwc *output_hwc, const char *location)
{
   E_Output_Hwc_Mode new_mode = E_OUTPUT_HWC_MODE_NO;

   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   /* clean the reserved planes(clean the candidate ecs) */
   _e_output_hwc_planes_reserved_clean(output_hwc);

   if (!output_hwc->hwc_mode) return;

   /* set null to the e_planes */
   _e_output_hwc_planes_cancel(output_hwc);

   /* check the current mode */
   new_mode = _e_output_hwc_mode_get(output_hwc);

   if (output_hwc->hwc_mode == E_OUTPUT_HWC_MODE_FULL &&
       new_mode != E_OUTPUT_HWC_MODE_FULL)
     ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);

   output_hwc->hwc_mode = new_mode;

   ELOGF("HWC", " End...  at %s.", NULL, NULL, location);
}

EINTERN Eina_Bool
e_output_hwc_windows_enabled(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);

   return output_hwc->opt_hwc;
}

EINTERN const Eina_List *
e_output_hwc_windows_get(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, NULL);

   return output_hwc->hwc_windows;
}

EINTERN Eina_Bool
e_output_hwc_windows_render(E_Output_Hwc *output_hwc)
{
   E_Output *output = output_hwc->output;
   E_Hwc_Window_Target *target_hwc_window;

   target_hwc_window = output_hwc->target_hwc_window;
   if (!target_hwc_window)
     {
        ERR("fail to get target hwc_window for output(%p).", output);
        return EINA_FALSE;
     }

   if (!_e_output_hwc_windows_target_window_render(output, target_hwc_window))
     ERR("fail to render output(%p).", output);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_hwc_windows_commit(E_Output_Hwc *output_hwc)
{
   E_Hwc_Window *hwc_window = NULL;
   Eina_List *l;
   int need_tdm_commit = 0;
   Eina_Bool fb_commit = EINA_FALSE;
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);

   output = output_hwc->output;

   ELOGF("HWC-OPT", "=== Fetch the buffers ===", NULL, NULL);

   EINA_LIST_FOREACH(output_hwc->hwc_windows, l, hwc_window)
     {
        /* an underlying hwc_window still occupies a hw overlay, so we can't
         * allow to change buffer (make fetch) as hwc_window in a client_candidate state */
        if (e_hwc_window_get_state(hwc_window) == E_HWC_WINDOW_STATE_CLIENT_CANDIDATE)
          continue;

        /* fetch the surface to the window */
        if (!e_hwc_window_fetch(hwc_window)) continue;

        if (e_hwc_window_is_target(hwc_window)) fb_commit = EINA_TRUE;

        if (output->dpms == E_OUTPUT_DPMS_OFF)
          _e_output_hwc_windows_offscreen_commit(output, hwc_window);
     }

   if (output->dpms == E_OUTPUT_DPMS_OFF) return EINA_TRUE;

   if (!_e_output_hwc_windows_can_commit(output))
     {
        ELOGF("HWC-OPT", "Prevent by _e_output_hwc_windows_can_commit.", NULL, NULL);
        return EINA_FALSE;
     }

   EINA_LIST_FOREACH(output_hwc->hwc_windows, l, hwc_window)
     {
        if (_e_output_hwc_windows_prepare_commit(output, hwc_window))
          need_tdm_commit = 1;

        // TODO: to be fixed. check fps of fb_target currently.
        if (fb_commit) _e_output_hwc_windows_update_fps();
     }

   if (need_tdm_commit)
     {
        tdm_error error = TDM_ERROR_NONE;

        ELOGF("HWC-OPT", "=== Output Commit ===", NULL, NULL);
        error = tdm_output_commit(output->toutput, 0, _e_output_hwc_windows_commit_handler, output_hwc);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

        output->wait_commit = EINA_TRUE;
     }

   return EINA_TRUE;
}
