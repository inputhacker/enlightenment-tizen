#include "e.h"
#include "services/e_service_quickpanel.h"


#define DBG_EVALUATE 0

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
   int num_vis_wnd = 0;

   EINA_LIST_FOREACH(output_hwc->hwc_windows, l, hwc_window)
     {
        if (hwc_window->state == E_HWC_WINDOW_STATE_NONE) continue;

        if (!e_hwc_window_is_on_hw_overlay(hwc_window))
          return EINA_TRUE;

        num_vis_wnd++;
     }

   if (!num_vis_wnd)
     return EINA_TRUE;

   return EINA_FALSE;
}

static Eina_Bool
_e_output_hwc_windows_all_windows_init(E_Output_Hwc *output_hwc)
{
   const Eina_List *hwc_windows, *l;
   E_Hwc_Window *hwc_window = NULL;

   hwc_windows = e_output_hwc_windows_get(output_hwc);
   EINA_LIST_FOREACH(hwc_windows, l, hwc_window)
     {
        if (e_hwc_window_is_video(hwc_window)) continue;

        if (!e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_NONE))
          {
             ERR("e_hwc_window_state_set failed.");
             return EINA_FALSE;
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
     case E_HWC_WINDOW_STATE_VIDEO:
       return "VIDEO";
     case E_HWC_WINDOW_STATE_DEVICE_CANDIDATE:
       return "DEVICE_CANDIDATE";
     case E_HWC_WINDOW_STATE_CURSOR:
       return "CURSOR";
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

static unsigned int
_e_output_hwc_windows_aligned_width_get(tbm_surface_h tsurface)
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
         if (hwc_window->state == E_HWC_WINDOW_STATE_NONE) continue;

         if (e_hwc_window_is_target(hwc_window))
           ELOGF("HWC-WINS", "  hwc_window:%p -- target_hwc_window, state:%s",
                 NULL, NULL, hwc_window, _e_output_hwc_windows_get_name_of_wnd_state(hwc_window->state));
         else
           ELOGF("HWC-WINS", "  hwc_window:%p -- {title:%25s}, state:%s, deleted:%s, zpos:%d",
                 hwc_window->ec ? hwc_window->ec->pixmap : NULL, hwc_window->ec,
                 hwc_window, hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN",
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

static void
_e_output_hwc_windows_update(E_Output_Hwc *output_hwc, Eina_List *cl_list)
{
   const Eina_List *l;
   E_Hwc_Window *hwc_window;
   E_Client *ec;
   Eina_Bool result;
   int zpos = 0;

   /* clients are sorted in reverse order */
   EINA_LIST_REVERSE_FOREACH(cl_list, l, ec)
     {
        hwc_window = ec->hwc_window;
        if (!hwc_window)
          {
             ERR("hwc-opt: cannot find the hwc_window by ec(%p)", ec);
             continue;
          }

        /* e20 deal with video window at the e_comp_wl_video */
        if (e_hwc_window_is_video(hwc_window)) continue;

        result = e_hwc_window_zpos_set(hwc_window, zpos);
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
#if DBG_EVALUATE
   ELOGF("HWC-WINS", " Request HWC Validation to TDM HWC:", NULL, NULL);
   _e_output_hwc_windows_print_wnds_state(output_hwc);
#endif
}

static E_Hwc_Window_State
_e_output_hwc_windows_window_state_get(tdm_hwc_window_composition composition_type)
{
   E_Hwc_Window_State state = E_HWC_WINDOW_STATE_NONE;

   switch (composition_type)
     {
      case TDM_COMPOSITION_NONE:
        state = E_HWC_WINDOW_STATE_NONE;
        break;
      case TDM_COMPOSITION_CLIENT:
        state = E_HWC_WINDOW_STATE_CLIENT;
        break;
      case TDM_COMPOSITION_DEVICE:
        state = E_HWC_WINDOW_STATE_DEVICE;
        break;
      case TDM_COMPOSITION_DEVICE_CANDIDATE:
        state = E_HWC_WINDOW_STATE_DEVICE_CANDIDATE;
        break;
      case TDM_COMPOSITION_CURSOR:
        state = E_HWC_WINDOW_STATE_CURSOR;
        break;
      default:
        state = E_HWC_WINDOW_STATE_NONE;
        ERR("hwc-opt: unknown state of hwc_window.");
     }

   return state;
}

static Eina_Bool
_e_output_hwc_windows_validate(E_Output_Hwc *output_hwc)
{
   tdm_error tdm_err;
   uint32_t num_changes;
   E_Output *eo = output_hwc->output;
   tdm_output *toutput = eo->toutput;
   E_Hwc_Window *hwc_window;
   E_Hwc_Window_State state;

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
             state = _e_output_hwc_windows_window_state_get(composition_types[i]);
             if (!e_hwc_window_state_set(hwc_window, state))
               {
                  ERR("e_hwc_window_state_set failed.");
                  return EINA_FALSE;
               }
          }

        free(changed_hwc_window);
        free(composition_types);

        tdm_err = tdm_output_hwc_accept_changes(toutput);
        if (tdm_err != TDM_ERROR_NONE)
          {
             ERR("hwc-opt: failed to accept changes required by the hwc extension");
             return EINA_FALSE;
          }
#if DBG_EVALUATE
        ELOGF("HWC-WINS", " Modified after HWC Validation:", NULL, NULL);
        _e_output_hwc_windows_print_wnds_state(output_hwc);
#endif
     }

   return EINA_TRUE;
}

static void
_e_output_hwc_windows_activation_states_update(E_Output_Hwc *output_hwc)
{
   E_Hwc_Window *hwc_window;
   const Eina_List *l;

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
}

static Eina_Bool
_e_output_hwc_windows_target_window_render(E_Output *output, E_Hwc_Window_Target *target_hwc_window)
{
    if (target_hwc_window->hwc_window.state == E_HWC_WINDOW_STATE_NONE) return EINA_TRUE;

    if (e_comp_canvas_norender_get() > 0)
      {
           ELOGF("HWC-WINS", " NoRender get. Do not ecore_evas_manual_render.", NULL, NULL);
          return EINA_TRUE;
      }

   /* render the ecore_evas and
      update_ee is to be true at post_render_cb when the render is successful. */
   TRACE_DS_BEGIN(MANUAL RENDER);

   if (e_hwc_window_target_surface_queue_can_dequeue(target_hwc_window))
     {
        ELOGF("HWC-WINS", "###### Render target window(ecore_evas_manual_render))", NULL, NULL);
        ecore_evas_manual_render(target_hwc_window->ee);
     }

   TRACE_DS_END();

   return EINA_TRUE;
}

/* gets called at the beginning of an ecore_main_loop iteration */
static void
_e_output_hwc_windows_need_validate_handler(tdm_output *output)
{
   ELOGF("HWC-WINS", "backend asked to make the revalidation.", NULL, NULL);

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

   output_hwc->num_vis_ec = eina_list_count(ec_list);

#if DBG_EVALUATE
   ELOGF("HWC-WINS", " The number of visible clients:%d.", NULL, NULL, eina_list_count(ec_list));
#endif
   return ec_list;
}

static Eina_Bool
_e_output_hwc_windows_full_gl_composite_check(E_Output_Hwc *output_hwc, Eina_List *vis_cl_list)
{
   Eina_List *l;
   E_Client *ec;
   E_Hwc_Window *hwc_window = NULL;

   /* make the full_gl_composite when the zoom is enabled */
   if (output_hwc->output->zoom_set) goto full_gl_composite;

   /* full composite is forced to be set */
   if (e_output_hwc_deactive_get(output_hwc)) goto full_gl_composite;

   /* hwc_window manager required full GLES composition */
   if (e_comp->nocomp_override > 0)
     {
        ELOGF("HWC-WINS", "  HWC_MODE_NONE due to nocomp_override > 0.", NULL, NULL);
        goto full_gl_composite;
     }

   EINA_LIST_FOREACH(vis_cl_list, l, ec)
     {
#if 0  // TODO: check this condition.....
        // if there is a ec which is lower than quickpanel and quickpanel is opened.
        if (E_POLICY_QUICKPANEL_LAYER >= evas_object_layer_get(ec->frame))
          {
             // check whether quickpanel is open than break
             if (e_qp_visible_get())
               {
                   ELOGF("HWC-WINS", "    HWC_MODE_NONE due to quickpanel is opened.{title:%25s}.",
                         ec->pixmap, ec, ec->icccm.title);
               }
             goto full_gl_composite;
          }
#endif
        // if ec->frame is not for client buffer (e.g. launchscreen)
        if (e_comp_object_content_type_get(ec->frame) != E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE)
          {
             ELOGF("HWC-WINS", "  HWC_MODE_NONE due to E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE{title:%25s}.",
                   ec->pixmap, ec, ec->icccm.title);
             goto full_gl_composite;
          }

        // if there is UI subfrace, it means need to composite
        if (e_client_normal_client_has(ec))
          {
            ELOGF("HWC-WINS", "  HWC_MODE_NONE due to UI subfrace{title:%25s}.",
                  ec->pixmap, ec, ec->icccm.title);
            goto full_gl_composite;
          }
     }

   return EINA_FALSE;

full_gl_composite:
   EINA_LIST_FOREACH(vis_cl_list, l, ec)
     {
        hwc_window = ec->hwc_window;
        hwc_window->hwc_acceptable = EINA_FALSE;
        ELOGF("HWC-WINS", "   hwc_window:%p -- {title:%25s} is NOT hwc_acceptable.",
              ec->pixmap, ec, hwc_window, ec->icccm.title);
     }
   return EINA_TRUE;
}

/* filter visible clients by the hwc_window manager
 *
 * returns list of clients which are acceptable to be composited by hw,
 * it's a caller responsibility to free it
 *
 * for optimized hwc the returned list contains ALL clients
 */
static void
_e_output_hwc_windows_hwc_acceptable_check(Eina_List *vis_cl_list)
{
   Eina_List *l;
   E_Client *ec;
   E_Hwc_Window *hwc_window = NULL;

   EINA_LIST_FOREACH(vis_cl_list, l, ec)
     {
        hwc_window = ec->hwc_window;
        hwc_window->hwc_acceptable = EINA_TRUE;

        // check clients not able to use hwc
        // if ec has invalid buffer or scaled( transformed ) or forced composite(never_hwc)
        if (!_e_output_hwc_ec_check(ec))
          {
             hwc_window->hwc_acceptable = EINA_FALSE;
             ELOGF("HWC-WINS", "   hwc_window:%p -- {title:%25s} is NOT hwc_acceptable.",
                   ec->pixmap, ec, hwc_window, ec->icccm.title);
             continue;
          }
     }
}

static Eina_Bool
_e_output_hwc_windows_enable_target_window(E_Output_Hwc *output_hwc)
{
   E_Hwc_Window *hwc_window;

   if (!output_hwc->target_hwc_window)
     {
        ERR("we don't have the target hwc_window");
        return EINA_FALSE;
     }

   hwc_window = (E_Hwc_Window*)output_hwc->target_hwc_window;
   e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_DEVICE);

   return EINA_TRUE;
}

static Eina_Bool
_e_output_hwc_windows_evaluate(E_Output_Hwc *output_hwc)
{
   Eina_Bool ret = EINA_FALSE;
   Eina_Bool result;
   Eina_List *vis_clist = NULL;
   E_Output_Hwc_Mode hwc_mode = E_OUTPUT_HWC_MODE_NO;
#if DBG_EVALUATE
   ELOGF("HWC-WINS", "###### Output HWC Apply (evaluate) ===", NULL, NULL);
#endif
   /* exclude all hwc_windows from being considered by hwc */
   result = _e_output_hwc_windows_all_windows_init(output_hwc);
   EINA_SAFETY_ON_FALSE_GOTO(result, done);

   /* get the visible ecs */
   vis_clist = _e_output_hwc_windows_vis_ec_list_get(output_hwc);

   /* in order to turn on target_window and smooth transition is not used the
    * all hwc_windows is excluded
    */
   if (!output_hwc->output->zoom_set)
     {
        /* check the gles composite with all hwc_windows. */
        if (!_e_output_hwc_windows_full_gl_composite_check(output_hwc, vis_clist))
          {
             /* by demand of hwc_window manager to prevent some e_clients to be shown by hw directly */
             _e_output_hwc_windows_hwc_acceptable_check(vis_clist);
          }

        /* update thw hwc_windows information with the previous evaluation */
        _e_output_hwc_windows_update(output_hwc, vis_clist);
     }
   else
     _e_output_hwc_windows_update(output_hwc, NULL);

   /* validate the updated hwc_windows by asking tdm_hwc_output */
   result = _e_output_hwc_windows_validate(output_hwc);
   EINA_SAFETY_ON_FALSE_GOTO(result, done);

   if (output_hwc->output->zoom_set && eina_list_count(vis_clist) == 1)
     {
        E_Client *ec = eina_list_nth(vis_clist, 0);
        /* The vis_hwc_window's buffer will be used as src for pp. In this case
         * vis_hwc_window has to be withdrawn from the gl compositing */
        if (ec && ec->hwc_window)
          {
             int w, h;

             e_output_size_get(output_hwc->output, &w, &h);
             if (ec->comp_data->buffer_ref.buffer &&
                 ec->comp_data->buffer_ref.buffer->w == w &&
                 ec->comp_data->buffer_ref.buffer->h == h)
               {
                  ec->hwc_window->state = E_HWC_WINDOW_STATE_DEVICE;
                  ec->hwc_window->type = TDM_COMPOSITION_DEVICE;
               }
          }
     }

   /* TODO: decide the E_OUTPUT_HWC_MODE */
   hwc_mode = _e_output_hwc_windows_need_target_hwc_window(output_hwc) ?
           E_OUTPUT_HWC_MODE_HYBRID : E_OUTPUT_HWC_MODE_FULL;

   if (hwc_mode == E_OUTPUT_HWC_MODE_HYBRID || hwc_mode == E_OUTPUT_HWC_MODE_NO)
     {
#if DBG_EVALUATE
        ELOGF("HWC-WINS", " HWC_MODE is HYBRID composition.", NULL, NULL);
#endif
        result = _e_output_hwc_windows_enable_target_window(output_hwc);
        EINA_SAFETY_ON_FALSE_GOTO(result, done);
     }
#if DBG_EVALUATE
   else
     ELOGF("HWC-WINS", " HWC_MODE is FULL HW composition.", NULL, NULL);
#endif

   if (output_hwc->hwc_mode != hwc_mode)
     {
        if (hwc_mode == E_OUTPUT_HWC_MODE_FULL)
          ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
        else if(hwc_mode == E_OUTPUT_HWC_MODE_HYBRID)
          ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);

        output_hwc->hwc_mode  = hwc_mode;
     }

   /* update the activate/decativate state */
   _e_output_hwc_windows_activation_states_update(output_hwc);

   ret = EINA_TRUE;

done:
   if (vis_clist)
     eina_list_free(vis_clist);

   return ret;
}

