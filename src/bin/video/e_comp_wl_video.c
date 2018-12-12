#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e.h"
#include <tdm.h>
#include <values.h>
#include <tdm_helper.h>
#include <wayland-tbm-server.h>
#include <tizen-extension-server-protocol.h>

static int _video_detail_log_dom = -1;

#define VER(fmt, arg...) ELOGF("VIDEO", "<ERR> window(0x%08"PRIxPTR"): "fmt, \
                               video->ec->pixmap, video->ec, video->window, ##arg)
#define VWR(fmt, arg...) ELOGF("VIDEO", "<WRN> window(0x%08"PRIxPTR"): "fmt, \
                               video->ec->pixmap, video->ec, video->window, ##arg)
#define VIN(fmt, arg...) ELOGF("VIDEO", "<INF> window(0x%08"PRIxPTR"): "fmt, \
                               video->ec->pixmap, video->ec, video->window, ##arg)
#define VDB(fmt, arg...) DBG("window(0x%08"PRIxPTR") ec(%p): "fmt, video->window, video->ec, ##arg)

#define DET(...)          EINA_LOG_DOM_DBG(_video_detail_log_dom, __VA_ARGS__)
#define VDT(fmt, arg...)   DET("window(0x%08"PRIxPTR"): "fmt, video->window, ##arg)

typedef struct _E_Video E_Video;

struct _E_Video
{
   struct wl_resource *video_object;
   struct wl_resource *surface;
   E_Client *ec;
   Ecore_Window window;

   Eina_Bool  follow_topmost_visibility;
   Eina_Bool  allowed_attribute;

};

static Eina_List *video_list = NULL;

static void _e_video_set(E_Video *video, E_Client *ec);
static void _e_video_destroy(E_Video *video);

static E_Video *
find_video_with_surface(struct wl_resource *surface)
{
   E_Video *video;
   Eina_List *l;
   EINA_LIST_FOREACH(video_list, l, video)
     {
        if (video->surface == surface)
          return video;
     }
   return NULL;
}

static E_Client *
find_offscreen_parent_get(E_Client *ec)
{
   E_Client *parent = NULL;

   if (!ec->comp_data || !ec->comp_data->sub.data)
     return NULL;

   parent = ec->comp_data->sub.data->parent;
   while (parent)
     {
        if (!parent->comp_data || !parent->comp_data->sub.data)
          return NULL;

        if (parent->comp_data->sub.data->remote_surface.offscreen_parent)
          return parent->comp_data->sub.data->remote_surface.offscreen_parent;

        parent = parent->comp_data->sub.data->parent;
     }

   return NULL;
}

static int
_e_video_get_prop_id(E_Video *video, const char *name)
{
   const tdm_prop *props;
   int i, count = 0;

   e_client_video_available_properties_get(video->ec, &props, &count);
   for (i = 0; i < count; i++)
     {
        if (!strncmp(name, props[i].name, TDM_NAME_LEN))
          {
             VDB("check property(%s)", name);
             return props[i].id;
          }
     }

   return -1;
}

