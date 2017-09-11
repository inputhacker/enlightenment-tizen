#include "e.h"

static int
_hwc_set(E_Output *eout)
{
   const Eina_List *ep_l = NULL, *l;
   E_Plane *ep = NULL;
   E_Output_Hwc_Mode mode = E_OUTPUT_HWC_MODE_NO;

   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout->planes, EINA_FALSE);

   ep_l = e_output_planes_get(eout);
   EINA_LIST_REVERSE_FOREACH(ep_l, l , ep)
     {
        Eina_Bool set = EINA_FALSE;

        if (e_plane_is_fb_target(ep))
          {
             if (ep->prepare_ec)
               {
                  set = e_plane_ec_set(ep, ep->prepare_ec);
                  if (set)
                    {
                       ELOGF("HWC", "is set on fb_target( %d)", ep->prepare_ec->pixmap, ep->prepare_ec, ep->zpos);
                       mode = E_OUTPUT_HWC_MODE_FULL;

                       // fb target is occupied by a client surface, means compositor disabled
                       ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
                    }
                  break;
               }
          }
        else if (ep->prepare_ec)
          {
             set = e_plane_ec_set(ep, ep->prepare_ec);
             if (set)
               {
                  ELOGF("HWC", "is set on %d", ep->prepare_ec->pixmap, ep->prepare_ec, ep->zpos);
                  mode |= E_OUTPUT_HWC_MODE_HYBRID;
               }
             else
               break;
          }
     }

   return mode;
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

static Eina_Bool
_hwc_cancel(E_Output_Hwc *output_hwc)
{
   Eina_List *l ;
   E_Plane *ep;
   Eina_Bool ret = EINA_TRUE;
   E_Output *eout = output_hwc->output;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout->planes, EINA_FALSE);

   EINA_LIST_FOREACH(eout->planes, l, ep)
     {
        if (!output_hwc->hwc_use_multi_plane &&
            !e_plane_is_cursor(ep) &&
            !e_plane_is_fb_target(ep))
          {
             if (ep->ec) ret = EINA_FALSE; // core cannot end HWC
             continue;
          }

        e_plane_ec_prepare_set(ep, NULL);
        e_plane_ec_set(ep, NULL);
     }

   return ret;
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
_hwc_plane_change_ec(E_Plane *ep, E_Client *old_ec, E_Client *new_ec)
{
   Eina_Bool ret = EINA_FALSE;

   if (!new_ec)
     {
        if (e_plane_is_reserved(ep))
          e_plane_reserved_set(ep, 0);
     }

   e_plane_ec_prepare_set(ep, NULL);

   if (e_plane_ec_set(ep, new_ec))
     {
        if (new_ec)
          {
             ELOGF("HWC", "new_ec(%s) is set on %d",
                   new_ec->pixmap, new_ec,
                   e_client_util_name_get(new_ec) ? new_ec->icccm.name : "no name", ep->zpos);
          }
        else
          {
             ELOGF("HWC", "NULL is set on %d", NULL, NULL, ep->zpos);
          }
        ret = EINA_TRUE;
     }
   else
     {
        ELOGF("HWC", "failed to set new_ec(%s) on %d",
              NULL, new_ec,
              new_ec ? (new_ec->icccm.name ? new_ec->icccm.name : "no name") : "NULL",
              ep->zpos);
     }

   return ret;
}

static Eina_Bool
_e_output_hwc_apply(E_Output_Hwc *output_hwc)
{
   const Eina_List *ep_l = NULL, *l;
   E_Plane *ep = NULL, *ep_fb = NULL;
   int mode = 0;
   E_Output *eout = output_hwc->output;

   ep_l = e_output_planes_get(eout);
   EINA_LIST_FOREACH(ep_l, l, ep)
     {
        if (!ep_fb)
          {
             if (e_plane_is_fb_target(ep))
               {
                  ep_fb = ep;
                  if (ep->prepare_ec != NULL) goto hwcompose;
               }
             continue;
          }
        if (ep->zpos > ep_fb->zpos)
          if (ep->prepare_ec != NULL) goto hwcompose;
     }

   goto compose;

hwcompose:
   mode = _hwc_set(eout);
   if (mode == E_OUTPUT_HWC_MODE_NO) ELOGF("HWC", "it is failed to assign surface on plane", NULL, NULL);

compose:
   if (mode != E_OUTPUT_HWC_MODE_NO) output_hwc->hwc_mode = mode;

   return !!mode;
}

