#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "e_video_internal.h"
#include "e_video_hwc.h"

typedef struct _E_Video_Hwc_Windows E_Video_Hwc_Windows;

struct _E_Video_Hwc_Windows
{
   E_Video_Hwc base;

   E_Hwc *hwc;
   E_Hwc_Window *hwc_window;

   struct
     {
        E_Client_Video_Info info;
        tbm_surface_h buffer;
        Eina_Bool wait_release;
     } commit_data;
};

static void
_e_video_hwc_windows_commit_data_set(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Video_Buf *vbuf)
{
   CLEAR(evhw->commit_data.info);

   /* Set src_config */
   evhw->commit_data.info.src_config.size.h = vbuf->width_from_pitch;
   evhw->commit_data.info.src_config.size.v = vbuf->height_from_size;
   evhw->commit_data.info.src_config.pos.x = vbuf->content_r.x;
   evhw->commit_data.info.src_config.pos.y = vbuf->content_r.y;
   evhw->commit_data.info.src_config.pos.w = vbuf->content_r.w;
   evhw->commit_data.info.src_config.pos.h = vbuf->content_r.h;
   evhw->commit_data.info.src_config.format = vbuf->tbmfmt;

   /* Set dst_pos */
   evhw->commit_data.info.dst_pos.x = evhw->base.geo.tdm.output_r.x;
   evhw->commit_data.info.dst_pos.y = evhw->base.geo.tdm.output_r.y;
   evhw->commit_data.info.dst_pos.w = evhw->base.geo.tdm.output_r.w;
   evhw->commit_data.info.dst_pos.h = evhw->base.geo.tdm.output_r.h;

   /* Set transform */
   evhw->commit_data.info.transform = vbuf->content_t;

   /* Set buffer */
   evhw->commit_data.buffer = vbuf->tbm_surface;

   /* Set flag to wait until commit data is released. Otherwise, it maybe loses
    * frame buffer. */
   evhw->commit_data.wait_release = EINA_TRUE;

   DBG("Client(%s):PID(%d) RscID(%d), Buffer(%p, refcnt:%d) is shown."
       "Geometry details are : buffer size(%dx%d) src(%d,%d, %dx%d)"
       " dst(%d,%d, %dx%d), transform(%d)",
       e_client_util_name_get(evhw->base.ec) ?: "No Name" ,
       evhw->base.ec->netwm.pid,
       wl_resource_get_id(evhw->base.ec->comp_data->surface),
       vbuf, vbuf->ref_cnt,
       evhw->commit_data.info.src_config.size.h,
       evhw->commit_data.info.src_config.size.v,
       evhw->commit_data.info.src_config.pos.x,
       evhw->commit_data.info.src_config.pos.y,
       evhw->commit_data.info.src_config.pos.w,
       evhw->commit_data.info.src_config.pos.h,
       evhw->commit_data.info.dst_pos.x, evhw->commit_data.info.dst_pos.y,
       evhw->commit_data.info.dst_pos.w, evhw->commit_data.info.dst_pos.h,
       evhw->commit_data.info.transform);
}