static void
_e_output_hwc_windows_commit_handler(tdm_output *toutput, unsigned int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec,
                                  void *user_data)
{
   const Eina_List *l;
   E_Hwc_Window *window;
   E_Output_Hwc *output_hwc = (E_Output_Hwc *)user_data;

   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   if (output_hwc->pp_tsurface && !output_hwc->output->zoom_set)
     {
        tbm_surface_internal_unref(output_hwc->pp_tsurface);
        output_hwc->pp_tsurface = NULL;
     }

   EINA_LIST_FOREACH(e_output_hwc_windows_get(output_hwc), l, window)
     {
         if (!e_hwc_window_commit_data_release(window)) continue;
         if (e_hwc_window_is_video(window))
           e_video_commit_data_release(window->ec, sequence, tv_sec, tv_usec);
     }

   /* 'wait_commit' is mechanism to make 'fetch and commit' no more than one time per a frame;
    * a 'page flip' happened so it's time to allow to make 'fetch and commit' for the e_output */
   output_hwc->wait_commit = EINA_FALSE;
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

static E_Hwc_Window *
_e_output_hwc_windows_pp_window_get(E_Output_Hwc *output_hwc, tbm_surface_h tsurface)
{
   Eina_List *l;
   E_Hwc_Window *hwc_window = NULL;

   EINA_LIST_FOREACH(output_hwc->pp_hwc_window_list, l, hwc_window)
     {
        if (!hwc_window) continue;
        if (!hwc_window->commit_data) continue;

        if (hwc_window->commit_data->tsurface == tsurface)
          return hwc_window;
     }

   return NULL;
}

static void
_e_output_hwc_windows_pp_pending_data_remove(E_Output_Hwc *output_hwc)
{
   E_Hwc_Window_Commit_Data *data = NULL;
   Eina_List *l = NULL, *ll = NULL;

   if (eina_list_count(output_hwc->pending_pp_commit_data_list) != 0)
     {
        EINA_LIST_FOREACH_SAFE(output_hwc->pending_pp_commit_data_list, l, ll, data)
          {
             if (!data) continue;
             output_hwc->pending_pp_commit_data_list = eina_list_remove_list(output_hwc->pending_pp_commit_data_list, l);
             tbm_surface_queue_release(output_hwc->pp_tqueue, data->tsurface);
             tbm_surface_internal_unref(data->tsurface);
             E_FREE(data);
          }
     }
   eina_list_free(output_hwc->pending_pp_commit_data_list);
   output_hwc->pending_pp_commit_data_list = NULL;

   if (eina_list_count(output_hwc->pending_pp_hwc_window_list) != 0)
     {
        E_Hwc_Window *hwc_window;
        EINA_LIST_FOREACH_SAFE(output_hwc->pending_pp_hwc_window_list, l, ll, hwc_window)
          {
             if (!hwc_window) continue;
             output_hwc->pending_pp_hwc_window_list = eina_list_remove_list(output_hwc->pending_pp_hwc_window_list, l);

             if (hwc_window->ec) e_pixmap_image_clear(hwc_window->ec->pixmap, 1);
             e_hwc_window_commit_data_release(hwc_window);
          }
     }
   eina_list_free(output_hwc->pending_pp_hwc_window_list);
   output_hwc->pending_pp_hwc_window_list = NULL;
}

static Eina_Bool
_e_output_hwc_windows_pp_output_data_commit(E_Output_Hwc *output_hwc, E_Hwc_Window_Commit_Data *data);

static Eina_Bool
_e_output_hwc_windows_pp_window_commit(E_Output_Hwc *output_hwc, E_Hwc_Window *hwc_window);

static void
_e_output_hwc_windows_pp_output_commit_handler(tdm_output *toutput, unsigned int sequence,
                                              unsigned int tv_sec, unsigned int tv_usec,
                                              void *user_data)
{
   E_Output_Hwc *output_hwc;
   E_Hwc_Window_Commit_Data *data;
   E_Output *output = NULL;
   const Eina_List *l;
   E_Hwc_Window *window;

   EINA_SAFETY_ON_NULL_RETURN(user_data);

   output_hwc = user_data;

   output_hwc->pp_output_commit = EINA_FALSE;

   EINA_LIST_FOREACH(e_output_hwc_windows_get(output_hwc), l, window)
     {
        if (window->commit_data && !window->commit_data->tsurface)
          e_hwc_window_commit_data_release(window);
     }

   /* layer already resetted */
   if (output_hwc->pp_output_commit_data)
     {
        data = output_hwc->pp_output_commit_data;
        output_hwc->pp_output_commit_data = NULL;

        /* if pp_set is false, do not deal with pending list */
        if (!output_hwc->pp_set)
          {
             if (output_hwc->pp_tsurface)
               tbm_surface_internal_unref(output_hwc->pp_tsurface);

             output_hwc->pp_tsurface = data->tsurface;
             output_hwc->wait_commit = EINA_FALSE;

             E_FREE(data);

             return;
          }

        if (output_hwc->pp_tqueue && output_hwc->pp_tsurface)
          {
             /* release and unref the current pp surface on the plane */
             tbm_surface_queue_release(output_hwc->pp_tqueue, output_hwc->pp_tsurface);
             tbm_surface_internal_unref(output_hwc->pp_tsurface);
          }

        /* set the new pp surface to the plane */
        output_hwc->pp_tsurface = data->tsurface;

        E_FREE(data);
     }

   ELOGF("HWC-WINS", "PP Output Commit Handler Output_Hwc(%p)", NULL, NULL, output_hwc);

   output = output_hwc->output;
   if (e_output_dpms_get(output))
     {
        _e_output_hwc_windows_pp_pending_data_remove(output_hwc);
        return;
     }

   /* deal with the pending layer commit */
   if (eina_list_count(output_hwc->pending_pp_commit_data_list) != 0)
     {
        data = eina_list_nth(output_hwc->pending_pp_commit_data_list, 0);
        if (data)
          {
             output_hwc->pending_pp_commit_data_list = eina_list_remove(output_hwc->pending_pp_commit_data_list, data);

             ELOGF("HWC-WINS", "PP Output Commit Handler start pending commit data(%p) tsurface(%p)", NULL, NULL, data, data->tsurface);

             if (!_e_output_hwc_windows_pp_output_data_commit(output_hwc, data))
               {
                  ERR("fail to _e_output_hwc_windows_pp_output_data_commit");
                  return;
               }
          }
     }

   /* deal with the pending pp commit */
   if (eina_list_count(output_hwc->pending_pp_hwc_window_list) != 0)
     {
        E_Hwc_Window *hwc_window;

        hwc_window = eina_list_nth(output_hwc->pending_pp_hwc_window_list, 0);
        if (hwc_window)
          {
             if (!tbm_surface_queue_can_dequeue(output_hwc->pp_tqueue, 0))
               return;

             output_hwc->pending_pp_hwc_window_list = eina_list_remove(output_hwc->pending_pp_hwc_window_list, hwc_window);

             ELOGF("HWC-WINS", "PP Layer Commit Handler start pending pp data(%p) tsurface(%p)", NULL, NULL, data, data->tsurface);

             if (!_e_output_hwc_windows_pp_window_commit(output_hwc, hwc_window))
               {
                  ERR("fail _e_output_hwc_windows_pp_data_commit");
                  e_hwc_window_commit_data_release(hwc_window);
                  return;
               }
          }
     }
}

static Eina_Bool
_e_output_hwc_windows_pp_output_data_commit(E_Output_Hwc *output_hwc, E_Hwc_Window_Commit_Data *data)
{
   E_Output *output = NULL;
   tdm_layer *toutput = NULL;
   tdm_error tdm_err;
   tdm_hwc_region fb_damage;

   /* the damage isn't supported by hwc extension yet */
   memset(&fb_damage, 0, sizeof(fb_damage));

   EINA_SAFETY_ON_NULL_RETURN_VAL(data, EINA_FALSE);

   output = output_hwc->output;
   toutput = output->toutput;

   if (e_output_dpms_get(output))
     {
        _e_output_hwc_windows_pp_pending_data_remove(output_hwc);
        goto fail;
     }

   /* no need to pass composited_wnds list because smooth transition isn't
    * used is this case */
   tdm_err = tdm_output_hwc_set_client_target_buffer(toutput, data->tsurface, fb_damage, NULL, 0);
   if (tdm_err != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_output_hwc_set_client_target_buffer");
        goto fail;
     }

   tdm_err = tdm_output_commit(toutput, 0, _e_output_hwc_windows_pp_output_commit_handler, output_hwc);

   if (tdm_err != TDM_ERROR_NONE)
     {
        ERR("fail to tdm_output_commit output_hwc:%p", output_hwc);
        goto fail;
     }

   output_hwc->pp_output_commit = EINA_TRUE;
   output_hwc->pp_output_commit_data = data;

   return EINA_TRUE;

fail:
   tbm_surface_internal_unref(data->tsurface);
   tbm_surface_queue_release(output_hwc->pp_tqueue, data->tsurface);
   E_FREE(data);

   return EINA_FALSE;
}

static Eina_Bool
_e_output_hwc_windows_pp_output_commit(E_Output_Hwc *output_hwc, tbm_surface_h tsurface)
{
   tbm_surface_h pp_tsurface = NULL;
   tbm_error_e tbm_err;
   E_Hwc_Window_Commit_Data *data = NULL;

   ELOGF("HWC-WINS", "PP Layer Commit  output_hwc(%p)     pp_tsurface(%p)", NULL, NULL, output_hwc, tsurface);

   tbm_err = tbm_surface_queue_enqueue(output_hwc->pp_tqueue, tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("fail tbm_surface_queue_enqueue");
        goto fail;
     }

   tbm_err = tbm_surface_queue_acquire(output_hwc->pp_tqueue, &pp_tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("fail tbm_surface_queue_acquire");
        goto fail;
     }

   data = E_NEW(E_Hwc_Window_Commit_Data, 1);
   if (!data) goto fail;
   data->tsurface = pp_tsurface;
   tbm_surface_internal_ref(data->tsurface);

   if (output_hwc->pp_output_commit)
     {
        output_hwc->pending_pp_commit_data_list = eina_list_append(output_hwc->pending_pp_commit_data_list, data);
        return EINA_TRUE;
     }

   if (!_e_output_hwc_windows_pp_output_data_commit(output_hwc, data))
     {
        ERR("fail to _e_output_hwc_windows_pp_output_data_commit");
        return EINA_FALSE;
     }

   return EINA_TRUE;

fail:
   tbm_surface_queue_release(output_hwc->pp_tqueue, tsurface);
   if (pp_tsurface && pp_tsurface != tsurface)
     tbm_surface_queue_release(output_hwc->pp_tqueue, pp_tsurface);

   return EINA_FALSE;
}

static void
_e_output_hwc_windows_pp_commit_handler(tdm_pp *pp, tbm_surface_h tsurface_src, tbm_surface_h tsurface_dst, void *user_data)
{
   E_Output *output = NULL;
   E_Output_Hwc *output_hwc = NULL;
   E_Hwc_Window *hwc_window = NULL;

   output_hwc = (E_Output_Hwc *)user_data;
   EINA_SAFETY_ON_NULL_RETURN(output_hwc);
   hwc_window = _e_output_hwc_windows_pp_window_get(output_hwc, tsurface_src);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   output_hwc->pp_hwc_window_list = eina_list_remove(output_hwc->pp_hwc_window_list, hwc_window);

   if (hwc_window->ec) e_pixmap_image_clear(hwc_window->ec->pixmap, 1);
   e_hwc_window_commit_data_release(hwc_window);

   if (eina_list_count(output_hwc->pending_pp_hwc_window_list) == 0)
     {
        output_hwc->wait_commit = EINA_FALSE;
        output_hwc->pp_commit = EINA_FALSE;
     }

   ELOGF("HWC-WINS", "PP Commit Handler output_hwc(%p) tsurface src(%p) dst(%p)",
         NULL, NULL, output_hwc, tsurface_src, tsurface_dst);

   /* if pp_set is false, skip the commit */
   if (!output_hwc->pp_set)
     {
        if (output_hwc->tpp)
          {
             tdm_pp_destroy(output_hwc->tpp);
             output_hwc->tpp = NULL;
          }
        goto done;
     }

   output = output_hwc->output;
   if (e_output_dpms_get(output))
     {
        _e_output_hwc_windows_pp_pending_data_remove(output_hwc);
        tbm_surface_queue_release(output_hwc->pp_tqueue, tsurface_dst);

        goto done;
     }

   if (!_e_output_hwc_windows_pp_output_commit(output_hwc, tsurface_dst))
     ERR("fail to _e_output_hwc_windows_pp_output_commit");

done:
   tbm_surface_internal_unref(tsurface_src);
   tbm_surface_internal_unref(tsurface_dst);
}

static Eina_Bool
_e_output_hwc_pp_windows_info_set(E_Output_Hwc *output_hwc, E_Hwc_Window *hwc_window,
                                  tbm_surface_h dst_tsurface)
{
   tdm_info_pp pp_info;
   tdm_error ret = TDM_ERROR_NONE;
   unsigned int aligned_width_src = 0, aligned_width_dst = 0;
   tbm_surface_info_s surf_info_src, surf_info_dst;
   tbm_surface_h src_tsurface = hwc_window->commit_data->tsurface;

   /* when the pp_set_info is true, change the pp set_info */
   if (!output_hwc->pp_set_info) return EINA_TRUE;
   output_hwc->pp_set_info = EINA_FALSE;

   tbm_surface_get_info(src_tsurface, &surf_info_src);

   aligned_width_src = _e_output_hwc_windows_aligned_width_get(src_tsurface);
   if (aligned_width_src == 0) return EINA_FALSE;

   tbm_surface_get_info(dst_tsurface, &surf_info_dst);

   aligned_width_dst = _e_output_hwc_windows_aligned_width_get(dst_tsurface);
   if (aligned_width_dst == 0) return EINA_FALSE;

   pp_info.src_config.size.h = aligned_width_src;
   pp_info.src_config.size.v = surf_info_src.height;
   pp_info.src_config.format = surf_info_src.format;

   pp_info.dst_config.size.h = aligned_width_dst;
   pp_info.dst_config.size.v = surf_info_dst.height;
   pp_info.dst_config.format = surf_info_dst.format;

   pp_info.transform = TDM_TRANSFORM_NORMAL;
   pp_info.sync = 0;
   pp_info.flags = 0;

   pp_info.src_config.pos.x = output_hwc->pp_rect.x;
   pp_info.src_config.pos.y = output_hwc->pp_rect.y;
   pp_info.src_config.pos.w = output_hwc->pp_rect.w;
   pp_info.src_config.pos.h = output_hwc->pp_rect.h;
   pp_info.dst_config.pos.x = 0;
   pp_info.dst_config.pos.y = 0;
   pp_info.dst_config.pos.w = surf_info_dst.width;
   pp_info.dst_config.pos.h = surf_info_dst.height;

   ret = tdm_pp_set_info(output_hwc->tpp, &pp_info);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   ELOGF("HWC-WINS", "PP Info  Output_Hwc(%p) src_rect(%d,%d),(%d,%d), dst_rect(%d,%d),(%d,%d)",
         NULL, NULL, output_hwc,
         pp_info.src_config.pos.x, pp_info.src_config.pos.y, pp_info.src_config.pos.w, pp_info.src_config.pos.h,
         pp_info.dst_config.pos.x, pp_info.dst_config.pos.y, pp_info.dst_config.pos.w, pp_info.dst_config.pos.h);

   return EINA_TRUE;
}

static Eina_Bool
_e_output_hwc_windows_pp_window_commit(E_Output_Hwc *output_hwc, E_Hwc_Window *hwc_window)
{
   E_Output *output = NULL;
   tbm_surface_h pp_tsurface = NULL;
   tbm_error_e tbm_err = TBM_ERROR_NONE;
   tdm_error tdm_err = TDM_ERROR_NONE;
   E_Hwc_Window_Commit_Data *commit_data = hwc_window->commit_data;
   EINA_SAFETY_ON_FALSE_RETURN_VAL(commit_data, EINA_FALSE);

   tbm_surface_h tsurface = commit_data->tsurface;

   ELOGF("HWC-WINS", "PP Commit  Output_Hwc(%p)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
            NULL, NULL, output_hwc, commit_data->tsurface, output_hwc->pp_tqueue,
            commit_data->buffer_ref.buffer ? commit_data->buffer_ref.buffer->resource : NULL, commit_data);

   output = output_hwc->output;
   if (e_output_dpms_get(output))
     {
        _e_output_hwc_windows_pp_pending_data_remove(output_hwc);
        return EINA_FALSE;
     }

   tbm_err = tbm_surface_queue_dequeue(output_hwc->pp_tqueue, &pp_tsurface);
   if (tbm_err != TBM_ERROR_NONE)
     {
        ERR("fail tbm_surface_queue_dequeue");
        return EINA_FALSE;
     }

   if (!_e_output_hwc_pp_windows_info_set(output_hwc, hwc_window, pp_tsurface))
     {
        ERR("fail _e_output_hwc_windows_info_set");
        goto pp_fail;
     }

   tdm_err = tdm_pp_set_done_handler(output_hwc->tpp, _e_output_hwc_windows_pp_commit_handler, output_hwc);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, pp_fail);

   tbm_surface_internal_ref(pp_tsurface);
   tbm_surface_internal_ref(commit_data->tsurface);
   tdm_err = tdm_pp_attach(output_hwc->tpp, commit_data->tsurface, pp_tsurface);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, attach_fail);

   output_hwc->pp_hwc_window_list = eina_list_append(output_hwc->pp_hwc_window_list, hwc_window);

   tdm_err = tdm_pp_commit(output_hwc->tpp);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, commit_fail);

   output_hwc->wait_commit = EINA_TRUE;
   output_hwc->pp_commit = EINA_TRUE;

   return EINA_TRUE;