static Eina_Bool
_e_video_is_visible(E_Video *video)
{
   E_Client *offscreen_parent;

   if (e_object_is_del(E_OBJECT(video->ec))) return EINA_FALSE;

   if (!e_pixmap_resource_get(video->ec->pixmap))
     {
        VDB("no comp buffer");
        return EINA_FALSE;
     }

   if (video->ec->comp_data->sub.data && video->ec->comp_data->sub.data->stand_alone)
     return EINA_TRUE;

   offscreen_parent = find_offscreen_parent_get(video->ec);
   if (offscreen_parent && offscreen_parent->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
     {
        VDB("video surface invisible: offscreen fully obscured");
        return EINA_FALSE;
     }

   if (!evas_object_visible_get(video->ec->frame))
     {
        VDB("evas obj invisible");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN void
e_comp_wl_video_hwc_window_commit_data_release(E_Hwc_Window *hwc_window, unsigned int sequence,
                            unsigned int tv_sec, unsigned int tv_usec)
{
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN(ec);

   e_client_video_commit_data_release(ec, sequence, tv_sec, tv_usec);
}

EINTERN tbm_surface_h
e_comp_wl_video_hwc_widow_surface_get(E_Hwc_Window *hwc_window)
{
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, NULL);

   if (!e_hwc_window_is_video(hwc_window))
     {
       ERR("ehw:%p is NOT Video HWC window.", hwc_window);
       return NULL;
     }

   ec = hwc_window->ec;
   if (!ec) return NULL;

   return e_client_video_tbm_surface_get(ec);
}

EINTERN Eina_Bool
e_comp_wl_video_hwc_window_info_get(E_Hwc_Window *hwc_window, tdm_hwc_window_info *hwc_win_info)
{
   E_Client *ec = NULL;
   E_Client_Video_Info vinfo;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (!e_hwc_window_is_video(hwc_window))
     {
       ERR("ehw:%p is NOT Video HWC window.", hwc_window);
       return EINA_FALSE;
     }

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   if (!e_client_video_info_get(ec, &vinfo))
     return EINA_FALSE;

   memcpy(&hwc_win_info->src_config, &vinfo.src_config, sizeof(tdm_info_config));
   memcpy(&hwc_win_info->dst_pos, &vinfo.dst_pos, sizeof(tdm_pos));
   hwc_win_info->transform = vinfo.transform;

   return EINA_TRUE;
}

static E_Video *
_e_video_create(struct wl_resource *video_object, struct wl_resource *surface)
{
   E_Video *video;
   E_Client *ec;

   ec = wl_resource_get_user_data(surface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(ec)), NULL);

   video = calloc(1, sizeof *video);
   EINA_SAFETY_ON_NULL_RETURN_VAL(video, NULL);

   video->video_object = video_object;
   video->surface = surface;

   VIN("create. ec(%p) wl_surface@%d", ec, wl_resource_get_id(video->surface));

   video_list = eina_list_append(video_list, video);

   _e_video_set(video, ec);

   return video;
}

static void
_e_video_set(E_Video *video, E_Client *ec)
{
   Eina_Bool res;
   int minw = -1, minh = -1, maxw = -1, maxh = -1;
   int align = -1;
   int i, count = 0;
   const tdm_prop *props;

   video->ec = ec;
   video->window = e_client_util_win_get(ec);

   EINA_SAFETY_ON_NULL_RETURN(video->ec->zone);

   e_client_video_set(ec);
   /* FIXME workaround */
   if ((e_config->eom_enable == EINA_TRUE) &&
       (e_eom_is_ec_external(ec)))
     return;

   res = e_zone_video_available_size_get(ec->zone, &minw, &minh, &maxw, &maxh, &align);
   if (res)
     {
        tizen_video_object_send_size(video->video_object,
                                     minw, minh, maxw, maxh, align);
        /* VIN("align width: output(%d) pp(%d) video(%d)",
            video->output_align, video->pp_align, video->video_align); */
     }
   else
     VER("Failed to get video available size");

   e_client_video_available_properties_get(ec, &props, &count);
   for (i = 0; i < count; i++)
     {
        tdm_value value;

        res = e_client_video_property_get(ec, props[i].id, &value);
        if (!res)
          {
             VER("Failed to get property name %s value %d", props[i].name, value.u32);
             continue;
          }

        tizen_video_object_send_attribute(video->video_object, props[i].name, value.u32);
     }
}

static void
_e_video_destroy(E_Video *video)
{
   if (!video)
     return;

   VIN("destroy");

   wl_resource_set_destructor(video->video_object, NULL);

   e_client_video_unset(video->ec);

   video_list = eina_list_remove(video_list, video);

   free(video);

#if 0
   if (e_comp_wl_video_buffer_list_length() > 0)
     e_comp_wl_video_buffer_list_print(NULL);
#endif
}

static Eina_Bool
_e_video_cb_ec_remove(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   E_Video *video;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (!ec->comp_data) return ECORE_CALLBACK_PASS_ON;

   video = find_video_with_surface(ec->comp_data->surface);
   if (!video) return ECORE_CALLBACK_PASS_ON;

   _e_video_destroy(video);

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_comp_wl_video_object_destroy(struct wl_resource *resource)
{
   E_Video *video = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(video);

   _e_video_destroy(video);
}

static void
_e_comp_wl_video_object_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_video_object_cb_set_attribute(struct wl_client *client,
                                         struct wl_resource *resource,
                                         const char *name,
                                         int32_t value)
{
   E_Video *video;
   tdm_value v;
   int id;

   video = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(video);

   if(video->ec)
     VDT("Client(%s):PID(%d) RscID(%d) Attribute:%s, Value:%d",
         e_client_util_name_get(video->ec) ?: "No Name",
         video->ec->netwm.pid, wl_resource_get_id(video->surface),
         name, value);

   // check available property & count
   id = _e_video_get_prop_id(video, name);
   if(id < 0)
     {
        VIN("no available property");
        return;
     }

   v.u32 = value;
   if ((!_e_video_is_visible(video)) &&
       /* if mute off, need to do it after buffer commit */
       ((!strncmp(name, "mute", TDM_NAME_LEN)) && (value == 0)))
     {
        e_client_video_property_delay_set(video->ec, id, v);
        return;
     }
   else
     e_client_video_property_set(video->ec, id, v);
}

static void
_e_comp_wl_video_object_cb_follow_topmost_visibility(struct wl_client *client,
                                                     struct wl_resource *resource)
{
   E_Video *video;

   video = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(video);

   if(!video->ec || video->follow_topmost_visibility)
     return;

   VIN("set follow_topmost_visibility");

   video->follow_topmost_visibility= EINA_TRUE;
   e_client_video_topmost_visibility_follow(video->ec);
}

static void
_e_comp_wl_video_object_cb_unfollow_topmost_visibility(struct wl_client *client,
                                                       struct wl_resource *resource)
{
   E_Video *video;

   video = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(video);

   if(!video->ec || !video->follow_topmost_visibility)
     return;

   VIN("set unfollow_topmost_visibility");

   video->follow_topmost_visibility= EINA_FALSE;
   e_client_video_topmost_visibility_unfollow(video->ec);
}

static void
_e_comp_wl_video_object_cb_allowed_attribute(struct wl_client *client,
                                             struct wl_resource *resource)
{
   E_Video *video;

   video = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(video);

   if(!video->ec || video->allowed_attribute)
     return;

   VIN("set allowed_attribute");

   video->allowed_attribute = EINA_TRUE;
   e_client_video_property_allow(video->ec);
}

static void
_e_comp_wl_video_object_cb_disallowed_attribute(struct wl_client *client,
                                                struct wl_resource *resource)
{
   E_Video *video;

   video = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(video);

   if(!video->ec || !video->allowed_attribute)
     return;

   VIN("set disallowed_attribute");

   video->allowed_attribute = EINA_FALSE;
   e_client_video_property_disallow(video->ec);
}

static const struct tizen_video_object_interface _e_comp_wl_video_object_interface =
{
   _e_comp_wl_video_object_cb_destroy,
   _e_comp_wl_video_object_cb_set_attribute,
   _e_comp_wl_video_object_cb_follow_topmost_visibility,
   _e_comp_wl_video_object_cb_unfollow_topmost_visibility,
   _e_comp_wl_video_object_cb_allowed_attribute,
   _e_comp_wl_video_object_cb_disallowed_attribute,
};

static void
_e_comp_wl_video_cb_get_object(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t id,
                               struct wl_resource *surface)
{
   E_Video *video;
   int version = wl_resource_get_version(resource);
   struct wl_resource *res;

   res = wl_resource_create(client, &tizen_video_object_interface, version, id);
   if (res == NULL)
     {
        wl_client_post_no_memory(client);
        return;
     }

   if (!(video = _e_video_create(res, surface)))
     {
        wl_resource_destroy(res);
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_comp_wl_video_object_interface,
                                  video, _e_comp_wl_video_object_destroy);
}

static void
_e_comp_wl_video_cb_get_viewport(struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t id,
                                 struct wl_resource *surface)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(surface))) return;
   if (!ec->comp_data) return;

   if (ec->comp_data && ec->comp_data->scaler.viewport)
     {
        wl_resource_post_error(resource,
                               TIZEN_VIDEO_ERROR_VIEWPORT_EXISTS,
                               "a viewport for that subsurface already exists");
        return;
     }

   if (!e_comp_wl_viewport_create(resource, id, surface))
     {
        ERR("Failed to create viewport for wl_surface@%d",
            wl_resource_get_id(surface));
        wl_client_post_no_memory(client);
        return;
     }
}

