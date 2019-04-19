#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "e_video_internal.h"
#include "e_video_hwc.h"

#define IFACE_ENTRY                                      \
   E_Video_Hwc_Windows *evhw;                              \
   evhw = container_of(iface, E_Video_Hwc_Windows, base.backend)

typedef struct _E_Video_Hwc_Windows E_Video_Hwc_Windows;

struct _E_Video_Hwc_Windows
{
   E_Video_Hwc base;

   E_Client_Video_Info info;

   E_Hwc *hwc;
   E_Hwc_Window *hwc_window;

   tbm_surface_h cur_tsurface;

   E_Comp_Wl_Hook *hook_subsurf_create;

   Eina_Bool wait_commit_data_release;
};

static void _e_video_commit_from_waiting_list(E_Video_Hwc_Windows *evhw);

static void
_e_video_hwc_windows_commit_done(E_Video_Hwc_Windows *evhw)
{
   if (!e_video_hwc_commit_done((E_Video_Hwc *)evhw))
     return;

   if (evhw->base.waiting_list)
     _e_video_commit_from_waiting_list(evhw);
}

static Eina_Bool
_e_video_frame_buffer_show(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Video_Buf *vbuf)
{
   /* show means that set the information of the buffer and the info of the hwc window */

   if (!vbuf) return EINA_TRUE;

   CLEAR(evhw->info);
   evhw->info.src_config.size.h = vbuf->width_from_pitch;
   evhw->info.src_config.size.v = vbuf->height_from_size;
   evhw->info.src_config.pos.x = vbuf->content_r.x;
   evhw->info.src_config.pos.y = vbuf->content_r.y;
   evhw->info.src_config.pos.w = vbuf->content_r.w;
   evhw->info.src_config.pos.h = vbuf->content_r.h;
   evhw->info.src_config.format = vbuf->tbmfmt;
   evhw->info.dst_pos.x = evhw->base.geo.tdm.output_r.x;
   evhw->info.dst_pos.y = evhw->base.geo.tdm.output_r.y;
   evhw->info.dst_pos.w = evhw->base.geo.tdm.output_r.w;
   evhw->info.dst_pos.h = evhw->base.geo.tdm.output_r.h;
   evhw->info.transform = vbuf->content_t;

   evhw->cur_tsurface = vbuf->tbm_surface;

   evhw->wait_commit_data_release = EINA_TRUE;

   // TODO:: this logic move to the hwc windows after hwc commit
#if 1
   E_Client *topmost;

   topmost = e_comp_wl_topmost_parent_get(evhw->base.ec);
   if (topmost && topmost->argb && !e_comp_object_mask_has(evhw->base.ec->frame))
     {
        Eina_Bool do_punch = EINA_TRUE;

        /* FIXME: the mask obj can be drawn at the wrong position in the beginnig
         * time. It happens caused by window manager policy.
         */
        if ((topmost->fullscreen || topmost->maximized) &&
            (evhw->base.geo.output_r.x == 0 || evhw->base.geo.output_r.y == 0))
          {
             int bw, bh;

             e_pixmap_size_get(topmost->pixmap, &bw, &bh);

             if (bw > 100 && bh > 100 &&
                 evhw->base.geo.output_r.w < 100 && evhw->base.geo.output_r.h < 100)
               {
                  VIN("don't punch. (%dx%d, %dx%d)", evhw->base.ec,
                      bw, bh, evhw->base.geo.output_r.w, evhw->base.geo.output_r.h);
                  do_punch = EINA_FALSE;
               }
          }

        if (do_punch)
          {
             e_comp_object_mask_set(evhw->base.ec->frame, EINA_TRUE);
             VIN("punched", evhw->base.ec);
          }
     }

   if (e_video_debug_punch_value_get())
     {
        e_comp_object_mask_set(evhw->base.ec->frame, EINA_TRUE);
        VIN("punched", evhw->base.ec);
     }
#endif

   DBG("Client(%s):PID(%d) RscID(%d), Buffer(%p, refcnt:%d) is shown."
       "Geometry details are : buffer size(%dx%d) src(%d,%d, %dx%d)"
       " dst(%d,%d, %dx%d), transform(%d)",
       e_client_util_name_get(evhw->base.ec) ?: "No Name" , evhw->base.ec->netwm.pid,
       wl_resource_get_id(evhw->base.ec->comp_data->surface), vbuf, vbuf->ref_cnt,
       evhw->info.src_config.size.h, evhw->info.src_config.size.v, evhw->info.src_config.pos.x,
       evhw->info.src_config.pos.y, evhw->info.src_config.pos.w, evhw->info.src_config.pos.h,
       evhw->info.dst_pos.x, evhw->info.dst_pos.y, evhw->info.dst_pos.w, evhw->info.dst_pos.h, evhw->info.transform);


   return EINA_TRUE;
}

