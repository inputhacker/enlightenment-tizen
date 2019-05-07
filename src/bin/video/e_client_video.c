#include "e_video_internal.h"

#define EO_DATA_KEY  "E_Client_Video"

#define INTERNAL_DATA_GET                             \
   E_Client_Video *ecv;                               \
   ecv = evas_object_data_get(ec->frame, EO_DATA_KEY)

#define API_ENTRY                                     \
   INTERNAL_DATA_GET;                                 \
   EINA_SAFETY_ON_NULL_RETURN(ecv)

#define IFACE_CHECK_RET(iname, ret)                   \
   INTERNAL_DATA_GET;                                 \
   EINA_SAFETY_ON_NULL_RETURN_VAL(ecv, ret);          \
   VIN(#iname, ec);                                   \
   if (!ecv->iface->iname)                            \
      return ret

struct _E_Client_Video
{
   /* Composite interface ( hwc planes / hwc windows / fallback ) */
   E_Video_Comp_Iface *iface;

   E_Client *ec;

   Eina_List *event_handlers;

   Eina_Bool redirect;
   Eina_Bool follow_topmost_visibility;
   Eina_Bool allowed_property;
};

static void
_e_client_video_comp_iface_deinit(E_Client_Video *ecv)
{
   ecv->iface->destroy(ecv->iface);
   ecv->iface = NULL;
}

static Eina_Bool
_e_client_video_comp_iface_init(E_Client_Video *ecv, E_Client *ec)
{
   E_Video_Comp_Iface *iface = NULL;
   E_Hwc_Policy hwc_pol;

   if ((e_config->eom_enable == EINA_TRUE) && (e_eom_is_ec_external(ec)))
     {
        VIN("Try to intialize external interface", ec);
        iface = e_video_external_iface_create(ecv);
        goto end;
     }

   if (e_video_debug_display_primary_plane_value_get())
     {
        VIN("Select SW Compositing mode according to configuration", ec);
        goto end;
     }

   hwc_pol = e_zone_video_hwc_policy_get(ec->zone);
   if (hwc_pol != E_HWC_POLICY_NONE)
     {
        VIN("Initialize the interface of the client_video for HWC mode", ec);
        iface = e_video_hwc_iface_create(ecv);
     }

end:
   if (!iface)
     {
        iface = e_video_fallback_iface_create(ecv);
        if (!iface)
          {
             VER("Failed to create 'E_Video_Comp_Iface'", ec);
             return EINA_FALSE;
          }
     }

   ecv->iface = iface;

   return EINA_TRUE;
}

static void
_e_client_video_deinit(E_Client_Video *ecv)
{
   _e_client_video_comp_iface_deinit(ecv);

   E_FREE_LIST(ecv->event_handlers, ecore_event_handler_del);
}

static void
_e_client_video_del(E_Client_Video *ecv)
{
   _e_client_video_deinit(ecv);

   evas_object_data_del(ecv->ec->frame, EO_DATA_KEY);
   e_object_unref(E_OBJECT(ecv->ec));

   free(ecv);
}

static Eina_Bool
_e_client_video_cb_ec_zone_set(void *data, int type EINA_UNUSED, void *event)
{
   E_Client_Video *ecv;
   E_Event_Client_Zone_Set *ev;

   ecv = data;
   ev = event;

   if (ecv->ec != ev->ec)
     goto end;

   /* TODO
    * if compositing policy of given E_Zone is changed, de-initialize previous
    * compositing interface and initialize new compositing interface. */

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_client_video_cb_ec_remove(void *data, int type EINA_UNUSED, void *event)
{
   E_Client_Video *ecv;
   E_Event_Client *ev;

   ev = event;
   ecv = data;
   if (ev->ec != ecv->ec)
     goto end;

   _e_client_video_del(ecv);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_client_video_init(E_Client_Video *ecv, E_Client *ec)
{
   Eina_Bool res;

   ecv->ec = ec;

   res = _e_client_video_comp_iface_init(ecv, ec);
   if (!res)
     {
        VER("Failed to initialize the composition interface for video", ec);
        return EINA_FALSE;
     }

   E_LIST_HANDLER_APPEND(ecv->event_handlers, E_EVENT_CLIENT_ZONE_SET,
                         _e_client_video_cb_ec_zone_set, ecv);
   E_LIST_HANDLER_APPEND(ecv->event_handlers, E_EVENT_CLIENT_REMOVE,
                         _e_client_video_cb_ec_remove, ecv);

   return EINA_TRUE;
}

E_API Eina_Bool
e_client_video_set(E_Client *ec)
{
   E_Client_Video *ecv;
   Eina_Bool res;

   VIN("Set video client", ec);

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->frame, EINA_FALSE);

   if (e_object_is_del(E_OBJECT(ec)))
     {
        VER("Can't handle deleted client", ec);
        return EINA_FALSE;
     }

   ecv = evas_object_data_get(ec->frame, EO_DATA_KEY);
   if (ecv)
     {
        VER("Given client was already set as Video client", ec);
        return EINA_FALSE;
     }

   ecv = E_NEW(E_Client_Video, 1);
   if (!ecv)
     {
        VER("Failed to allocate memory", ec);
        return EINA_FALSE;
     }

   res = _e_client_video_init(ecv, ec);
   if (!res)
     {
        VER("Failed to initialize video setting", ec);
        free(ecv);
        return EINA_FALSE;
     }

   evas_object_data_set(ec->frame, EO_DATA_KEY, ecv);
   e_object_ref(E_OBJECT(ec));

   return EINA_TRUE;
}

E_API void
e_client_video_unset(E_Client *ec)
{
   INTERNAL_DATA_GET;

   if (!ecv)
     {
        VWR("It's not video client or already deleted(%d)",
            ec, e_object_is_del(E_OBJECT(ec)));
        return;
     }

   VIN("Unset video client", ec);

   _e_client_video_del(ecv);
}

E_API Eina_Bool
e_client_video_topmost_visibility_follow(E_Client *ec)
{
   INTERNAL_DATA_GET;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ecv, EINA_FALSE);

   ecv->follow_topmost_visibility = EINA_TRUE;
   return EINA_TRUE;
}

E_API Eina_Bool
e_client_video_topmost_visibility_unfollow(E_Client *ec)
{
   INTERNAL_DATA_GET;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ecv, EINA_FALSE);

   ecv->follow_topmost_visibility = EINA_FALSE;
   return EINA_TRUE;
}