commit_fail:
   output_hwc->pp_hwc_window_list = eina_list_remove(output_hwc->pp_hwc_window_list, hwc_window);
attach_fail:
   tbm_surface_internal_unref(pp_tsurface);
   tbm_surface_internal_unref(tsurface);
pp_fail:
   tbm_surface_queue_release(output_hwc->pp_tqueue, pp_tsurface);

   ERR("failed _e_output_hwc_windows_pp_data_commit");

   return EINA_FALSE;
}

static E_Hwc_Window *
_e_output_hwc_windows_pp_get_hwc_window_for_zoom(E_Output_Hwc *output_hwc)
{
   const Eina_List *hwc_windows, *l;
   E_Hwc_Window *hwc_window = NULL;
   E_Hwc_Window *hwc_window_for_zoom = NULL;
   int num = 0;
   int w, h;

   e_output_size_get(output_hwc->output, &w, &h);

   hwc_windows = e_output_hwc_windows_get(output_hwc);
   EINA_LIST_FOREACH(hwc_windows, l, hwc_window)
   {
      if (!e_hwc_window_is_on_hw_overlay(hwc_window)) continue;

      hwc_window_for_zoom = hwc_window;
      num++;
   }

   if (num != 1) return NULL;
   if (!hwc_window_for_zoom->tsurface) return NULL;
   if (tbm_surface_get_width(hwc_window_for_zoom->tsurface) != w ||
       tbm_surface_get_height(hwc_window_for_zoom->tsurface) != h)
     return NULL;

   return hwc_window_for_zoom;
}