static void
_e_video_commit_buffer(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Video_Buf *vbuf)
{
   evhw->base.committed_list = eina_list_append(evhw->base.committed_list, vbuf);

   if (!e_video_hwc_can_commit((E_Video_Hwc *)evhw))
     goto no_commit;

   if (!_e_video_frame_buffer_show(evhw, vbuf))
     goto no_commit;

   return;

no_commit:
   _e_video_hwc_windows_commit_done(evhw);
}

static void
_e_video_commit_from_waiting_list(E_Video_Hwc_Windows *evhw)
{
   E_Comp_Wl_Video_Buf *vbuf;

   vbuf = eina_list_nth(evhw->base.waiting_list, 0);
   evhw->base.waiting_list = eina_list_remove(evhw->base.waiting_list, vbuf);

   _e_video_commit_buffer(evhw, vbuf);
}

EINTERN Eina_Bool
e_video_hwc_windows_frame_buffer_show(E_Video_Hwc *evh, E_Comp_Wl_Video_Buf *vbuf)
{
   E_Video_Hwc_Windows *evhw;

   evhw = (E_Video_Hwc_Windows *)evh;
   return _e_video_frame_buffer_show(evhw, vbuf);
}

static void
_e_video_buffer_show(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Video_Buf *vbuf, unsigned int transform)
{
   vbuf->content_t = transform;

   e_comp_wl_video_buffer_set_use(vbuf, EINA_TRUE);

   if (vbuf->comp_buffer)
     e_comp_wl_buffer_reference(&vbuf->buffer_ref, vbuf->comp_buffer);

   if (evhw->wait_commit_data_release)
     {
        evhw->base.waiting_list = eina_list_append(evhw->base.waiting_list, vbuf);
        VDB("There are waiting fbs more than 1", evhw->base.ec);
        return;
     }

   _e_video_commit_buffer(evhw, vbuf);
}

EINTERN void
e_video_hwc_windows_buffer_show(E_Video_Hwc *evh, E_Comp_Wl_Video_Buf *vbuf, unsigned int transform)
{
   E_Video_Hwc_Windows *evhw;

   evhw = (E_Video_Hwc_Windows *)evh;
   _e_video_buffer_show(evhw, vbuf, transform);
}

static Eina_Bool
_e_video_hwc_windows_init(E_Video_Hwc_Windows *evhw)
{
   E_Hwc *hwc;
   E_Hwc_Window *hwc_window;

   hwc = evhw->base.e_output->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);
   hwc_window = e_hwc_window_new(hwc, evhw->base.ec, E_HWC_WINDOW_STATE_VIDEO);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   evhw->hwc_window = hwc_window;
   evhw->hwc = hwc;

   return EINA_TRUE;
}