EINTERN Eina_Bool
e_client_video_property_allow(E_Client *ec)
{
   INTERNAL_DATA_GET;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ecv, EINA_FALSE);

   ecv->allowed_property = EINA_TRUE;
   return EINA_TRUE;
}

EINTERN Eina_Bool
e_client_video_property_disallow(E_Client *ec)
{
   INTERNAL_DATA_GET;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ecv, EINA_FALSE);

   ecv->allowed_property = EINA_FALSE;
   return EINA_TRUE;
}

E_API Eina_Bool
e_client_video_property_get(E_Client *ec, unsigned int id, tdm_value *value)
{
   IFACE_CHECK_RET(property_get, EINA_FALSE);

   return ecv->iface->property_get(ecv->iface, id, value);
}

E_API Eina_Bool
e_client_video_property_set(E_Client *ec, unsigned int id, tdm_value value)
{
   IFACE_CHECK_RET(property_set, EINA_FALSE);

   return ecv->iface->property_set(ecv->iface, id, value);
}

EINTERN Eina_Bool
e_client_video_property_delay_set(E_Client *ec, unsigned int id, tdm_value value)
{
   IFACE_CHECK_RET(property_delay_set, EINA_FALSE);

   return ecv->iface->property_delay_set(ecv->iface, id, value);
}

E_API Eina_Bool
e_client_video_available_properties_get(E_Client *ec, const tdm_prop **props, int *count)
{
   IFACE_CHECK_RET(available_properties_get, EINA_FALSE);

   return ecv->iface->available_properties_get(ecv->iface, props, count);
}

EINTERN Eina_Bool
e_client_video_info_get(E_Client *ec, E_Client_Video_Info *info)
{
   IFACE_CHECK_RET(info_get, EINA_FALSE);

   return ecv->iface->info_get(ecv->iface, info);
}

EINTERN Eina_Bool
e_client_video_commit_data_release(E_Client *ec, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec)
{
   IFACE_CHECK_RET(commit_data_release, EINA_FALSE);

   return ecv->iface->commit_data_release(ecv->iface, sequence, tv_sec, tv_usec);
}

EINTERN tbm_surface_h
e_client_video_tbm_surface_get(E_Client *ec)
{
   IFACE_CHECK_RET(tbm_surface_get, EINA_FALSE);

   return ecv->iface->tbm_surface_get(ecv->iface);
}

EINTERN Eina_Bool
e_client_video_comp_redirect_get(E_Client *ec)
{
   INTERNAL_DATA_GET;

   if (!ecv)
     return EINA_FALSE;

   return ecv->redirect;
}

/* Video Internal Functions */
EINTERN E_Client *
e_client_video_ec_get(E_Client_Video *ecv)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ecv, NULL);
   return ecv->ec;
}

EINTERN void
e_client_video_comp_redirect_set(E_Client_Video *ecv)
{
   EINA_SAFETY_ON_NULL_RETURN(ecv);
   ecv->redirect = EINA_TRUE;
}

EINTERN void
e_client_video_comp_redirect_unset(E_Client_Video *ecv)
{
   EINA_SAFETY_ON_NULL_RETURN(ecv);
   ecv->redirect = EINA_FALSE;
}

EINTERN Eina_Bool
e_client_video_topmost_visibility_follow_get(E_Client_Video *ecv)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ecv, EINA_FALSE);
   return ecv->follow_topmost_visibility;
}

EINTERN Eina_Bool
e_client_video_property_allow_get(E_Client_Video *ecv)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ecv, EINA_FALSE);
   return ecv->allowed_property;
}