static Eina_Bool
_e_output_hwc_windows_pp_commit(E_Output_Hwc *output_hwc)
{
   E_Hwc_Window_Commit_Data *commit_data = NULL;
   E_Hwc_Window *hwc_window = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc->pp_tqueue, EINA_FALSE);

   hwc_window = _e_output_hwc_windows_pp_get_hwc_window_for_zoom(output_hwc);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   commit_data = hwc_window->commit_data;
   if (!commit_data) return EINA_TRUE;
   if (!commit_data->tsurface) return EINA_TRUE;

   if (!tbm_surface_queue_can_dequeue(output_hwc->pp_tqueue, 0))
     {
        ELOGF("HWC-WINS", "PP Commit  Can Dequeue failed Output_Hwc(%p)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
              NULL, NULL, output_hwc, commit_data->tsurface, output_hwc->pp_tqueue,
              commit_data->buffer_ref.buffer ? commit_data->buffer_ref.buffer->resource : NULL, commit_data);
        output_hwc->pending_pp_hwc_window_list = eina_list_append(output_hwc->pending_pp_hwc_window_list, hwc_window);

        output_hwc->wait_commit = EINA_TRUE;

        return EINA_TRUE;
     }

   if (eina_list_count(output_hwc->pending_pp_hwc_window_list) != 0)
     {
        ELOGF("HWC-WINS", "PP Commit  Pending pp data remained Output_Hwc(%p)   tsurface(%p) tqueue(%p) wl_buffer(%p) data(%p)",
              NULL, NULL, output_hwc, commit_data->tsurface, output_hwc->pp_tqueue,
              commit_data->buffer_ref.buffer ? commit_data->buffer_ref.buffer->resource : NULL, commit_data);
        output_hwc->pending_pp_hwc_window_list = eina_list_append(output_hwc->pending_pp_hwc_window_list, hwc_window);

        output_hwc->wait_commit = EINA_TRUE;

        return EINA_TRUE;
     }

   if (!_e_output_hwc_windows_pp_window_commit(output_hwc, hwc_window))
     {
        ERR("fail _e_output_hwc_windows_pp_data_commit");
        e_hwc_window_commit_data_release(hwc_window);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}