static void
_e_video_destroy(E_Video_Hwc_Windows *evhw)
{
   if (!evhw)
     return;

   VIN("destroy", evhw->base.ec);

   e_hwc_window_free(evhw->hwc_window);

   free(evhw);

#if 0
   if (e_comp_wl_video_buffer_list_length() > 0)
     e_comp_wl_video_buffer_list_print(NULL);
#endif
}

EINTERN Eina_Bool
e_video_hwc_windows_check_if_pp_needed(E_Video_Hwc *evh)
{
   E_Video_Hwc_Windows *evhw;
   int i, count = 0;
   const tbm_format *formats;
   Eina_Bool found = EINA_FALSE;
   E_Hwc *hwc;

   evhw = (E_Video_Hwc_Windows *)evh;
   hwc = evhw->hwc;
   if (hwc->tdm_hwc_video_stream)
     return EINA_FALSE;

   if (!e_comp_screen_available_video_formats_get(&formats, &count))
     return EINA_FALSE;

   for (i = 0; i < count; i++)
     if (formats[i] == evhw->base.tbmfmt)
       {
          found = EINA_TRUE;
          break;
       }

   if (!found)
     {
        if (formats && count > 0)
          evhw->base.pp_tbmfmt = formats[0];
        else
          {
             WRN("No layer format information!!!");
             evhw->base.pp_tbmfmt = TBM_FORMAT_ARGB8888;
          }
        return EINA_TRUE;
     }

   if (hwc->tdm_hwc_video_scanout)
     goto need_pp;

   /* check size */
   if (evhw->base.geo.input_r.w != evhw->base.geo.output_r.w || evhw->base.geo.input_r.h != evhw->base.geo.output_r.h)
     if (!hwc->tdm_hwc_video_scale)
       goto need_pp;

   /* check rotate */
   if (evhw->base.geo.transform || e_comp->e_comp_screen->rotation > 0)
     if (!hwc->tdm_hwc_video_transform)
       goto need_pp;

   return EINA_FALSE;

need_pp:
   evhw->base.pp_tbmfmt = evhw->base.tbmfmt;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_cb_ec_visibility_change(void *data, int type, void *event)
{
   E_Event_Remote_Surface_Provider *ev;
   E_Client *ec, *offscreen_parent;
   E_Video_Hwc_Windows *evhw;

   evhw = data;
   offscreen_parent = e_video_hwc_client_offscreen_parent_get(evhw->base.ec);
   if (!offscreen_parent)
     goto end;

   ev = event;
   ec = ev->ec;
   if (offscreen_parent != ec)
     goto end;

   switch (ec->visibility.obscured)
   {
       case E_VISIBILITY_FULLY_OBSCURED:
           evas_object_hide(evhw->base.ec->frame);
           break;
       case E_VISIBILITY_UNOBSCURED:
           evas_object_show(evhw->base.ec->frame);
           break;
       default:
           VER("Not implemented", evhw->base.ec);
           return ECORE_CALLBACK_PASS_ON;
   }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_video_ec_visibility_event_free(void *d EINA_UNUSED, E_Event_Client *ev)
{
   e_object_unref(E_OBJECT(ev->ec));
   free(ev);
}

static void
_e_video_ec_visibility_event_send(E_Client *ec)
{
   E_Event_Client *ev;
   int obscured;

   obscured = ec->visibility.obscured;
   ELOGF("VIDEO <INF>", "Signal visibility change event of video, type %d",
         ec, obscured);

   ev = E_NEW(E_Event_Client, 1);
   if (!ev) return;
   ev->ec = ec;
   e_object_ref(E_OBJECT(ec));
   ecore_event_add(E_EVENT_CLIENT_VISIBILITY_CHANGE, ev,
                   (Ecore_End_Cb)_e_video_ec_visibility_event_free, NULL);
}

static void
_e_video_hwc_windows_ec_visibility_set(E_Client *ec, E_Visibility vis)
{
   if (ec->visibility.obscured == vis)
     return;

   ec->visibility.obscured = vis;
   _e_video_ec_visibility_event_send(ec);
}

static Eina_Bool
_e_video_cb_topmost_ec_visibility_change(void *data, int type, void *event)
{
   E_Video_Hwc_Windows *evhw;
   E_Event_Client *ev;
   E_Client *topmost;

   ev = event;
   evhw = data;
   if (!evhw->base.follow_topmost_visibility)
       goto end;

   topmost = e_comp_wl_topmost_parent_get(evhw->base.ec);
   if (!topmost) goto end;
   if (topmost != ev->ec) goto end;
   if (topmost == evhw->base.ec) goto end;

   /* Update visibility of video client by changing visibility of topmost client */
   _e_video_hwc_windows_ec_visibility_set(evhw->base.ec, topmost->visibility.obscured);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_video_hwc_windows_ec_event_deinit(E_Video_Hwc_Windows *evhw)
{
   E_FREE_FUNC(evhw->hook_subsurf_create, e_comp_wl_hook_del);
   E_FREE_LIST(evhw->base.ec_event_handler, ecore_event_handler_del);
}

const char *
_e_video_hwc_windows_prop_name_get_by_id(E_Video_Hwc_Windows *evhw, unsigned int id)
{
   const tdm_prop *props;
   int i, count = 0;

   e_hwc_windows_get_video_available_properties(evhw->hwc, &props, &count);
   for (i = 0; i < count; i++)
     {
        if (props[i].id == id)
          {
             VDB("check property(%s)", evhw->base.ec, props[i].name);
             return props[i].name;
          }
     }

   VER("No available property: id %d", evhw->base.ec, id);

   return NULL;
}

static void
_e_video_hwc_windows_cb_hook_subsurface_create(void *data, E_Client *ec)
{
   E_Video_Hwc_Windows *evhw;
   E_Client *topmost1, *topmost2;

   evhw = data;
   if (!evhw->base.follow_topmost_visibility)
     return;

   /* This is to raise an 'VISIBILITY_CHANGE' event to video client when its
    * topmost ancestor is changed. The reason why it uses hook handler of
    * creation of subsurface is that there is no event for like parent change,
    * and being created subsurface that has common topmost parent means
    * it implies topmost parent has been possibly changed. */
   topmost1 = e_comp_wl_topmost_parent_get(ec);
   topmost2 = e_comp_wl_topmost_parent_get(evhw->base.ec);
   if (topmost1 && topmost2)
     {
        if (topmost1 == topmost2)
          _e_video_hwc_windows_ec_visibility_set(evhw->base.ec, topmost1->visibility.obscured);
     }
}

static void
_e_video_hwc_windows_ec_event_init(E_Video_Hwc_Windows *evhw)
{
   evhw->hook_subsurf_create =
      e_comp_wl_hook_add(E_COMP_WL_HOOK_SUBSURFACE_CREATE,
                         _e_video_hwc_windows_cb_hook_subsurface_create, evhw);

   E_LIST_HANDLER_APPEND(evhw->base.ec_event_handler, E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE,
                         _e_video_cb_ec_visibility_change, evhw);
   E_LIST_HANDLER_APPEND(evhw->base.ec_event_handler, E_EVENT_CLIENT_VISIBILITY_CHANGE,
                         _e_video_cb_topmost_ec_visibility_change, evhw);
}

static void
_e_video_hwc_windows_iface_destroy(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   _e_video_hwc_windows_ec_event_deinit(evhw);
   _e_video_destroy(evhw);
}

static Eina_Bool
_e_video_hwc_windows_iface_property_get(E_Video_Comp_Iface *iface, unsigned int id, tdm_value *value)
{
   tdm_error ret;

   IFACE_ENTRY;

   ret = tdm_hwc_window_get_property(evhw->hwc_window->thwc_window, id, value);
   if (ret != TDM_ERROR_NONE)
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_property_set(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value)
{
   const char *name;

   IFACE_ENTRY;

   VIN("set_attribute", evhw->base.ec);

   name = _e_video_hwc_windows_prop_name_get_by_id(evhw, id);
   if (!name)
   {
      VER("_e_video_hwc_windows_prop_name_get_by_id failed", evhw->base.ec);
      return EINA_FALSE;
   }

   if (evhw->base.allowed_attribute)
     {
        VIN("set_attribute now : property(%s), value(%d)", evhw->base.ec, name, value.u32);

        /* set the property on the fly */
        if (!e_hwc_window_set_property(evhw->hwc_window, id, name, value, EINA_TRUE))
          {
             VER("set property failed", evhw->base.ec);
             return EINA_FALSE;
          }
     }
   else
     {
        VIN("set_attribute at commit : property(%s), value(%d)", evhw->base.ec, name, value.u32);

        /* set the property before hwc commit */
        if (!e_hwc_window_set_property(evhw->hwc_window, id, name, value, EINA_FALSE))
          {
             VER("set property failed", evhw->base.ec);
             return EINA_FALSE;
          }
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_available_properties_get(E_Video_Comp_Iface *iface, const tdm_prop **props, int *count)
{
   IFACE_ENTRY;

   if (!e_hwc_windows_get_video_available_properties(evhw->hwc, props, count))
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_info_get(E_Video_Comp_Iface *iface, E_Client_Video_Info *info)
{
   IFACE_ENTRY;

   memcpy(&info->src_config, &evhw->info.src_config, sizeof(tdm_info_config));
   memcpy(&info->dst_pos, &evhw->info.dst_pos, sizeof(tdm_pos));
   info->transform = evhw->info.transform;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_commit_data_release(E_Video_Comp_Iface *iface, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec)
{
   IFACE_ENTRY;

   evhw->wait_commit_data_release = EINA_FALSE;

   _e_video_hwc_windows_commit_done(evhw);

   return EINA_TRUE;
}

static tbm_surface_h
_e_video_hwc_windows_iface_tbm_surface_get(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   return evhw->cur_tsurface;
}

EINTERN E_Video_Hwc *
e_video_hwc_windows_create(void)
{
   E_Video_Hwc_Windows *evhw;

   evhw = calloc(1, sizeof *evhw);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhw, NULL);

   return (E_Video_Hwc *)evhw;
}

EINTERN Eina_Bool
e_video_hwc_windows_init(E_Video_Hwc *evh)
{
   E_Video_Hwc_Windows *evhw;

   INF("Initializing HWC Windows mode");

   evhw = (E_Video_Hwc_Windows *)evh;
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhw, EINA_FALSE);

   if (!_e_video_hwc_windows_init(evhw))
     {
        ERR("Failed to init 'E_Video_Hwc_Windows'");
        return EINA_FALSE;
     }

   _e_video_hwc_windows_ec_event_init(evhw);

   evhw->base.backend.destroy = _e_video_hwc_windows_iface_destroy;
   evhw->base.backend.property_get = _e_video_hwc_windows_iface_property_get;
   evhw->base.backend.property_set = _e_video_hwc_windows_iface_property_set;
   evhw->base.backend.property_delay_set = NULL;
   evhw->base.backend.available_properties_get = _e_video_hwc_windows_iface_available_properties_get;
   evhw->base.backend.info_get = _e_video_hwc_windows_iface_info_get;
   evhw->base.backend.commit_data_release = _e_video_hwc_windows_iface_commit_data_release;
   evhw->base.backend.tbm_surface_get = _e_video_hwc_windows_iface_tbm_surface_get;

   return EINA_TRUE;
}

EINTERN tbm_surface_h
e_video_hwc_windows_displaying_buffer_get(E_Video_Hwc *evh)
{
   E_Video_Hwc_Windows *evhw;

   evhw = (E_Video_Hwc_Windows *)evh;
   return evhw->cur_tsurface;
}