static void
_e_comp_wl_video_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tizen_video_interface _e_comp_wl_video_interface =
{
   _e_comp_wl_video_cb_get_object,
   _e_comp_wl_video_cb_get_viewport,
   _e_comp_wl_video_cb_destroy,
};

static void
_e_comp_wl_video_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res;
   const uint32_t *formats = NULL;
   int i, count = 0;

   if (!(res = wl_resource_create(client, &tizen_video_interface, version, id)))
     {
        ERR("Could not create tizen_video_interface resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_comp_wl_video_interface, NULL, NULL);

   e_comp_screen_available_video_formats_get(&formats, &count);
   for (i = 0; i < count; i++)
     tizen_video_send_format(res, formats[i]);
}

static Eina_List *video_hdlrs;

static void
_e_comp_wl_vbuf_print(void *data, const char *log_path)
{
   e_comp_wl_video_buffer_list_print(log_path);
}

static void
_e_comp_wl_video_to_primary(void *data, const char *log_path)
{
   Eina_Bool flag;

   flag = e_video_debug_display_primary_plane_value_get();
   e_video_debug_display_primary_plane_set(!flag);
}

static void
_e_comp_wl_video_punch(void *data, const char *log_path)
{
   Eina_Bool flag;

   flag = e_video_debug_punch_value_get();
   e_video_debug_punch_set(!flag);
}

EINTERN int
e_comp_wl_video_init(void)
{
   e_comp->wl_comp_data->available_hw_accel.underlay = EINA_TRUE;
   DBG("enable HW underlay");

   e_comp->wl_comp_data->available_hw_accel.scaler = EINA_TRUE;
   DBG("enable HW scaler");

   if (!e_comp_wl) return 0;
   if (!e_comp_wl->wl.disp) return 0;
   if (e_comp->wl_comp_data->video.global) return 1;

   e_info_server_hook_set("vbuf", _e_comp_wl_vbuf_print, NULL);
   e_info_server_hook_set("video-to-primary", _e_comp_wl_video_to_primary, NULL);
   e_info_server_hook_set("video-punch", _e_comp_wl_video_punch, NULL);

   _video_detail_log_dom = eina_log_domain_register("e-comp-wl-video", EINA_COLOR_BLUE);
   if (_video_detail_log_dom < 0)
     {
        ERR("Failed eina_log_domain_register()..!\n");
        return 0;
     }

   /* try to add tizen_video to wayland globals */
   e_comp->wl_comp_data->video.global =
      wl_global_create(e_comp_wl->wl.disp, &tizen_video_interface, 1, NULL, _e_comp_wl_video_cb_bind);

   if (!e_comp->wl_comp_data->video.global)
     {
        ERR("Could not add tizen_video to wayland globals");
        return 0;
     }

   E_LIST_HANDLER_APPEND(video_hdlrs, E_EVENT_CLIENT_REMOVE,
                         _e_video_cb_ec_remove, NULL);

   return 1;
}

EINTERN void
e_comp_wl_video_shutdown(void)
{
   e_comp->wl_comp_data->available_hw_accel.underlay = EINA_FALSE;
   e_comp->wl_comp_data->available_hw_accel.scaler = EINA_FALSE;

   E_FREE_FUNC(e_comp->wl_comp_data->video.global, wl_global_destroy);
   E_FREE_LIST(video_hdlrs, ecore_event_handler_del);

   e_info_server_hook_set("vbuf", NULL, NULL);
   e_info_server_hook_set("video-dst-change", NULL, NULL);
   e_info_server_hook_set("video-to-primary", NULL, NULL);
   e_info_server_hook_set("video-punch", NULL, NULL);

   eina_log_domain_unregister(_video_detail_log_dom);
   _video_detail_log_dom = -1;
}