/* Plane based HWC */

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

   /*
    * E20 has two hwc policy options.
    * 1. One is the E_OUTPUT_HWC_POLICY_PLANES.
    *   - E20 decides the hwc policy with the E_Planes associated with the tdm_layers.
    *   - E20 manages how to set the surface(buffer) of the ec to the E_Plane.
    * 2. Another is the E_OUTPUT_HWC_POLICY_WIDNOWS.
    *   - The tdm-backend decides the hwc policy with the E_Hwc_Windows associated with the tdm_hwc_window.
    *   - E20 asks to verify the compsition types of the E_Hwc_Window of the ec.
    */
   if (output->tdm_hwc)
     {
        output_hwc->hwc_policy = E_OUTPUT_HWC_POLICY_WINDOWS;

        /* get backend a shot to ask us for the revalidation */
        error = tdm_output_hwc_set_need_validate_handler(output->toutput, _e_output_hwc_windows_need_validate_handler);
        EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, fail);

        ELOGF("HWC-WINS", "register a need_validate_handler for the eo:%p.", NULL, NULL, output);

        if (!e_hwc_window_init(output_hwc))
          {
             ERR("hwc_opt: E_Hwc_Window init failed");
             goto fail;
          }

        /* turn on sw compositor at the start */
        ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
     }
   else
     output_hwc->hwc_policy = E_OUTPUT_HWC_POLICY_PLANES;

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
   if (e_output_hwc_policy_get(output_hwc) == E_OUTPUT_HWC_POLICY_NONE) return;

   if (e_output_hwc_policy_get(output_hwc) == E_OUTPUT_HWC_POLICY_PLANES)
     {
        if (e_output_hwc_deactive_get(output_hwc))
          {
             if (output_hwc->hwc_mode != E_OUTPUT_HWC_MODE_NO)
               e_output_hwc_planes_end(output_hwc, "deactive set.");
             return;
          }

        if (!_e_output_hwc_planes_usable(output_hwc))
          {
             e_output_hwc_planes_end(output_hwc, __FUNCTION__);
             return;
          }

        if (output_hwc->hwc_mode == E_OUTPUT_HWC_MODE_NO)
          _e_output_hwc_planes_begin(output_hwc);
        else
          _e_output_hwc_planes_changed(output_hwc);
     }
   else
     {
        /* evaluate which e_output_hwc_window will be composited by hwc and wich by GLES */
        if (!_e_output_hwc_windows_evaluate(output_hwc))
           ERR("fail to _e_output_hwc_windows_evaluate.");
     }
}

