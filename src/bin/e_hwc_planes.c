#include "e.h"
#include "services/e_service_quickpanel.h"

EINTERN Eina_Bool
e_hwc_planes_init(void)
{
   return EINA_TRUE;
}

EINTERN void
e_hwc_planes_deinit(void)
{
   // TODO:
   ;;;
}

static Eina_Bool
_e_hwc_planes_ec_check(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   E_Output *eout;
   int minw = 0, minh = 0;
   int transform;

   if (ec->comp_override > 0) return EINA_FALSE;

   if (e_comp_object_is_animating(ec->frame)) return EINA_FALSE;

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
      case E_COMP_WL_BUFFER_TYPE_TBM:
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
   transform = e_comp_wl_output_buffer_transform_get(ec);
   if ((eout->config.rotation / 90) != transform)
     {
        if (e_comp_screen_rotation_ignore_output_transform_watch(ec))
          {
            if (e_comp_wl->touch.pressed)
              return EINA_FALSE;
          }
        else
          return EINA_FALSE;
     }


   return EINA_TRUE;
}

static void
_e_hwc_planes_prepare_init(E_Hwc *hwc)
{
   const Eina_List *ep_l = NULL, *l ;
   E_Plane *ep = NULL;
   E_Output *eout = hwc->output;

   EINA_SAFETY_ON_NULL_RETURN(hwc);

   ep_l = e_output_planes_get(eout);
   EINA_LIST_FOREACH(ep_l, l, ep)
     {
        if (!hwc->hwc_use_multi_plane &&
            !e_plane_is_cursor(ep) &&
            !e_plane_is_fb_target(ep))
          continue;

        e_plane_ec_prepare_set(ep, NULL);
     }
}