static Eina_Bool
_e_output_hwc_changed(E_Output_Hwc *output_hwc)
{
   Eina_Bool ret = EINA_FALSE;
   E_Plane *ep = NULL;
   const Eina_List *ep_l = NULL, *p_l;
   Eina_Bool assign_success = EINA_TRUE;
   int mode = E_OUTPUT_HWC_MODE_NO;
   E_Output *eout = output_hwc->output;

   ep_l = e_output_planes_get(eout);
   EINA_LIST_REVERSE_FOREACH(ep_l, p_l, ep)
     {
        if (!assign_success)
          {
             //unset planes from 'assign_success' became EINA_FALSE to the fb target
             _hwc_plane_unset(ep);
             continue;
          }

        if (ep->ec != ep->prepare_ec)
          {
             assign_success = _hwc_plane_change_ec(ep, ep->ec, ep->prepare_ec);
             ret = EINA_TRUE;
          }
        else if (!ep->prepare_ec)
          {
             if (e_plane_is_reserved(ep))
               {
                  e_plane_reserved_set(ep, 0);
                  ELOGF("HWC", "unset reserved mem on %d", NULL, NULL, ep->zpos);
               }
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

   return ret;
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

   zone = e_comp_zone_find(e_output_output_id_get(eout));
   EINA_SAFETY_ON_NULL_RETURN_VAL(zone, EINA_FALSE);

   // check whether to use hwc and prepare the core assignment policy
   if (!_e_output_hwc_prepare(output_hwc, zone)) return EINA_FALSE;

   // extra policy can replace core policy
   e_comp_hook_call(E_COMP_HOOK_PREPARE_PLANE, NULL);

   // check the hwc is avaliable.
   E_Plane *ep = NULL, *ep_fb = NULL;
   const Eina_List *ep_l = NULL, *p_l;

   ep_l = e_output_planes_get(eout);

   // It is not hwc_usable if cursor is shown when the hw cursor is not supported.
   if ((eout->cursor_available.max_w == -1) ||
       (eout->cursor_available.max_h == -1))
     {
        // hw cursor is not supported by libtdm, than let's composite
        if (!e_pointer_is_hidden(e_comp->pointer)) return EINA_FALSE;
     }

   ep_fb = e_output_fb_target_get(eout);
   if (!ep_fb) return EINA_FALSE;

   if (ep_fb->prepare_ec)
     {
        // It is not hwc_usable if the geometry of the prepare_ec at the ep_fb is not proper.
        int bw = 0, bh = 0;

        // It is not hwc_usable if attached buffer is not valid.
        buffer = e_pixmap_resource_get(ep_fb->prepare_ec->pixmap);
        if (!buffer) return EINA_FALSE;

        e_pixmap_size_get(ep_fb->prepare_ec->pixmap, &bw, &bh);

        // if client and zone's geometry is not match with, or
        // if plane with reserved_memory(esp. fb target) has assigned smaller buffer,
        // won't support hwc properly, than let's composite
        if (ep_fb->reserved_memory &&
            ((bw != zone->w) || (bh != zone->h) ||
            (ep_fb->prepare_ec->x != zone->x) || (ep_fb->prepare_ec->y != zone->y) ||
            (ep_fb->prepare_ec->w != zone->w) || (ep_fb->prepare_ec->h != zone->h)))
          {
             DBG("Cannot use HWC if geometry is not 1 on 1 match with reserved_memory");
             return EINA_FALSE;
          }
     }
   else
     {
        // It is not hwc_usable if the all prepare_ec in every plane are null
        Eina_Bool all_null = EINA_TRUE;

        EINA_LIST_FOREACH(ep_l, p_l, ep)
          {
             if (ep == ep_fb) continue;
             if (ep->prepare_ec)
               {
                  // if attached buffer is not valid, hwc is not usable
                  buffer = e_pixmap_resource_get(ep->prepare_ec->pixmap);
                  if (!buffer) return EINA_FALSE;

                  // It is not hwc_usable if the zpos of the ep is over the one of ep_fb
                  if (ep->zpos < ep_fb->zpos) return EINA_FALSE;

                  all_null = EINA_FALSE;
                  break;
               }
          }
        if (all_null) return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_output_hwc_begin(E_Output_Hwc *output_hwc)
{
   Eina_Bool mode_set = EINA_FALSE;

   if (e_comp->nocomp_override > 0) return;

   mode_set = _e_output_hwc_apply(output_hwc);
   if (!mode_set) return;
   if (!output_hwc->hwc_mode) return;

   ELOGF("HWC", " Begin ...", NULL, NULL);
}

EINTERN void
e_output_hwc_end(E_Output_Hwc *output_hwc, const char *location)
{
   Eina_Bool mode_set = EINA_FALSE;
   E_Zone *zone;
   Eina_List *l;
   Eina_Bool fully_hwc;

   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

  fully_hwc = (output_hwc->hwc_mode == E_OUTPUT_HWC_MODE_FULL) ? EINA_TRUE : EINA_FALSE;

   _hwc_reserved_clean(output_hwc);

   if (!e_comp->hwc) return;
   if (!output_hwc->hwc_mode) return;

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        E_Output * eout;
        if (!zone->output_id) continue;
        eout = e_output_find(zone->output_id);
        if (eout) mode_set |= _hwc_cancel(output_hwc);
     }

   if (!mode_set) return;

   output_hwc->hwc_mode = E_OUTPUT_HWC_MODE_NO;

   if (fully_hwc)
     ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);

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

   if(_e_output_hwc_usable(output_hwc))
     {
        if (output_hwc->hwc_mode)
          {
             if (_e_output_hwc_changed(output_hwc))
               {
                  if (output_hwc->hwc_mode == E_OUTPUT_HWC_MODE_NO)
                    ELOGF("HWC", " End...  due to surface changes", NULL, NULL);
                  else
                    ELOGF("HWC", " hwc surface changed", NULL, NULL);
               }
          }
        else
          _e_output_hwc_begin(output_hwc);
     }
   else
     e_output_hwc_end(output_hwc, __FUNCTION__);
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

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   output_hwc = E_NEW(E_Output_Hwc, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, NULL);

   output_hwc->output = output;

   return output_hwc;
}

EINTERN void
e_output_hwc_del(E_Output_Hwc *output_hwc)
{
   if (!output_hwc) return;

   E_FREE(output_hwc);
}