EINTERN E_Output_Hwc_Mode
e_output_hwc_mode_get(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, E_OUTPUT_HWC_MODE_NO);

   return output_hwc->hwc_mode;
}

EINTERN E_Output_Hwc_Policy
e_output_hwc_policy_get(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);

   return output_hwc->hwc_policy;
}

EINTERN void
e_output_hwc_deactive_set(E_Output_Hwc *output_hwc, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   e_output_hwc_planes_end(output_hwc, __FUNCTION__);
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
e_output_hwc_planes_multi_plane_set(E_Output_Hwc *output_hwc, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   e_output_hwc_planes_end(output_hwc, __FUNCTION__);
   output_hwc->hwc_use_multi_plane = set;

   ELOGF("HWC", "e_output_hwc_planes_multi_plane_set : %d", NULL, NULL, set);
}

EINTERN Eina_Bool
e_output_hwc_planes_multi_plane_get(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);

   return output_hwc->hwc_use_multi_plane;
}

EINTERN void
e_output_hwc_planes_end(E_Output_Hwc *output_hwc, const char *location)
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
   E_Output *output = NULL;
   Eina_Bool fetched = EINA_FALSE; // for logging.
   tdm_error error = TDM_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);

   output = output_hwc->output;

   ELOGF("HWC-WINS", "###### Prepare Windows Commit(Fetch the buffers)", NULL, NULL);

   if (output_hwc->wait_commit)
     {
        ELOGF("HWC-WINS", "!!!!!!!! Didn't get Output Commit Handler Yet !!!!!!!!", NULL, NULL);
        return EINA_TRUE;
     }

   EINA_LIST_FOREACH(output_hwc->hwc_windows, l, hwc_window)
     {
        /* fetch the surface to the window */
        if (!e_hwc_window_fetch(hwc_window)) continue;

        fetched = EINA_TRUE;

        if (output->dpms == E_OUTPUT_DPMS_OFF)
          _e_output_hwc_windows_offscreen_commit(output, hwc_window);
     }

   if (!fetched) ELOGF("HWC-WINS", " no new fetched buffers", NULL, NULL);

   if (output->dpms == E_OUTPUT_DPMS_OFF) return EINA_TRUE;

   if (!_e_output_hwc_windows_can_commit(output))
     {
        ELOGF("HWC-WINS", " Prevent by _e_output_hwc_windows_can_commit.", NULL, NULL);
        return EINA_FALSE;
     }

   EINA_LIST_FOREACH(output_hwc->hwc_windows, l, hwc_window)
     {
        if (_e_output_hwc_windows_prepare_commit(output, hwc_window))
          need_tdm_commit = 1;
     }

   if (need_tdm_commit)
     {
       if (output->zoom_set)
         {
            e_output_zoom_rotating_check(output);
            ELOGF("HWC-WINS", "###### PP Commit", NULL, NULL);
            if (!_e_output_hwc_windows_pp_commit(output_hwc))
              {
                ERR("_e_output_hwc_windows_pp_commit failed.");
                return EINA_FALSE;
              }
         }
       else
         {
            ELOGF("HWC-WINS", "!!!!!!!! Output Commit !!!!!!!!", NULL, NULL);
            ELOGF("HWC-WINS", " The number of visible clients:%d.", NULL, NULL, output_hwc->num_vis_ec);
            _e_output_hwc_windows_print_wnds_state(output_hwc);

            error = tdm_output_commit(output->toutput, 0, _e_output_hwc_windows_commit_handler, output_hwc);
            if (error != TDM_ERROR_NONE)
            {
               _e_output_hwc_windows_commit_handler(output->toutput, 0, 0, 0, output_hwc);
               ERR("tdm_output_commit failed.");

               return EINA_FALSE;
            }

            output_hwc->wait_commit = EINA_TRUE;
         }
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_hwc_windows_pp_commit_possible_check(E_Output_Hwc *output_hwc)
{
   if (!output_hwc->pp_set) return EINA_FALSE;

   if (output_hwc->pp_tqueue)
     {
        if (!tbm_surface_queue_can_dequeue(output_hwc->pp_tqueue, 0))
          return EINA_FALSE;
     }

   if (output_hwc->pending_pp_hwc_window_list)
     {
        if (eina_list_count(output_hwc->pending_pp_hwc_window_list) != 0)
          return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_hwc_windows_zoom_set(E_Output_Hwc *output_hwc, Eina_Rectangle *rect)
{
   E_Comp_Screen *e_comp_screen = NULL;
   tdm_error ret = TDM_ERROR_NONE;
   int w, h;
   Eina_List *l;
   E_Hwc_Window *hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);

   if ((output_hwc->pp_rect.x == rect->x) &&
       (output_hwc->pp_rect.y == rect->y) &&
       (output_hwc->pp_rect.w == rect->w) &&
       (output_hwc->pp_rect.h == rect->h))
     return EINA_TRUE;

   e_comp_screen = e_comp->e_comp_screen;
   e_output_size_get(output_hwc->output, &w, &h);

   if (!output_hwc->tpp)
     {
        output_hwc->tpp = tdm_display_create_pp(e_comp_screen->tdisplay, &ret);
        if (ret != TDM_ERROR_NONE)
          {
             ERR("fail tdm pp create");
             goto fail;
          }
     }

   if (!output_hwc->pp_tqueue)
     {
        //TODO: Does e20 get the buffer flags from the tdm backend?
        output_hwc->pp_tqueue = tbm_surface_queue_create(3, w, h, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
        if (!output_hwc->pp_tqueue)
          {
             ERR("fail tbm_surface_queue_create");
             goto fail;
          }
     }

   output_hwc->pp_rect.x = rect->x;
   output_hwc->pp_rect.y = rect->y;
   output_hwc->pp_rect.w = rect->w;
   output_hwc->pp_rect.h = rect->h;

   output_hwc->pp_set = EINA_TRUE;
   output_hwc->target_hwc_window->skip_surface_set = EINA_TRUE;
   output_hwc->pp_set_info = EINA_TRUE;

   EINA_LIST_FOREACH(output_hwc->hwc_windows, l, hwc_window)
     {
        if (e_hwc_window_state_get(hwc_window) ==E_HWC_WINDOW_STATE_NONE) continue;
        if (e_hwc_window_is_target(hwc_window)) continue;
        if (e_hwc_window_is_video(hwc_window)) continue;

        hwc_window->update_exist = EINA_TRUE;
     }

   /* to wake up main loop */
   uint64_t value = 1;
   if (write(output_hwc->target_hwc_window->event_fd, &value, sizeof(value)) < 0)
     ERR("failed to wake up main loop:%m");

   return EINA_TRUE;

fail:
   if (output_hwc->tpp)
     {
        tdm_pp_destroy(output_hwc->tpp);
        output_hwc->tpp = NULL;
     }

   return EINA_FALSE;
}

EINTERN void
e_output_hwc_windows_zoom_unset(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   output_hwc->pp_set_info = EINA_FALSE;
   output_hwc->target_hwc_window->skip_surface_set = EINA_FALSE;
   output_hwc->pp_set = EINA_FALSE;

   output_hwc->pp_rect.x = 0;
   output_hwc->pp_rect.y = 0;
   output_hwc->pp_rect.w = 0;
   output_hwc->pp_rect.h = 0;

   _e_output_hwc_windows_pp_pending_data_remove(output_hwc);

   if (output_hwc->pp_tsurface)
     tbm_surface_queue_release(output_hwc->pp_tqueue, output_hwc->pp_tsurface);

   if (output_hwc->pp_tqueue)
     {
        tbm_surface_queue_destroy(output_hwc->pp_tqueue);
        output_hwc->pp_tqueue = NULL;
     }

   if (!output_hwc->pp_commit)
     {
        if (output_hwc->tpp)
          {
             tdm_pp_destroy(output_hwc->tpp);
             output_hwc->tpp = NULL;
          }
     }

   if (output_hwc->pp_output_commit_data)
     output_hwc->wait_commit = EINA_TRUE;

   /* to wake up main loop */
   uint64_t value = 1;
   if (write(output_hwc->target_hwc_window->event_fd, &value, sizeof(value)) < 0)
     ERR("failed to wake up main loop:%m");
}