static int
_e_hwc_planes_prepare_cursor(E_Output *eout, int n_cur, Eina_List *hwc_clist)
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
_e_hwc_planes_prepare_plane(E_Hwc *hwc, int n_vis, int n_skip, Eina_List *hwc_clist)
{
   const Eina_List *ep_l = NULL, *l ;
   Eina_List *hwc_ly = NULL;
   E_Plane *ep = NULL, *ep_fb = NULL;
   int n_ly = 0, n_ec = 0;
   E_Client *ec = NULL;
   Eina_Bool ret = EINA_FALSE;
   int nouse = 0;
   E_Output *eout = hwc->output;

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
        if (!hwc->hwc_use_multi_plane) continue;
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
_e_hwc_planes_cancel(E_Hwc *hwc)
{
   Eina_List *l ;
   E_Plane *ep;
   E_Output *eout = hwc->output;

   EINA_LIST_FOREACH(eout->planes, l, ep)
     {
        if (!hwc->hwc_use_multi_plane &&
            !e_plane_is_cursor(ep) &&
            !e_plane_is_fb_target(ep))
             continue;

        e_plane_ec_prepare_set(ep, NULL);
        e_plane_ec_set(ep, NULL);
     }
}

static Eina_Bool
_e_hwc_planes_reserved_clean(E_Hwc *hwc)
{
   Eina_List *l;
   E_Plane *ep;
   E_Output *eout = hwc->output;

   EINA_LIST_FOREACH(eout->planes, l, ep)
     {
        if (!hwc->hwc_use_multi_plane &&
            !e_plane_is_cursor(ep) &&
            !e_plane_is_fb_target(ep))
          continue;

        if (e_plane_is_reserved(ep))
            e_plane_reserved_set(ep, 0);
     }

   return EINA_TRUE;
}

static void
_e_hwc_planes_unset(E_Plane *ep)
{
   if (e_plane_is_reserved(ep))
     e_plane_reserved_set(ep, 0);

   e_plane_ec_prepare_set(ep, NULL);
   e_plane_ec_set(ep, NULL);

   ELOGF("HWC-PLNS", "unset plane %d to NULL", NULL, ep->zpos);
}

static Eina_Bool
_e_hwc_planes_change_ec(E_Plane *ep, E_Client *new_ec)
{
   if (!e_plane_ec_set(ep, new_ec))
     return EINA_FALSE;

   if (new_ec)
     ELOGF("HWC-PLNS", "new_ec(%s) is set on %d",
           new_ec,
           e_client_util_name_get(new_ec) ? new_ec->icccm.name : "no name", ep->zpos);
   else
     ELOGF("HWC-PLNS", "NULL is set on %d", NULL, ep->zpos);

   return EINA_TRUE;
}

static void
_e_hwc_planes_changed(E_Hwc *hwc)
{
   Eina_Bool ret = EINA_FALSE;
   E_Plane *ep = NULL;
   const Eina_List *ep_l = NULL, *p_l;
   Eina_Bool assign_success = EINA_TRUE;
   int mode = E_HWC_MODE_NONE;
   E_Output *eout = hwc->output;

   ep_l = e_output_planes_get(eout);
   /* check the planes from top to down */
   EINA_LIST_REVERSE_FOREACH(ep_l, p_l, ep)
     {
        if (!assign_success)
          {
             //unset planes from 'assign_success' became EINA_FALSE to the fb target
             _e_hwc_planes_unset(ep);
             continue;
          }

        if (e_plane_is_reserved(ep) &&
            ep->prepare_ec == NULL)
          {
             e_plane_reserved_set(ep, 0);
             ELOGF("HWC-PLNS", "unset reserved mem on %d", NULL, ep->zpos);
          }

        if (ep->ec != ep->prepare_ec)
          {
             assign_success = _e_hwc_planes_change_ec(ep, ep->prepare_ec);
             ret = EINA_TRUE;
          }

        if (ep->ec) mode = E_HWC_MODE_HYBRID;

        if (e_plane_is_fb_target(ep))
          {
             if (ep->ec) mode = E_HWC_MODE_FULL;
             break;
          }
   }

   if (hwc->hwc_mode != mode)
     {
        ELOGF("HWC-PLNS", "mode changed (from %d to %d) due to surface changes",
              NULL,
              hwc->hwc_mode, mode);

        if (mode == E_HWC_MODE_FULL)
          {
             // fb target is occupied by a client surface, means compositor disabled
             ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
          }
        else if (hwc->hwc_mode == E_HWC_MODE_FULL)
          {
             // fb target is occupied by a client surface, means compositor disabled
             ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
          }

        hwc->hwc_mode = mode;
     }

   if (ret)
     {
        if (hwc->hwc_mode == E_HWC_MODE_NONE)
          ELOGF("HWC-PLNS", " End...  due to surface changes", NULL);
        else
          ELOGF("HWC-PLNS", " hwc surface changed", NULL);
     }
}

static Eina_Bool
_e_hwc_planes_prepare(E_Hwc *hwc, E_Zone *zone)
{
   Eina_List *vl;
   Eina_Bool ret = EINA_FALSE;
   E_Client *ec;
   int n_vis = 0, n_ec = 0, n_cur = 0, n_skip = 0;
   Eina_List *hwc_ok_clist = NULL, *vis_clist = NULL;
   E_Output *output = hwc->output;

   _e_hwc_planes_prepare_init(hwc);

   if (e_comp->nocomp_override > 0) return EINA_FALSE;

   vis_clist = e_comp_vis_ec_list_get(zone);
   if (!vis_clist) return EINA_FALSE;

   // check clients not able to use hwc
   EINA_LIST_FOREACH(vis_clist, vl, ec)
     {
        // if there is a ec which is lower than quickpanel and quickpanel is opened.
        if (E_POLICY_QUICKPANEL_LAYER >= evas_object_layer_get(ec->frame))
          {
             // check whether quickpanel is open than break
             if (e_config->use_desk_smart_obj && e_qps_visible_get()) goto done;
          }

        // if ec->frame is not for client buffer (e.g. launchscreen)
        if (e_comp_object_content_type_get(ec->frame) != E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE)
           goto done;

        // if there is UI subfrace, it means need to composite
        if (e_client_normal_client_has(ec))
           goto done;

        // if ec has invalid buffer or scaled( transformed ) or forced composite(never_hwc)
        if (!_e_hwc_planes_ec_check(ec))
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

   if (n_cur >= 1)
     n_skip = _e_hwc_planes_prepare_cursor(output, n_cur, hwc_ok_clist);

   if (n_skip > 0) ret = EINA_TRUE;

   ret |= _e_hwc_planes_prepare_plane(hwc, n_vis, n_skip, hwc_ok_clist);

done:
   eina_list_free(hwc_ok_clist);
   eina_list_free(vis_clist);

   return ret;
}

static Eina_Bool
_e_hwc_planes_usable(E_Hwc *hwc)
{
   E_Output *eout = hwc->output;
   E_Comp_Wl_Buffer *buffer = NULL;
   E_Zone *zone = NULL;
   int bw = 0, bh = 0;
   Eina_Bool all_null = EINA_TRUE;
   E_Plane *ep = NULL;
   const Eina_List *ep_l = NULL, *p_l;

   zone = e_comp_zone_find(e_output_output_id_get(eout));
   EINA_SAFETY_ON_NULL_RETURN_VAL(zone, EINA_FALSE);

   // check whether to use hwc and prepare the core assignment policy
   _e_hwc_planes_prepare(hwc, zone);

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
_e_hwc_planes_can_hwcompose(E_Output *eout)
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
_e_hwc_planes_begin(E_Hwc *hwc)
{
   const Eina_List *ep_l = NULL, *l;
   E_Output *eout = hwc->output;
   E_Plane *ep = NULL;
   E_Hwc_Mode mode = E_HWC_MODE_NONE;
   Eina_Bool set = EINA_FALSE;

   if (_e_hwc_planes_can_hwcompose(eout))
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
                  ELOGF("HWC-PLNS", "is set on fb_target( %d)", ep->prepare_ec, ep->zpos);
                  mode = E_HWC_MODE_FULL;

                  // fb target is occupied by a client surface, means compositor disabled
                  ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
               }
             else
               {
                  ELOGF("HWC-PLNS", "is set on %d", ep->prepare_ec, ep->zpos);
                  mode = E_HWC_MODE_HYBRID;
               }
          }

        if (mode != E_HWC_MODE_NONE)
          ELOGF("HWC-PLNS", " Begin ...", NULL);
     }

   hwc->hwc_mode = mode;
}