static void
_e_video_destroy(E_Video_Hwc_Windows *evhw)
{
   e_hwc_window_free(evhw->hwc_window);
   free(evhw);
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
_e_video_hwc_windows_iface_destroy(E_Video_Hwc *evh)
{
   E_Video_Hwc_Windows *evhw;

   evhw = (E_Video_Hwc_Windows *)evh;
   _e_video_destroy(evhw);
}

static Eina_Bool
_e_video_hwc_windows_iface_property_get(E_Video_Hwc *evh, unsigned int id, tdm_value *value)
{
   E_Video_Hwc_Windows *evhw;
   tdm_error ret;

   evhw = (E_Video_Hwc_Windows *)evh;
   ret = tdm_hwc_window_get_property(evhw->hwc_window->thwc_window, id, value);
   if (ret != TDM_ERROR_NONE)
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_property_set(E_Video_Hwc *evh, unsigned int id, tdm_value value)
{
   E_Video_Hwc_Windows *evhw;
   const char *name;

   evhw = (E_Video_Hwc_Windows *)evh;
   VIN("set_attribute", evhw->base.ec);

   name = _e_video_hwc_windows_prop_name_get_by_id(evhw, id);
   if (!name)
   {
      VER("_e_video_hwc_windows_prop_name_get_by_id failed", evhw->base.ec);
      return EINA_FALSE;
   }

   if (e_client_video_property_allow_get(evhw->base.ecv))
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
_e_video_hwc_windows_iface_available_properties_get(E_Video_Hwc *evh, const tdm_prop **props, int *count)
{
   E_Video_Hwc_Windows *evhw;

   evhw = (E_Video_Hwc_Windows *)evh;
   if (!e_hwc_windows_get_video_available_properties(evhw->hwc, props, count))
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_buffer_commit(E_Video_Hwc *evh, E_Comp_Wl_Video_Buf *vbuf)
{
   E_Video_Hwc_Windows *evhw;

   evhw = (E_Video_Hwc_Windows *)evh;

   /* show means that set the information of the buffer and the info of the hwc window */

   if (!vbuf) return EINA_TRUE;

   _e_video_hwc_windows_commit_data_set(evhw, vbuf);

   // TODO:: this logic move to the hwc windows after hwc commit
#if 1
   e_video_hwc_client_mask_update((E_Video_Hwc *)evhw);
#endif

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_check_if_pp_needed(E_Video_Hwc *evh)
{
   E_Video_Hwc_Windows *evhw;
   int i, count = 0;
   const tbm_format *formats;
   Eina_Bool found = EINA_FALSE;
   E_Hwc *hwc;
   tdm_error error = TDM_ERROR_NONE;

   evhw = (E_Video_Hwc_Windows *)evh;
   hwc = evhw->hwc;
   if (hwc->tdm_hwc_video_stream)
     return EINA_FALSE;

   error = tdm_hwc_get_video_supported_formats(hwc->thwc, &formats, &count);
   if (error != TDM_ERROR_NONE)
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

static  Eina_Bool
_e_video_hwc_windows_iface_commit_available_check(E_Video_Hwc *evh)
{
   E_Video_Hwc_Windows *evhw;

   evhw = (E_Video_Hwc_Windows *)evh;
   return !evhw->commit_data.wait_release;
}

static tbm_surface_h
_e_video_hwc_windows_iface_displaying_buffer_get(E_Video_Hwc *evh)
{
   E_Video_Hwc_Windows *evhw;

   evhw = (E_Video_Hwc_Windows *)evh;
   return evhw->commit_data.buffer;
}

static void
_e_video_hwc_windows_iface_set(E_Video_Hwc_Iface *iface)
{
   iface->destroy = _e_video_hwc_windows_iface_destroy;
   iface->property_get = _e_video_hwc_windows_iface_property_get;
   iface->property_set = _e_video_hwc_windows_iface_property_set;
   iface->available_properties_get = _e_video_hwc_windows_iface_available_properties_get;
   iface->buffer_commit = _e_video_hwc_windows_iface_buffer_commit;
   iface->check_if_pp_needed = _e_video_hwc_windows_iface_check_if_pp_needed;
   iface->commit_available_check = _e_video_hwc_windows_iface_commit_available_check;
   iface->displaying_buffer_get = _e_video_hwc_windows_iface_displaying_buffer_get;
}

EINTERN E_Video_Hwc *
e_video_hwc_windows_create(E_Output *output, E_Client *ec)
{
   E_Video_Hwc_Windows *evhw;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);

   VIN("Create HWC Windows backend", ec);

   evhw = E_NEW(E_Video_Hwc_Windows, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhw, NULL);

   evhw->hwc = output->hwc;
   if (!evhw->hwc)
     {
        free(evhw);
        return NULL;
     }

   evhw->hwc_window = e_hwc_window_new(evhw->hwc, ec, E_HWC_WINDOW_STATE_VIDEO);
   if (!evhw->hwc_window)
     {
        free(evhw);
        return NULL;
     }

   _e_video_hwc_windows_iface_set(&evhw->base.backend);

   return (E_Video_Hwc *)evhw;
}

EINTERN Eina_Bool
e_video_hwc_windows_info_get(E_Video_Hwc *evh, E_Client_Video_Info *info)
{
   E_Video_Hwc_Windows *evhw;

   evhw = (E_Video_Hwc_Windows *)evh;
   memcpy(&info->src_config, &evhw->commit_data.info.src_config, sizeof(tdm_info_config));
   memcpy(&info->dst_pos, &evhw->commit_data.info.dst_pos, sizeof(tdm_pos));
   info->transform = evhw->commit_data.info.transform;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_video_hwc_windows_commit_data_release(E_Video_Hwc *evh, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec)
{
   E_Video_Hwc_Windows *evhw;

   evhw = (E_Video_Hwc_Windows *)evh;
   evhw->commit_data.wait_release = EINA_FALSE;

   if (e_video_hwc_current_fb_update(evh))
     e_video_hwc_wait_buffer_commit(evh);

   return EINA_TRUE;
}

EINTERN tbm_surface_h
e_video_hwc_windows_tbm_surface_get(E_Video_Hwc *evh)
{
   E_Video_Hwc_Windows *evhw;

   evhw = (E_Video_Hwc_Windows *)evh;
   return evhw->commit_data.buffer;
}