static E_Hwc_Mode
_e_hwc_mode_get(E_Hwc *hwc)
{
   const Eina_List *ll = NULL, *l;
   E_Output *output = hwc->output;
   E_Plane *plane = NULL;

   /* check the planes from down to top */
   EINA_LIST_FOREACH_SAFE(output->planes, l, ll, plane)
     {
        if (!plane->ec) continue;
        if (e_plane_is_fb_target(plane)) return E_HWC_MODE_FULL;

        return E_HWC_MODE_HYBRID;
     }

   return E_HWC_MODE_NONE;
}

EINTERN void
e_hwc_planes_multi_plane_set(E_Hwc *hwc, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(hwc);

   e_hwc_planes_end(hwc, __FUNCTION__);
   hwc->hwc_use_multi_plane = set;

   ELOGF("HWC-PLNS", "e_hwc_planes_multi_plane_set : %d", NULL, set);
}

EINTERN Eina_Bool
e_hwc_planes_multi_plane_get(E_Hwc *hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   return hwc->hwc_use_multi_plane;
}

EINTERN void
e_hwc_planes_end(E_Hwc *hwc, const char *location)
{
   E_Hwc_Mode new_mode = E_HWC_MODE_NONE;

   EINA_SAFETY_ON_NULL_RETURN(hwc);

   // once intercept_pol is activated, hwc set/unset should be handled by interceptor
   if (hwc->intercept_pol)
     {
        if (!e_hwc_intercept_hook_call(E_HWC_INTERCEPT_HOOK_END_ALL_PLANE, hwc))
          {
             hwc->intercept_pol = EINA_FALSE;
             goto end;
          }
        else
          return;
     }

   /* clean the reserved planes(clean the candidate ecs) */
   _e_hwc_planes_reserved_clean(hwc);

   if (!hwc->hwc_mode) return;

   /* set null to the e_planes */
   _e_hwc_planes_cancel(hwc);

end:
   /* check the current mode */
   new_mode = _e_hwc_mode_get(hwc);

   if (hwc->hwc_mode == E_HWC_MODE_FULL &&
       new_mode != E_HWC_MODE_FULL)
     ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);

   hwc->hwc_mode = new_mode;

   ELOGF("HWC-PLNS", " End...  at %s.", NULL, location);
}

EINTERN void
e_hwc_planes_client_end(E_Hwc *hwc, E_Client *ec, const char *location)
{
   const Eina_List *planes;
   E_Plane *plane;
   E_Output *output;
   const Eina_List *l;
   int old_hwc, new_hwc;

   EINA_SAFETY_ON_NULL_RETURN(hwc);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   if (!hwc->hwc_use_multi_plane)
     {
        e_hwc_planes_end(hwc, __FUNCTION__);
        return;
     }

   output = hwc->output;
   EINA_SAFETY_ON_NULL_RETURN(output);

   planes = e_output_planes_get(output);
   if (!planes) return;

   old_hwc = _e_hwc_mode_get(hwc);

   EINA_LIST_FOREACH(planes, l, plane)
     {
        if (!plane->ec)
          {
             if (e_plane_is_reserved(plane))
               e_plane_reserved_set(plane, 0);
             continue;
          }

        if (plane->ec != ec)
          {
             if (!e_hwc_client_is_above_hwc(ec, plane->ec))
               continue;
          }

        if (e_plane_is_reserved(plane))
          e_plane_reserved_set(plane, 0);

        e_plane_ec_prepare_set(plane, NULL);
        e_plane_ec_set(plane, NULL);
     }

   new_hwc = _e_hwc_mode_get(hwc);
   if ((old_hwc == E_HWC_MODE_FULL) && (new_hwc != E_HWC_MODE_FULL))
     ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);

   ELOGF("HWC-PLNS", " End client:(%s)...  at %s.", ec, e_client_util_name_get(ec), location);

   hwc->hwc_mode = new_hwc;
}

EINTERN void
e_hwc_planes_apply(E_Hwc *hwc)
{
   EINA_SAFETY_ON_NULL_RETURN(hwc);
   EINA_SAFETY_ON_NULL_RETURN(hwc->output);

   if (e_hwc_policy_get(hwc) == E_HWC_POLICY_NONE ||
       e_hwc_policy_get(hwc) == E_HWC_POLICY_WINDOWS) return;

   if (e_hwc_deactive_get(hwc))
     {
        if (hwc->hwc_mode != E_HWC_MODE_NONE)
          e_hwc_planes_end(hwc, "deactive set.");
        return;
     }

   // intercept hwc policy
   if (!e_hwc_intercept_hook_call(E_HWC_INTERCEPT_HOOK_PREPARE_PLANE, hwc))
     {
        hwc->intercept_pol = EINA_TRUE;
        return;
     }

   if (!_e_hwc_planes_usable(hwc))
     {
        e_hwc_planes_end(hwc, __FUNCTION__);
        return;
     }

   if (hwc->hwc_mode == E_HWC_MODE_NONE)
     _e_hwc_planes_begin(hwc);
   else
     _e_hwc_planes_changed(hwc);
}
